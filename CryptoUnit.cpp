#include "CryptoUnit.h"

#include <openssl/conf.h>
#include <openssl/err.h>


/* NOTE 1 -- This code here is based on OpenSSL 1.1.1, but works with OpenSSL
 * version 3.
 *
 * NOTE 2 -- I have used NULL rather than the modern C++ nullptr when passing
 * arguments to OpenSSL functions, for consistence with the OpenSSL documentation.
 */


/* CryptoUnit::CryptoUnit() just sets up the two OpenSSL cipher contexts which CryptoUnit
 * uses for encryption and decryption. Note that these contexts are stored in unique_ptrs
 * with a customized deleter, so they will be properly released even if this constructor
 * throws an error. The enc_key parameter holds the key to use for encrypting, while the
 * dec_key parameter holds the key to use for decrypting.
 */
CryptoUnit::CryptoUnit(const SecretKey& enc_key, const SecretKey& dec_key)
{
  enc_cipher_ctx = std::unique_ptr<EVP_CIPHER_CTX,CryptoUnitDeleter>(EVP_CIPHER_CTX_new());
  if(nullptr == enc_cipher_ctx.get()){
    throw std::runtime_error("CryptoUnit: EVP_CIPHER_CTX_new failed for encryption context");
  }

  if(1 != EVP_EncryptInit_ex(enc_cipher_ctx.get(), EVP_aes_256_gcm(), NULL, enc_key.data(), NULL)){
    throw std::runtime_error("CryptoUnit: EVP_EncryptInit_ex failed to set cipher and key");
  }

  dec_cipher_ctx = std::unique_ptr<EVP_CIPHER_CTX,CryptoUnitDeleter>(EVP_CIPHER_CTX_new());
  if(nullptr == dec_cipher_ctx.get()){
    throw std::runtime_error("CryptoUnit: EVP_CIPHER_CTX_new failed for decryption context");
  }

  if(1 != EVP_DecryptInit_ex(dec_cipher_ctx.get(), EVP_aes_256_gcm(), NULL, dec_key.data(), NULL)){
    throw std::runtime_error("CryptoUnit: EVP_DecryptInit_ex failed to set cipher and key");
  }
}


/* CryptoUnit::encrypt() computes the encryption of the argument "plaintext" using the
 * CryptoUnit's encryption key, with the given iv and additional data. The ciphertext and
 * AEAD tag are written to "dest" at offset "dest_offset" (the AEAD tag is written after
 * the ciphertext).
 */
void CryptoUnit::encrypt(const std::vector<unsigned char>& plaintext,
                         const std::vector<unsigned char>& additional,
                         const iv_t& iv,
                         std::vector<unsigned char>& dest,
                         std::vector<unsigned char>::size_type dest_offset)
{
  // set the encryption context's iv
  if(1 != EVP_EncryptInit_ex(enc_cipher_ctx.get(), NULL, NULL, NULL, iv.data()))
    throw std::runtime_error("CryptoUnit: EVP_EncryptInit_ex failed to set iv");

  /* We add the additional data to the encryption context, and then encrypt the data. Both
   * of these operations are done via calls to EVP_EncryptUpdate(), and OpenSSL requires that
   * all additional data is added before any encryption happens. We do both of these things in
   * the following single loop to avoid code duplication, with a boolean doing_additional used
   * to signal a switch to encryption after the additional data has been added.
   *
   * We need to use a loop here rather than just having two calls to EVP_EncryptUpdate() (one
   * for additional data and one for encryption) because EVP_EncryptUpdate() does not guarantee
   * to process all the data supplied in one call.
   *
   * I do not know if EVP_EncryptUpdate() can actually fail to process all the bytes passed to
   * it for a stream cipher like GCM, and if so whether the processing can get "stuck" and repeatedly
   * not process any bytes. I cannot rule out these possibilities by reading the OpenSSL docs, and
   * so I must cover them. I deal with the "stuck processing" problem in a crude way, by throwing
   * and error if three consecutive attempts do not process any bytes.
   */
  unsigned int total_done = 0;
  unsigned int num_zero_returns = 0;
  bool doing_additional = true; /* true means we are still adding additional data, false means
                                   we have moved on to encrypting */
  while(true){ // exit from the loop is handled inside the loop

    // if EVP_EncryptUpdate() has made no progress three times in a row, give up
    if(num_zero_returns >= 3){
      throw std::runtime_error("CryptoUnit: EVP_EncryptUpdate could not process all data");
    }

    // if we have finished adding the additional data, move on to the encryption
    if( doing_additional and (total_done == additional.size()) ){
      doing_additional = false;
      total_done = 0;
    }

    // if we have encrypted all the data, exit
    if( (not doing_additional) and (total_done == plaintext.size()) ){
      break;
    }

    int len_out;
    int processing_result = doing_additional ?
      EVP_EncryptUpdate(enc_cipher_ctx.get(), NULL, &len_out, &additional.at(total_done),
                        additional.size()-total_done) :
      EVP_EncryptUpdate(enc_cipher_ctx.get(), &dest.at(dest_offset+total_done), &len_out,
                        &plaintext.at(total_done), plaintext.size()-total_done);

    if(1 != processing_result){
      throw std::runtime_error("CryptoUnit: EVP_EncryptUpdate failed");
    }

    total_done += len_out;
    if(len_out == 0){
      num_zero_returns += 1;
    } else {
      num_zero_returns = 0;
    }
  }

  /* According to the OpenSSL documentation EncryptFinal_ex() is used to write out any remaining
   * ciphertext to the buffer. This is unnecessary with stream ciphers like GCM, and so we pass
   * NULL as the buffer. However, we retain the call to EVP_EncryptFinal_ex() because the examples
   * which I've seen all have it before the subsequent call to EVP_CIPHER_CTX_ctrl() to get the
   * AEAD tag, and the documentation seems to suggest that it should be there.
   */
  int len_out;
  if(1 != EVP_EncryptFinal_ex(enc_cipher_ctx.get(), NULL, &len_out)){
    throw std::runtime_error("CryptoUnit: EVP_EncryptFinal_ex failed");
  }

  /* append the AEAD tag to the ciphertext */
  if(1 != EVP_CIPHER_CTX_ctrl(enc_cipher_ctx.get(), EVP_CTRL_GCM_GET_TAG, 16,
			      &dest.at(dest_offset+plaintext.size()))){
    throw std::runtime_error("CryptoUnit: EVP_CIPHER_CTX_ctrl failed to get tag from encryption");
  }

}


/* CryptoUnit::decrypt() authenticates and decrypts the ciphertext and AEAD tag which begins at
 * offset src_offset in ciphertext_and_tag and has length "length" bytes (including the AEAD
 * tag). The authentication and decryption use CryptoUnit's decryption key, the initialization
 * vector iv, and the additional data in "additional".
 *
 * The pass-by-reference parameter good_tag is used to indicate whether the AEAD authentication
 * tag was valid. Callers should always check this bool on return. If good_tag is true, the
 * tag was valid and the returned value is the decrypted plaintext. If good_tag is false,
 * then the tag was invalid and the returned value is an empty vector. In the latter case,
 * the ciphertext should be discarded.
 *
 * The use of an output parameter avoids using exception handling for something which is not
 * and error, since it is to be expected that attackers might send fraudulent ciphertexts.
 * The creation of an empty vector as a return value in the case of an invalid tag is reasonable,
 * since creating an empty vector is very cheap.
 */
std::vector<unsigned char> CryptoUnit::decrypt(const std::vector<unsigned char>& ciphertext_and_tag,
                                               const std::vector<unsigned char>& additional,
                                               const iv_t& iv,
                                               std::vector<unsigned char>::size_type src_offset,
                                               std::vector<unsigned char>::size_type length,
                                               bool& good_tag)
{
  // set the decryption context's iv
  if(1 != EVP_DecryptInit_ex(dec_cipher_ctx.get(), NULL, NULL, NULL, iv.data())){
    throw std::runtime_error("CryptoUnit: EVP_DecryptInit_ex failed to set iv");
  }

  int ciphertext_size = length-16;
  std::vector<unsigned char> plaintext(ciphertext_size);

  /* We add the additional data to the decryption context, and then perform the decryption.
   * Both of these operations are done via calls to EVP_DecryptUpdate(), and OpenSSL requires
   * all additional data to be added before any decryption is done. The structure of the loop
   * used here is exactly the same as the corresponding loop in CryptoUnit::encrypt(), so see
   * the long comment block before that loop for an explanation.
   */
  unsigned int total_done = 0;
  unsigned int num_zero_returns = 0;
  bool doing_additional = true; /* true means we are still adding additional data, false means
                                   we have moved on to decrypting */
  while(true){ // exit from the loop is handled inside the loop

    // if EVP_DecryptUpdate() has made no progress three times in a row, give up
    if(num_zero_returns >= 3){
      throw std::runtime_error("CryptoUnit: EVP_DecryptUpdate could not process all data");
    }

    // if we have finished adding the additional data, move on to the decryption
    if( doing_additional and (total_done == additional.size()) ){
      doing_additional = false;
      total_done = 0;
    }

    // if we have decrypted all the data, exit
    if( (not doing_additional) and (total_done == plaintext.size()) ){
      break;
    }

    int len_out;
    int processing_result = doing_additional ?
      EVP_DecryptUpdate(dec_cipher_ctx.get(), NULL, &len_out, &additional.at(total_done),
                        additional.size()-total_done) :
      EVP_DecryptUpdate(dec_cipher_ctx.get(), &plaintext.at(total_done), &len_out,
                        &ciphertext_and_tag.at(src_offset+total_done), ciphertext_size-total_done);

    if(1 != processing_result){
      throw std::runtime_error("CryptoUnit: EVP_DecryptUpdate failed");
    }

    total_done += len_out;
    if(len_out == 0){
      num_zero_returns += 1;
    } else {
      num_zero_returns = 0;
    }
  }

  /* pass the AEAD tag to dec_cipher_ctx for checking below */
  unsigned char* tag_start =
    const_cast<unsigned char*>(&ciphertext_and_tag.at(src_offset+ciphertext_size));
  if(1 != EVP_CIPHER_CTX_ctrl(dec_cipher_ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag_start)){
    throw std::runtime_error("CryptoUnit: EVP_CIPHER_CTX_ctrl failed to set the tag for decryption");
  }

  /* EVP_DecryptFinal_ex() checks the AEAD tag */
  int len_out;
  if(1 != EVP_DecryptFinal_ex(dec_cipher_ctx.get(), NULL, &len_out)){
    good_tag = false;
    return std::vector<unsigned char>();
  }

  good_tag = true;
  return plaintext;
}


void CryptoUnit::CryptoUnitDeleter::operator()(EVP_CIPHER_CTX *p)
{
  if(nullptr != p){
    EVP_CIPHER_CTX_free(p);
  }
}

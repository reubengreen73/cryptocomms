/* A class to wrap the encryption/decryption/authentication functionality of
 * AES_256_GCM AEAD provided by OpenSSL.
 */

#ifndef CRYPTOUNIT_H
#define CRYPTOUNIT_H

#include <vector>
#include <stdexcept>
#include <memory>
#include <array>
#include <openssl/evp.h>

#include "SecretKey.h"

/* Note that CryptoUnit does not store the secret key directly in itself, but
 * only indirectly via the enc_cipher_ctx and dec_cipher_ctx members. Thus there
 * is no danger of key leakage via stack memory.
 */
class CryptoUnit
{
public:
  /* we only support the recommended iv length of 12 bytes, so we may as well make
     a type to represent this */
  typedef std::array<unsigned char,12> iv_t;

  CryptoUnit(const SecretKey& key);

  /* We do not want to allow copying, as there is no good way to do this, since we
   * do not want the EVP_CIPHER_CTX objects pointed to by enc_ciphertext_ctx and
   * dec_ciphertext_ctx to be either copied or shared between CryptoUnits. We only
   * allow moves.
   */
  CryptoUnit (const CryptoUnit&) = delete;
  CryptoUnit& operator= (const CryptoUnit&) = delete;

  std::vector<unsigned char> encrypt(const std::vector<unsigned char>& plaintext,
                                     const std::vector<unsigned char>& additional,
				     iv_t iv);
  std::vector<unsigned char> decrypt(std::vector<unsigned char>& tagged_ciphertext,
                                     const std::vector<unsigned char>& additional,
				     iv_t iv, bool& good_tag);

private:
  /* CryptoUnitDeleter is used to customize the behaviour of the unique_ptrs holding
     the encryption and decryption contexts */
  struct CryptoUnitDeleter
  {
    void operator()(EVP_CIPHER_CTX *p);
  };

  /* we use unique_ptrs to hold the OpenSSL encryption/decryption contexts, to ensure
     that they are properly deleted during object destruction (see the operator()
     of CryptoUnitDeleter) */
  std::unique_ptr<EVP_CIPHER_CTX,CryptoUnitDeleter> enc_cipher_ctx;
  std::unique_ptr<EVP_CIPHER_CTX,CryptoUnitDeleter> dec_cipher_ctx;
};

#endif

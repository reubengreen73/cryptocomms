#include "HKDFUnit.h"

#include <stdexcept>
#include <memory>

#include <openssl/evp.h>
#include <openssl/kdf.h>

/* NOTE 1 -- This code here is based on OpenSSL 1.1.1, but works with OpenSSL
 * version 3.
 *
 * NOTE 2 -- I have used NULL rather than the modern C++ nullptr when passing
 * arguments to OpenSSL functions, for consistence with the OpenSSL documentation.
 */

namespace
{

  /* custom deleter for use with a std::unique_ptr holding an EVP_PKEY_CTX to automate
     correct freeing of the EVP_PKEY_CTX */
  struct HKDFUnitDeleter
  {
    void operator()(EVP_PKEY_CTX *ctx)
    {
      if(nullptr != ctx){
        EVP_PKEY_CTX_free(ctx);
      }
    }
  };

}


/* hkdf_expand applies() the HKDF expand operation to the secret key in "secret" with the
 * info parameter in "info" using the SHA256 hash function. Note that we use only the HKDF
 * expand operation here. We use this to derive multiple keys from the same secret (each
 * Connection calls hkdf_expand() twice with the same "secret" but different "info", to derive
 * send and receive keys). The shared secret which two peered Connections share is required to
 * be chosen with cryptographic randomness, so we do not need the HKDF extract operation.
 */
SecretKey hkdf_expand(const SecretKey& secret,
                      const std::vector<unsigned char>& info)
{
  /* Create the EVP_KEY_CTX for the HKDF operation. We hold the EVP_KEY_CTX in a std::unique_ptr
     with a custom deleter to ensure that the EVP_PKEY_CTX is correctly freed automatically when
     this function exits. The NULL argument to EVP_PKEY_CTX_new_id tells OpenSSL to use the
     default "engine" for HKDF. */
  std::unique_ptr<EVP_PKEY_CTX,HKDFUnitDeleter> pkey_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, NULL));
  if(nullptr == pkey_ctx.get()){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_CTX_new_id failed");
  }

  /* initialise the EVP_PKEY_CTX */
  if(EVP_PKEY_derive_init(pkey_ctx.get()) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_derive_init failed");
  }

  /* set the EVP_PKEY_CTX to only do HKDF expand (rather than both extract and expand) */
  if(EVP_PKEY_CTX_hkdf_mode(pkey_ctx.get(), EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_CTX_hkdf_mode failed");
  }

  /* set the hash function to SHA256 */
  if(EVP_PKEY_CTX_set_hkdf_md(pkey_ctx.get(), EVP_sha256()) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_CTX_set_hkdf_md failed");
  }

  /* set the secret key */
  if(EVP_PKEY_CTX_set1_hkdf_key(pkey_ctx.get(), secret.data(), secret_key_size) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_CTX_set1_hkdf_key failed");
  }

  /* set the info parameter */
  if(EVP_PKEY_CTX_add1_hkdf_info(pkey_ctx.get(), info.data(), info.size()) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_CTX_add1_hkdf_info failed");
  }

  /* derive the output key using the HKDF expand operation */
  std::array<unsigned char,secret_key_size> hkdf_output;
  size_t outlen = secret_key_size;
  if(EVP_PKEY_derive(pkey_ctx.get(), hkdf_output.data(), &outlen) != 1){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_derive failed");
  }

  /* check that the expected number of bytes were generated */
  if(outlen != secret_key_size){
    throw std::runtime_error("HKDFUnit: EVP_PKEY_derive wrote the wrong number of bytes");
  }

  /* put the output key bytes into a SecretKey and then zero out the temporary buffer */
  SecretKey output_key(hkdf_output);
  for(auto& x : hkdf_output){
    x = 0;
  }

  return output_key;
}

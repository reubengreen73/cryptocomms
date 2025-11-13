#include "testsys.h"
#include "../CryptoUnit.h"
#include "../SecretKey.h"

#include <string>
#include <vector>


/* The tests in this file just check the output of CryptoUnit against some of the AES 256 GCM
 * test vectors given in Appendix B of the document "The Galois/Counter Mode of Operation (GCM)"
 * by McGrew and Viega. The tests have names of the form CryptoUnit_McGrew_Viega_xx, where xx
 * is the number of the test vector in the document. For each test vector, we check that encryption,
 * decryption, and tag checking work correctly, before making various modifications to the
 * ciphertext, tag, and additional data to check that these modifications are correctly detected
 * by the authentication mechanism.
 *
 * We use all of the test vectors from the document by McGrew and Viega which are suitable for our
 * use case, which only allows 32 byte keys and 12 byte initialization vectors. The other test vectors
 * in the document use different key and iv lengths, but we do not support this.
 *
 * It may seem somewhat odd to use the test vectors in this way, since we are not actually testing
 * the encryption/decryption/authentication routines (since all of this logic is provided by OpenSSL).
 * However, using these test vectors allows us to check that the Cryptocomms code is correctly
 * marshalling data into and out of OpenSSL, and that it is using the results correctly.
 */


/* helper function to convert a string of hex digits to the byte string it represents */
std::vector<unsigned char> bytes_from_hex_string(const std::string& hexstr)
{
  std::vector<unsigned char> byte_vec(hexstr.size()/2);
  for(unsigned int i=0; i < byte_vec.size(); i++){
    byte_vec[i] = std::stoi(hexstr.substr(i*2,2),nullptr,16);
  }
  return byte_vec;
}

/* CryptoUnit takes separate keys for encryption and decryption. The test functions here
 * use two different CryptoUnits, one for encryption and one for decryption, as this
 * reflects the real usage of CryptoUnit in the code. We use a dummy key for the unused
 * encryption/decryption keys in our CryptoUnits. The following key is not used in any
 * of the test vectors, and so is suitable for use as a dummy key.
 */
const SecretKey unused_key("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

/* helper function to check that a test vector is encrypted/decrypted/authenticated
 * correctly
 */
void run_test_vector(const std::string& key_str,
                     const std::string& plaintext_str,
                     const std::string& additional_str,
                     const std::string& iv_str,
                     const std::string& ciphertext_str,
                     const std::string& tag_str,
                     unsigned int ciphertext_offset = 0)
{
  SecretKey secret_key(key_str);
  CryptoUnit crypto_unit_enc(secret_key,unused_key);
  CryptoUnit crypto_unit_dec(unused_key,secret_key);

  typedef std::vector<unsigned char> bytes_t;

  bytes_t plaintext = bytes_from_hex_string(plaintext_str);
  bytes_t additional = bytes_from_hex_string(additional_str);
  bytes_t iv_bytes = bytes_from_hex_string(iv_str);
  bytes_t tagged_ciphertext =
    bytes_from_hex_string(std::string(2*ciphertext_offset,'0') +
                          ciphertext_str+tag_str);

  CryptoUnit::iv_t iv;
  for(CryptoUnit::iv_t::size_type i=0; i<iv.size(); i++){
    iv[i] = iv_bytes[i];
  }

  bytes_t trial_tagged_ciphertext(ciphertext_offset+plaintext.size()+16);
  crypto_unit_enc.encrypt(plaintext, additional, iv,
                          trial_tagged_ciphertext,ciphertext_offset);

  bool tag_valid;
  bytes_t trial_plaintext = crypto_unit_dec.decrypt(tagged_ciphertext,
                                                    additional,
                                                    iv,
                                                    ciphertext_offset,
                                                    plaintext.size()+16,
                                                    tag_valid);

  TESTASSERT(trial_tagged_ciphertext == tagged_ciphertext);
  TESTASSERT(tag_valid);
  TESTASSERT(trial_plaintext == plaintext);
}


/* helper function to check that a tampered set of data is detected correctly */
void check_tamper_detected(const std::string& key_str,
                           const std::string& additional_str,
                           const std::string& iv_str,
                           const std::string& ciphertext_str,
                           const std::string& tag_str)
{
  SecretKey secret_key(key_str);
  CryptoUnit crypto_unit(unused_key,secret_key);

  typedef std::vector<unsigned char> bytes_t;

  bytes_t additional = bytes_from_hex_string(additional_str);
  bytes_t iv_bytes = bytes_from_hex_string(iv_str);
  bytes_t tagged_ciphertext = bytes_from_hex_string(ciphertext_str+tag_str);

  CryptoUnit::iv_t iv;
  for(CryptoUnit::iv_t::size_type i=0; i<iv.size(); i++){
    iv[i] = iv_bytes[i];
  }

  bool tag_valid;
  bytes_t trial_plaintext = crypto_unit.decrypt(tagged_ciphertext,
                                                additional,
                                                iv,
                                                0,tagged_ciphertext.size(),
                                                tag_valid);

  TESTASSERT(not tag_valid);
  TESTASSERT(trial_plaintext == bytes_t());
}


TESTFUNC(CryptoUnit_McGrew_Viega_13)
{
  std::string key_str = "00000000000000000000000000000000"\
                        "00000000000000000000000000000000";
  std::string plaintext_str = "";
  std::string additional_str = "";
  std::string iv_str = "000000000000000000000000";
  std::string ciphertext_str = "";
  std::string tag_str = "530f8afbc74536b9a963b4f1c4cb738b";

  run_test_vector(key_str,plaintext_str,additional_str,
                  iv_str,ciphertext_str,tag_str);

  std::string bad_tag_str = tag_str;
  bad_tag_str[0] = 'a';
  check_tamper_detected(key_str, additional_str, iv_str,
                        ciphertext_str, bad_tag_str);

  std::string bad_ciphertext_str = "00";
  check_tamper_detected(key_str, additional_str, iv_str,
                        bad_ciphertext_str, tag_str);

  std::string bad_additional_str = "00";
  check_tamper_detected(key_str, bad_additional_str, iv_str,
                        ciphertext_str, tag_str);
}


TESTFUNC(CryptoUnit_McGrew_Viega_14)
{
  std::string key_str = "00000000000000000000000000000000"\
                        "00000000000000000000000000000000";
  std::string plaintext_str = "00000000000000000000000000000000";
  std::string additional_str = "";
  std::string iv_str = "000000000000000000000000";
  std::string ciphertext_str = "cea7403d4d606b6e074ec5d3baf39d18";
  std::string tag_str = "d0d1c8a799996bf0265b98b5d48ab919";

  run_test_vector(key_str,plaintext_str,additional_str,
                  iv_str,ciphertext_str,tag_str);

  std::string bad_tag_str = tag_str;
  bad_tag_str[31] = '0';
  check_tamper_detected(key_str, additional_str, iv_str,
                        ciphertext_str, bad_tag_str);

  std::string bad_ciphertext_str_1 =
    ciphertext_str.substr(0,ciphertext_str.length()-2);
  check_tamper_detected(key_str, additional_str, iv_str,
                        bad_ciphertext_str_1, tag_str);

  std::string bad_ciphertext_str_2 = ciphertext_str + "00";
  check_tamper_detected(key_str, additional_str, iv_str,
                        bad_ciphertext_str_2, tag_str);

  std::string bad_ciphertext_str_3 = ciphertext_str;
  bad_ciphertext_str_3[7] = '1';
  check_tamper_detected(key_str, additional_str, iv_str,
                        bad_ciphertext_str_2, tag_str);

  std::string bad_additional_str = "00";
  check_tamper_detected(key_str, bad_additional_str, iv_str,
                        ciphertext_str, tag_str);
}


TESTFUNC(CryptoUnit_McGrew_Viega_15)
{
  std::string key_str = "feffe9928665731c6d6a8f9467308308"\
                        "feffe9928665731c6d6a8f9467308308";
  std::string plaintext_str = "d9313225f88406e5a55909c5aff5269a"\
                              "86a7a9531534f7da2e4c303d8a318a72"\
                              "1c3c0c95956809532fcf0e2449a6b525"\
                              "b16aedf5aa0de657ba637b391aafd255";
  std::string additional_str = "";
  std::string iv_str = "cafebabefacedbaddecaf888";
  std::string ciphertext_str = "522dc1f099567d07f47f37a32a84427d"\
                               "643a8cdcbfe5c0c97598a2bd2555d1aa"\
                               "8cb08e48590dbb3da7b08b1056828838"\
                               "c5f61e6393ba7a0abcc9f662898015ad";
  std::string tag_str = "b094dac5d93471bdec1a502270e3cc6c";

  run_test_vector(key_str,plaintext_str,additional_str,
                  iv_str,ciphertext_str,tag_str);

  std::string bad_tag_str = tag_str;
  bad_tag_str[10] = '2';
  check_tamper_detected(key_str, additional_str, iv_str, ciphertext_str,
                        bad_tag_str);

  std::string bad_ciphertext_str_1 =
    ciphertext_str.substr(0,ciphertext_str.length()-2);
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_1,
                        tag_str);

  std::string bad_ciphertext_str_2 = ciphertext_str + "01";
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_2,
                        tag_str);

  std::string bad_ciphertext_str_3 = ciphertext_str;
  bad_ciphertext_str_3[40] = '0';
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_2,
                        tag_str);

  std::string bad_additional_str = "00";
  check_tamper_detected(key_str, bad_additional_str, iv_str, ciphertext_str,
                        tag_str);
}


TESTFUNC(CryptoUnit_McGrew_Viega_16)
{
  std::string key_str = "feffe9928665731c6d6a8f9467308308"\
                        "feffe9928665731c6d6a8f9467308308";
  std::string plaintext_str = "d9313225f88406e5a55909c5aff5269a"\
                              "86a7a9531534f7da2e4c303d8a318a72"\
                              "1c3c0c95956809532fcf0e2449a6b525"\
                              "b16aedf5aa0de657ba637b39";
  std::string additional_str = "feedfacedeadbeeffeedfacedeadbeef"\
                               "abaddad2";
  std::string iv_str = "cafebabefacedbaddecaf888";
  std::string ciphertext_str = "522dc1f099567d07f47f37a32a84427d"\
                               "643a8cdcbfe5c0c97598a2bd2555d1aa"\
                               "8cb08e48590dbb3da7b08b1056828838"\
                               "c5f61e6393ba7a0abcc9f662";
  std::string tag_str = "76fc6ece0f4e1768cddf8853bb2d551b";

  run_test_vector(key_str,plaintext_str,additional_str,
                  iv_str,ciphertext_str,tag_str);

  std::string bad_tag_str = tag_str;
  bad_tag_str[10] = '2';
  check_tamper_detected(key_str, additional_str, iv_str, ciphertext_str,
                        bad_tag_str);

  std::string bad_ciphertext_str_1 =
    ciphertext_str.substr(0,ciphertext_str.length()-2);
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_1,
                        tag_str);

  std::string bad_ciphertext_str_2 = ciphertext_str + "ab";
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_2,
                        tag_str);

  std::string bad_ciphertext_str_3 = ciphertext_str;
  bad_ciphertext_str_3[20] = '2';
  check_tamper_detected(key_str, additional_str, iv_str, bad_ciphertext_str_2,
                        tag_str);

  std::string bad_additional_str_1 =
    additional_str.substr(0,additional_str.length()-2);
  check_tamper_detected(key_str, bad_additional_str_1, iv_str, ciphertext_str,
                        tag_str);

  std::string bad_additional_str_2 = additional_str + "01";
  check_tamper_detected(key_str, bad_additional_str_2, iv_str, ciphertext_str,
                        tag_str);

  std::string bad_additional_str_3 = additional_str;
  bad_additional_str_3[7] = 'b';
  check_tamper_detected(key_str, bad_additional_str_3, iv_str, ciphertext_str,
                        tag_str);
}


/* test the feature of CryptoUnit::encrypt and ::decrypt which allows the ciphertext
   and tag to be written/read at an offset in the vector of bytes */
TESTFUNC(CryptoUnit_offset)
{
  // test vector as for McGrew_Viega_13 above
  std::string key_str = "00000000000000000000000000000000"\
                        "00000000000000000000000000000000";
  std::string plaintext_str = "";
  std::string additional_str = "";
  std::string iv_str = "000000000000000000000000";
  std::string ciphertext_str = "";
  std::string tag_str = "530f8afbc74536b9a963b4f1c4cb738b";

  // run the test, with a ciphertext offset of 17
  run_test_vector(key_str,plaintext_str,additional_str,
                  iv_str,ciphertext_str,tag_str,17);
}

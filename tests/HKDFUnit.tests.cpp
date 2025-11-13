#include "testsys.h"
#include "../SecretKey.h"
#include "../HKDFUnit.h"

#include <vector>
#include <string>

/* helper function to convert a string of hex digits to the byte string it represents */
std::vector<unsigned char> hkdf_bytes_from_hex_string(const std::string& hexstr)
{
  std::vector<unsigned char> byte_vec(hexstr.size()/2);
  for(unsigned int i=0; i < byte_vec.size(); i++){
    byte_vec[i] = std::stoi(hexstr.substr(i*2,2),nullptr,16);
  }
  return byte_vec;
}

/* helper function which checks the output of hkdf_expand against a test vector */
void run_test_vector(const std::string& info_hex_str,
                     const std::string& input_key_hex_str,
                     const std::string& output_key_hex_str)
{
  SecretKey input_key(input_key_hex_str);
  std::vector<unsigned char> info = hkdf_bytes_from_hex_string(info_hex_str);
  SecretKey expected_output_key(output_key_hex_str);

  SecretKey actual_output_key = hkdf_expand(input_key,info);

  for(unsigned int i=0; i<secret_key_size; i++){
    TESTASSERT(actual_output_key[i] == expected_output_key[i]);
  }
}

/* The TESTFUNCs below use test vectors based on those in RFC5869 "HMAC-based Extract-and-Expand Key
 * Derivation Function (HKDF)" by H. Krawczyk. However, I have changed the output length to 32 bytes
 * in all of them (and truncated the expected key to 32 bytes), since this is the only key length we
 * allow (and indeed SecretKey can only hold keys of this length).
 */

TESTFUNC(HKDFUnit_test_vector_1)
{
  std::string hex_info = "f0f1f2f3f4f5f6f7f8f9";
  std::string hex_secret = "077709362c2e32df0ddc3f0dc47bba63"\
                           "90b6c73bb50f9c3122ec844ad7c2b3e5";
  std::string hex_expected = "3cb25f25faacd57a90434f64d0362f2a"\
                             "2d2d0a90cf1a5a4c5db02d56ecc4c5bf";
  run_test_vector(hex_info,hex_secret,hex_expected);
}


TESTFUNC(HKDFUnit_test_vector_2)
{
   std::string hex_info = "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"\
                          "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"\
                          "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"\
                          "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"\
                          "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
   std::string hex_secret  = "06a6b88c5853361a06104c9ceb35b45c"\
                             "ef760014904671014a193f40c15fc244";
   std::string hex_expected  = "b11e398dc80327a1c8e7f78c596a4934"\
                                "4f012eda2d4efad8a050cc4c19afa97c";
  run_test_vector(hex_info,hex_secret,hex_expected);
}

TESTFUNC(HKDFUnit_test_vector_3)
{
  std::string hex_info = "";
  std::string hex_secret  = "19ef24a32c717b167f33a91d6f648bdf"  \
                           "96596776afdb6377ac434c1c293ccb04";
  std::string hex_expected = "8da4e775a563c18f715f802a063c5a31"\
                             "b8a11f5c5ee1879ec3454e5f3c738d2d";
  run_test_vector(hex_info,hex_secret,hex_expected);
}

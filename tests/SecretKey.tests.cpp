#include "testsys.h"
#include "../SecretKey.h"

#include <string>

/* test that the constructor which takes a hex string produces the correct key*/
TESTFUNC(construct_from_hex_str)
{
  std::string hex_str = "00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00";
  SecretKey sk(hex_str);

  /* check that the correct key was generated */
  bool keys_match = true;
  for(unsigned int i=0;i<secret_key_size;i++){
    unsigned char byte_val = std::stoi(hex_str.substr(i*2,2),nullptr,16);
    keys_match = keys_match && (byte_val == sk[i]);
  }

  TESTASSERT(keys_match);
}

/* test that hex strings which are too long or short produce the correct error */
TESTFUNC(hex_str_too_short_or_long)
{
  std::string hex_str_short = "00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF0";
  TESTTHROW(SecretKey sk(hex_str_short),"SecretKey: initialization string has wrong length");

  std::string hex_str_long = "00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF000";
  TESTTHROW(SecretKey sk(hex_str_long),"SecretKey: initialization string has wrong length");
}

/* test that hex strings with junk characters throw an error */
TESTFUNC(hex_invalid_char)
{
  std::string hex_str;

  hex_str = "G0010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00";
  TESTTHROW(SecretKey sk(hex_str),"SecretKey: \"G\" is not a valid hex digit");

  hex_str = " 0010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00";
  TESTTHROW(SecretKey sk(hex_str),"SecretKey: \" \" is not a valid hex digit");
}

/* check that a default-initialized SecretKey is not valid */
TESTFUNC(default_initialized_invalid)
{
  SecretKey sk;
  TESTTHROW(sk.data(),"SecretKey: key used while invalid");
}

/* check that a moved-from SecretKey is not valid */
TESTFUNC(moved_from_invalid)
{
  std::string hex_str = "00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00";
  SecretKey sk1(hex_str);
  SecretKey sk2 = std::move(sk1);
  TESTTHROW(sk1.data(),"SecretKey: key used while invalid");
}

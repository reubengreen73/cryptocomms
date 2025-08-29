#include "SecretKey.h"

#include <stdexcept>

namespace
{
  /* we forward-declare add_hex_to_int() so that we can put the
   * long-winded definition of the function (it contains a big
   * switch statement) at the bottom of the file.
   */
  void add_hex_to_int(const char& hex_in, unsigned char& int_out);
}


/* SecretKey::SecretKey() initializes a SecretKey into an invalid
 * state. Note that we do not need to initialize key_ as the
 * SecretKey is marked as not valid.
 */
SecretKey::SecretKey():
  valid_(false){}


/* SecretKey::SecretKey(const std::string& str) constructs a SecretKey which
 * holds the key specified in hexadecimal by the string str. str must consist
 * of exactly 2*secret_key_size characters, all of which must be one of
 * 0123456789AaBbCcDdEeFf . Each pair of characters is interpreted as a single
 * byte, and thus the string as a whole is interpreted as a sequence of
 * secret_key_size bytes, which is set as the value of this->key_.
 *
 */
SecretKey::SecretKey(const std::string& str):
  valid_(false)
{
  if(str.size() != 2*secret_key_size){
    throw std::runtime_error("SecretKey: initialization string has wrong length");
  }

  try{
    for(unsigned int i=0;i<secret_key_size;i++){
      /* Note that the method used here avoids copying any character from str
       * to another location in memory, or putting any part of the key's value
       * anywhere except in this->key_. This restriction is why add_hex_to_int()
       * is implemented in the strange-seeming way that it is: both arguments
       * are taken by reference, and the value from the first argument is *added*
       * to the second argument (this also explains why we need our special
       * add_hex_to_int() function rather than just using eg std::stoi).
       */
      key_[i]=0;
      add_hex_to_int(str[i*2],key_[i]);
      key_[i] *= 16;
      add_hex_to_int(str[(i*2)+1],key_[i]);
    }
  }
  catch(std::invalid_argument& e){
    throw std::runtime_error(std::string("SecretKey: ")+e.what());
  }
  catch(...){
    throw std::runtime_error("SecretKey: error initializing from hex string");
  }

  valid_ = true;
}


/* move assignment includes explicit zero-ing of the old key */
SecretKey& SecretKey::operator=(SecretKey&& other)
{
  if(this == &other){
    return *this;
  }

  for(unsigned int i=0;i<secret_key_size;i++){
    key_[i] = other.key_[i];
  }
  valid_ = other.valid_;
  other.erase();

  return *this;
}


SecretKey& SecretKey::operator=(const SecretKey& other)
{
  for(unsigned int i=0;i<secret_key_size;i++){
    key_[i] = other.key_[i];
  }
 valid_ = other.valid_;

  return *this;
}


SecretKey::SecretKey(SecretKey&& other)
{ *this = std::move(other); }


SecretKey::SecretKey(const SecretKey& other)
{ *this = other; }


void SecretKey::erase()
{
  for(unsigned int i=0;i<secret_key_size;i++){
    key_[i]=0;
  }
  valid_ = false;
}


SecretKey::~SecretKey()
{ erase(); }


/* Note that SecretKey::data() checks its validity before handing
 * out the pointer to its key data, but after this it is the
 * responsibility of whatever owns the SecretKey to ensure that
 * it does not become invalid while the key data is still in use.
 */
unsigned char* SecretKey::data()
{
  check_valid();
  return key_;
}


/* see the note above the non-const version of SecretKey::data() for
   information on validity checking.
 */
const unsigned char* SecretKey::data() const
{
  check_valid();
  return key_;
}


unsigned char& SecretKey::operator[](unsigned int pos)
{
  check_valid();
  if(pos > (secret_key_size-1)){
    throw std::runtime_error("SecretKey: index out of range");
  }
  return key_[pos];
}


/* Note that this operator[] returns by const reference, with a reference to
 * this->key_. If the caller captures this reference, than all copying of the
 * secret key is avoided.
 */
const unsigned char&  SecretKey::operator[](unsigned int pos) const
{
  check_valid();
  if(pos > (secret_key_size-1)){
    throw std::runtime_error("SecretKey: index out of range");
  }
  return key_[pos];
}


/* note that this validity checking is not thread-safe */
void SecretKey::check_valid() const
{
  if(not valid_){
    throw std::runtime_error("SecretKey: key used while invalid");
  }
}


namespace
{

  /* add_hex_to_int() is a simple utility function to help convert hex strings to unsigned
   * char values while avoiding copying these secret values around in memory. See the comments
   * in SecretKey::SecretKey(const std::string& str) for an explanation of the design choices
   * in this function (and indeed why it was necessary to create it rather than using standard
   * library functions). The key thing to note is that the value in hex_in is never copied out
   * of that variable, and the only place where the value is written to memory is in int_out.
   */
  void add_hex_to_int(const char& hex_in, unsigned char& int_out)
  {
    switch(hex_in){
    case '0':
      break;
    case '1':
      int_out += 1;
      break;
    case '2':
      int_out += 2;
      break;
    case '3':
      int_out += 3;
      break;
    case '4':
      int_out += 4;
      break;
    case '5':
      int_out += 5;
      break;
    case '6':
      int_out += 6;
      break;
    case '7':
      int_out += 7;
      break;
    case '8':
      int_out += 8;
      break;
    case '9':
      int_out += 9;
      break;
    case 'a':
      int_out += 10;
      break;
    case 'b':
      int_out += 11;
      break;
    case 'c':
      int_out += 12;
      break;
    case 'd':
      int_out += 13;
      break;
    case 'e':
      int_out += 14;
      break;
    case 'f':
      int_out += 15;
      break;
    case 'A':
      int_out += 10;
      break;
    case 'B':
      int_out += 11;
      break;
    case 'C':
      int_out += 12;
      break;
    case 'D':
      int_out += 13;
      break;
    case 'E':
      int_out += 14;
      break;
    case 'F':
      int_out += 15;
      break;
    default:
      auto char_str = std::string{hex_in};
      throw std::invalid_argument("\""+char_str+"\" is not a valid hex digit");
    }
  }

}

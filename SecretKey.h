/* SecretKey is a simple class for storing 32 byte cryptographic keys. SecretKey
 * has been designed to provide maximum in-memory security of the secret key. To
 * this end, the destructor erases the key from memory, and the member functions
 * avoid placing any part of the secret key into function arguments, local variables,
 * or return values.
 * SecretKey does some limited tracking of its own validity (e.g. whether the key data
 * has been initialized, whether the SecretKey has been moved from), but this validity
 * checking is not thread-safe.
 */

#ifndef SECRETKEY_H
#define SECRETKEY_H

#include <string>

class SecretKey
{
public:
  SecretKey();
  SecretKey(SecretKey&& other);
  SecretKey& operator=(SecretKey&& other);
  SecretKey(const SecretKey& other);
  SecretKey& operator=(const SecretKey& other);

  SecretKey(const std::string& str);

  ~SecretKey();

  unsigned char& operator[](unsigned int pos);
  const unsigned char&  operator[](unsigned int pos) const;

  unsigned char* data();
  const unsigned char* data() const;

  void check_valid() const;
  void erase();

private:
  /* The valid_ private member allows the SecretKey to know if it contains
   * a valid key or not. A SecretKey is not valid, for example, after being
   * default-constructed or being moved from. It is very important that SecretKeys
   * are not used for encryption when they are in this state, as they contain a
   * key which may be all zeros or consist of values from uninitialized memory.
   */
  bool valid_;
  unsigned char key_[32];
};

#endif

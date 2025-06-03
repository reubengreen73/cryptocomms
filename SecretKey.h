/* SecretKey is a simple class for storing 32 byte cryptographic keys. SecretKey
 * has been designed to provide maximum in-memory security of the secret key. To
 * this end, the destructor erases the key from memory, and the member functions
 * avoid placing any part of the secret key into function arguments, local variables,
 * or return values.
 */

#ifndef SECRETKEY_H
#define SECRETKEY_H

#include <string>

class SecretKey
{
public:
  SecretKey() = default;
  SecretKey(SecretKey&& other);
  SecretKey& operator=(SecretKey&& other);
  SecretKey(const SecretKey& other);
  SecretKey& operator=(const SecretKey& other);

  SecretKey(const std::string& str);

  ~SecretKey();

  unsigned char& operator[](std::size_t pos);
  const unsigned char&  operator[](std::size_t pos) const;

  unsigned char* data();
  const unsigned char* data() const;

  void erase();

private:
  unsigned char key[32];
};

#endif

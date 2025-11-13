/* A simple function to wrap OpenSSL's HKDF expand functionality. This function has been
 * separated into its own unit to allow it to have its own test suite, since it is vitally
 * important that this function works correctly to ensure crytographic security.
 */

#ifndef HKDF_UNIT_H
#define HKDF_UNIT_H

#include <vector>

#include "SecretKey.h"

SecretKey hkdf_expand(const SecretKey& secret,
                      const std::vector<unsigned char>& info);

#endif

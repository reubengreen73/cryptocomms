#include "PeerConfig.h"

/* This destructor just blanks out the secret key, which helps prevent key leakage
 * through memory reuse.
 */
PeerConfig::~PeerConfig()
{
  for(int i=0;i<32;i++){
    key[i] = 0;
  }
}

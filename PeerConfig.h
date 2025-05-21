/* A simple class to represent the configuration of a peer. This
 * class is really just an aggregate of data, but the destructor
 * does zeroing of the std::array containing the secret key.
 */

#ifndef PEERCONFIG_H
#define PEERCONFIG_H

#include <string>
#include <array>
#include <vector>
#include <utility>

typedef std::pair<std::array<unsigned char,2>,std::string> channel_spec;

class PeerConfig
{
public:
  std::string name;
  std::array<unsigned char,4> id;
  std::array<unsigned char,32> key;
  std::vector<channel_spec> channels;
  std::string ip_addr;
  uint16_t port;
  uint16_t max_packet_size;
  ~PeerConfig();
};


#endif

/* A simple class to represent the configuration of a peer. This
 * class is really just a convenient bundle of data.
 */

#ifndef PEERCONFIG_H
#define PEERCONFIG_H

#include <string>
#include <array>
#include <vector>
#include <utility>

#include "SecretKey.h"

typedef std::pair<std::array<unsigned char,2>,std::string> channel_spec;

class PeerConfig
{
public:
  std::string name;
  std::array<unsigned char,4> id;
  SecretKey key;
  std::vector<channel_spec> channels;
  std::string ip_addr;
  int port;
  int max_packet_size;
  void clear();
};


#endif

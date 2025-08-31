/* A simple class to represent the configuration of a peer. This
 * class is really just a convenient bundle of data.
 */

#ifndef PEERCONFIG_H
#define PEERCONFIG_H

#include <string>
#include <array>
#include <vector>
#include <utility>
#include <netinet/in.h> // for in_port_t

#include "IDTypes.h"
#include "SecretKey.h"

typedef std::pair<channel_id_type,std::string> channel_spec;

class PeerConfig
{
public:
  std::string name;
  host_id_type id;
  SecretKey key;
  std::vector<channel_spec> channels;
  std::string ip_addr;
  in_port_t port;
  int max_packet_size; // a value of -1 here indicates no max packet size set
  void clear();
};


#endif

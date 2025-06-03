/* ConfigFileParser parses configuration for a Cryptocomms system from a
 * configuration file, and makes that configuration available via its public members.
 */

#ifndef CONFIGFILEPARSER_H
#define CONFIGFILEPARSER_H

#include <string>
#include <vector>
#include <array>

#include "PeerConfig.h"

class ConfigFileParser
{
public:
  ConfigFileParser(const std::string& path);
  std::vector<PeerConfig> peer_configs;
  std::array<unsigned char,4> id;
  std::string ip_addr;
  uint16_t port;
  unsigned int max_packet_size;
};

#endif

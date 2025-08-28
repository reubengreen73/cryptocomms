/* ConfigFileParser parses configuration for a Cryptocomms system from a
 * configuration file, and makes that configuration available via its public members.
 */

#ifndef CONFIGFILEPARSER_H
#define CONFIGFILEPARSER_H

#include <string>
#include <vector>
#include <array>
#include <cstdint>

#include "IDTypes.h"
#include "PeerConfig.h"

class ConfigFileParser
{
public:
  ConfigFileParser(const std::string& path);
  std::vector<PeerConfig> peer_configs;
  host_id_type id;
  std::string ip_addr;
  std::uint16_t port;
  int max_packet_size; // a value of -1 here indicates no max packet size set
};

#endif

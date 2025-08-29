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
  host_id_type self_id;
  std::string self_ip_addr;
  std::uint16_t self_port;
  int default_max_packet_size; // a value of -1 here indicates no default max packet size set
};

#endif

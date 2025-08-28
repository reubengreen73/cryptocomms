#include "PeerConfig.h"

/* PeerConfig::clear() clears the state of the PeerConfig */
void PeerConfig::clear()
{
  name = "";
  channels = {};
  ip_addr = "";
  port = 0;
  max_packet_size = -1;

  for(int i=0; i<host_id_size; i++){
    id[i] = 0;
  }

  key.erase();
}

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <string>

#include "FifoIO.h"
#include "UDPSocket.h"
#include "SegmentNumGenerator.h"
#include "SecretKey.h"
#include "PeerConfig.h"
#include "ConfigFileParser.h"
#include "Session.h"

/* A simple version of the main "cryptocomms" program, which just takes a config
 * file, sets a Session running, and then just idles.
 */
int main(int argc, char** argv){
  if(argc != 2){
    std::cout << "Usage: " << argv[0] << " <config-file>\n";
    return 0;
  }

  ConfigFileParser cfp(argv[1]);
  int default_max_packet_size = (cfp.default_max_packet_size == -1) ?
    1200 : cfp.default_max_packet_size;
  std::string segnum_filepath = (cfp.segnum_filepath == "") ?
    "segnumfile" : cfp.segnum_filepath;
  Session session(cfp.self_id,
                  cfp.self_ip_addr,cfp.self_port,
                  default_max_packet_size,
                  cfp.peer_configs,
                  segnum_filepath,
                  5);

  while(true)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  return 0;
}

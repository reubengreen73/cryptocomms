/* Session is the primary top-level object of the application, representing a
 * single running instance of cryptocomms. Session provides the interface via
 * which an instance of cryptocomms is created and run.
 */

/* This unit is currently under construction, and does not yet implement the
 * full functionality which Session will have.
 */

#ifndef SESSION_H
#define SESSION_H

#include <string>
#include <array>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <map>
#include <netinet/in.h> // for in_port_t

#include "IDTypes.h"
#include "Connection.h"
#include "SegmentNumGenerator.h"
#include "PeerConfig.h"
#include "UDPSocket.h"

class Session{
public:
  Session(const host_id_type& self_id,
	  const std::string& self_ip_addr,
	  in_port_t self_port,
	  unsigned int default_max_packet_size,
	  const std::vector<PeerConfig>& peer_configs,
	  const std::string& segnum_file_path);
  void start();
  void stop();

private:
  typedef std::array<unsigned char,host_id_size+channel_id_size> connection_id_type;

  host_id_type self_id_;
  std::map<connection_id_type,std::unique_ptr<Connection>> connections_;
  unsigned int default_max_packet_size_;
  std::shared_ptr<UDPSocket> udp_socket_;
  std::shared_ptr<SegmentNumGenerator> segnumgen_;

};

#endif

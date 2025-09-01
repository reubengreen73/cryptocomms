#include "Session.h"


#include <algorithm>
#include <chrono>


/* The content of the body of Session::Session() is just creating all of the Connection objects.
 * Note that each Connection object represents a single channel of communication with a remote
 * host. If the given configuration includes multiple channels with another host, then each channel
 * will get a separate Connection.
 */
Session::Session(const host_id_type& self_id,
		 const std::string& self_ip_addr,
		 in_port_t self_port,
		 unsigned int default_max_packet_size,
		 const std::vector<PeerConfig>& peer_configs,
		 const std::string& segnum_file_path):
  self_id_(self_id),
  default_max_packet_size_(default_max_packet_size),
  udp_socket_(std::make_shared<UDPSocket>(self_ip_addr,self_port)),
  segnumgen_(std::make_shared<SegmentNumGenerator>(segnum_file_path,2*peer_configs.size()))
{
  for(auto const& peer_config : peer_configs){ // for each remote peer...
    for(auto const& ch_spec : peer_config.channels){ // ...loop through all the channels for that
                                                     // peer and create a Connection for each

      // a value of -1 in peer_config.max_packet_size indicates that no maximum packet size
      // was specified for this peer
      unsigned int max_packet_size = (peer_config.max_packet_size == -1) ?
	default_max_packet_size : peer_config.max_packet_size;

      // concatenate the peer's host id and the channel id to create the full id for this
      // Connection
      connection_id_type full_id;
      std::copy(peer_config.id.begin(),peer_config.id.end(),full_id.begin());
      std::copy(ch_spec.first.begin(),ch_spec.first.end(),full_id.begin()+host_id_size);

      connections_.insert({full_id,std::make_unique<Connection>(self_id,
								peer_config.name,
								peer_config.id,
								ch_spec.first,
								ch_spec.second,
								peer_config.key,
								peer_config.ip_addr,
								peer_config.port,
								max_packet_size,
								udp_socket_,
								segnumgen_)});

    }
  }

}


void Session::start()
{
}


void Session::stop()
{
}

/* A Connection object represents a single channel of communication to
 * a peer. Connections are responsible for the actual movement of data
 * from the "from the user" fifo to the network, and from the UDP
 * messages received from the network to the "to the user" fifo, including
 * all en-/decryption, authentication, and resending of data.
 */

/* This unit is currently under construction, and does not yet implement the
 * full functionality which Connetion will have.
 */

#ifndef CONNECTION_H
#define CONNECTION_H

#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <utility>
#include <array>
#include <netinet/in.h> // for in_port_t

#include "IDTypes.h"
#include "UDPSocket.h"
#include "FifoIO.h"
#include "SecretKey.h"
#include "SegmentNumGenerator.h"

class Connection
{
public:
  Connection(const host_id_type& self_id,
	     const std::string& peer_name,
	     const host_id_type& peer_id,
	     const channel_id_type& channel_id,
	     const std::string& fifo_base_path,
	     const SecretKey& key,
	     const std::string& peer_ip_addr,
	     in_port_t peer_port,
	     unsigned int max_packet_size,
	     const std::shared_ptr<UDPSocket>& udp_socket,
	     const std::shared_ptr<SegmentNumGenerator>& segnumgen);
  bool move_data(unsigned int loop_max);
  void add_message(const ReceivedUDPMessage& msg);
  void add_message(ReceivedUDPMessage&& msg);

private:
  host_id_type self_id_;
  std::string peer_name_;
  host_id_type peer_id_;
  channel_id_type channel_id_;
  std::string peer_ip_addr_;
  in_port_t peer_port_;
  unsigned int max_packet_size_;
  std::shared_ptr<UDPSocket> udp_socket_;
  std::shared_ptr<SegmentNumGenerator> segnumgen_;

  FifoFromUser fifo_from_user_;
  FifoToUser fifo_to_user_;
  std::deque<ReceivedUDPMessage> message_queue_;
  std::mutex queue_lock_;
};

#endif

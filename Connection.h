/* A Connection object represents a single channel of communication to
 * a peer. Connections are responsible for the actual movement of data
 * from the "from the user" FIFO to the network, and from the UDP
 * messages received from the network to the "to the user" FIFO, including
 * all en-/decryption, authentication, and resending of data.
 */

/* This unit is currently under construction, and does not yet implement the
 * full functionality which Connection will have.
 */

#ifndef CONNECTION_H
#define CONNECTION_H

#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <netinet/in.h> // for in_port_t
#include <utility>

#include "IDTypes.h"
#include "UDPSocket.h"
#include "FifoIO.h"
#include "SecretKey.h"
#include "SegmentNumGenerator.h"
#include "RTTTracker.h"
#include "CryptoUnit.h"
#include "EpochTime.h"
#include "CryptoMessageTracker.h"

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
  void move_data(unsigned int loop_max);
  bool is_data();
  void add_message(const ReceivedUDPMessage& msg);
  void add_message(ReceivedUDPMessage&& msg);
  int from_user_fifo_fd();
  std::pair<bool,millis_timestamp_t> open_status();

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
  std::unique_ptr<CryptoUnit> crypto_unit_;
  std::shared_ptr<RTTTracker> rtt_tracker_;

  FifoFromUser fifo_from_user_;
  FifoToUser fifo_to_user_;
  std::deque<ReceivedUDPMessage> message_queue_;
  std::mutex queue_lock_;
  CryptoMessageTracker current_crypto_message_tracker_;
  CryptoMessageTracker old_crypto_message_tracker_;
  /* a value of 0 in current_peer_segnum_ or old_peer_segnum_
     indicates that the corresponding CryptoMessageTracer is not
     currently in use */
  SegmentNumGenerator::segnum_t current_peer_segnum_;
  SegmentNumGenerator::segnum_t old_peer_segnum_;
  SegmentNumGenerator::segnum_t current_local_segnum_;
  /* a value of 0 in old_local_segnum_ means that there is no
     old local segment number */
  SegmentNumGenerator::segnum_t old_local_segnum_;
  CryptoMessageTracker::msgnum_t local_next_msgnum_;
  millis_timestamp_t last_hello_packet_sent_;

  struct MessageOuterHeader
  {
    host_id_type sender_id;
    channel_id_type channel_id;
    SegmentNumGenerator::segnum_t my_segnum;
    SegmentNumGenerator::segnum_t peer_segnum;
    CryptoMessageTracker::msgnum_t msgnum;
    /* iv and ad hold the same information as peer_segnum and msgnum (in iv)
       and my_segnum (in ad), but as bytes which are convenient for passing
       to the methods of CryptoUnit
     */
    CryptoUnit::iv_t iv;
    std::array<unsigned char,6> ad;
  };

  MessageOuterHeader unpack_header(const std::vector<unsigned char>& message_bytes);
  std::vector<unsigned char> create_packet(const std::vector<unsigned char>& data_bytes,
                                           SegmentNumGenerator::segnum_t peer_segnum = 0);
  void handle_message(std::vector<unsigned char>& message_data);
};

#endif

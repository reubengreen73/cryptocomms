#include "Connection.h"


/* suffixes for the file names of the FIFOs where data goes in and out of the program */
constexpr char fifo_from_user_suffix[] = "_OUTWARD";
constexpr char fifo_to_user_suffix[] = "_INWARD";


Connection::Connection(const host_id_type& self_id,
		       const std::string& peer_name,
		       const host_id_type& peer_id,
		       const channel_id_type& channel_id,
		       const std::string& fifo_base_path,
		       const SecretKey& key,
		       const std::string& peer_ip_addr,
		       in_port_t peer_port,
		       unsigned int max_packet_size,
		       const std::shared_ptr<UDPSocket>& udp_socket,
		       const std::shared_ptr<SegmentNumGenerator>& segnumgen):
  self_id_(self_id),
  peer_name_(peer_name),
  peer_id_(peer_id),
  channel_id_(channel_id),
  peer_ip_addr_(peer_ip_addr),
  peer_port_(peer_port),
  max_packet_size_(max_packet_size),
  udp_socket_(udp_socket),
  segnumgen_(segnumgen),
  fifo_from_user_(fifo_base_path+fifo_from_user_suffix),
  fifo_to_user_(fifo_base_path+fifo_to_user_suffix)
{}


/* This function is temporary code which implements very simplistic logic for
 * moving data from the "from user" fifo to the UDP socket, and for moving data
 * that has come from the UDP socket to the "to user" fifo.
 *
 * The function just runs a loop where, on each pass of the loop, we attempt
 * to pull one packet's worth of data out of fifo_from_user_ and send it via
 * udp_socket_, and then attempt to pull one UDP message from message_queue_
 * and write its contents to fifo_to_user_. The loop runs for at most loop_max
 * passes, or until neither operation has any data to work with.
 *
 * If any data was moved, move_data() returns true, otherwise it returns false.
 */
bool Connection::move_data(unsigned int loop_max)
{
  bool no_data_at_all = true; // used to record if we moved any data at all
                              // for the return value
  bool no_more_data = false; // used to record if a pass of the loop moved
                             // any data
  std::vector<unsigned char> fifo_data, msg_data;
  for(unsigned int i=0; (i<loop_max) and (!no_more_data); i++){
    no_more_data = true;

    /* attempt to pull one packet's worth of data out of fifo_from_user, and if
     * there is some data available, we prepend the peer's host id and the channel
     * id and then send it via udp_socket_
     */
    fifo_data = fifo_from_user_.read(max_packet_size_-(host_id_size+channel_id_size));
    if(fifo_data.size() > 0){
      no_more_data = false;
      no_data_at_all = false;
      msg_data = std::vector<unsigned char>(self_id_.begin(),self_id_.end());
      msg_data.insert(msg_data.end(),channel_id_.begin(),channel_id_.end());
      msg_data.insert(msg_data.end(),fifo_data.begin(),fifo_data.end());
      udp_socket_->send(msg_data,peer_ip_addr_,peer_port_);
    }

    /* attempt to pull a udp message off message_queue_... */
    ReceivedUDPMessage udp_message{false,{},"",0};
    {
      const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
      if(!message_queue_.empty()){
	udp_message = message_queue_.front();
	message_queue_.pop_front();
      }
    }

    /* ...and if there was a message waiting, write its data to fifo_to_user_ */
    if(udp_message.valid){
      no_more_data = false;
      no_data_at_all = false;
      auto it_begin = udp_message.data.begin()+(host_id_size+channel_id_size);
      auto it_end = udp_message.data.end();
      fifo_to_user_.write(std::vector<unsigned char>(it_begin,it_end));
    }

  }

  return no_data_at_all;
}


/* Connection::add_message(const ReceivedUDPMessage&) adds a UDP
 * message to the incoming message queue, message_queue_ (by copying)
 */
void Connection::add_message(const ReceivedUDPMessage& msg)
{
  const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
  message_queue_.push_back(msg);
}


/* Connection::add_message(const ReceivedUDPMessage&&) adds a UDP
 * message to the incoming message queue, message_queue_ (by moving)
 */
void Connection::add_message(ReceivedUDPMessage&& msg)
{
  const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
  message_queue_.push_back(std::move(msg));
}


/* Connection::from_user_fifo_fd() returns the file descriptor for the
 * Connection's FromUserFifo
 */
int Connection::from_user_fifo_fd()
{ return fifo_from_user_.file_descriptor(); }

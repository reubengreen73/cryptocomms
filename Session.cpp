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
		 std::uint16_t self_port,
		 unsigned int default_max_packet_size,
		 const std::vector<PeerConfig>& peer_configs,
		 const std::string& segnum_file_path):
  self_id_(self_id),
  default_max_packet_size_(default_max_packet_size),
  udp_socket_(std::make_shared<UDPSocket>(self_ip_addr, static_cast<in_port_t>(self_port))),
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


/* Session::start() starts the session by creating the two worker threads */
void Session::start()
{
  stopping_ = false;
  connections_thread = std::thread(&Session::connections_thread_func,this);
  socket_thread = std::thread(&Session::socket_thread_func,this);
  return;
}


/* Session::stop() shuts down a session by signalling to the worker threads
 * that they should stop
 */
void Session::stop()
{
  stopping_ = true;

  /* The socket thread blocks in receive calls on the socket, so we send
   * some empty packets to allow it to wake up and see that stopping_ is
   * true. This is a hack, but the code here is temporary.
   */
  for(int i=0; i<10; i++){
    udp_socket_->send({},udp_socket_->bound_addr(),udp_socket_->bound_port());
  }

  connections_thread.join();
  socket_thread.join();
  return;
}


/* Session::connections_thread_func() is the worker function for the thread which
 * runs the move_data() function of the Connection objects. This function is
 * temporary code which just does a very simple round robin through the Connections,
 * and sleeps the thread for a short period if there is nothing to do. This is, of course,
 * not how the full implementation will work.
 */
void Session::connections_thread_func()
{
  /* sleep_interval represents how many milliseconds the thread should sleep for
   * if it finds that none of the Connections have any data to move. sleep_interval
   * is doubled each consecutive time that the thread sleeps, and reset to 1 whenever
   * any data is moved.
   */
  unsigned int sleep_interval = 1;

  while(!stopping_){
    bool have_data = false; // records if any Connection moved some data

    std::for_each(connections_.begin(),connections_.end(),
		 [&](auto& x){
		   // we allow 100 iterations of the Connection's move_data loop
		   // before returning
		   bool data_here = x.second->move_data(100);
		   have_data = have_data && data_here;
		 }
		 );

    if(!have_data){
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_interval));
      sleep_interval = std::min(2*sleep_interval,64u);
    }
    else{
      sleep_interval = 1;
    }
  }

  return;
}


/* Session::socket_thread_func() is the worker function for the socket thread.
 * It simply pulls datagrams out of the UDPSocket and puts each one on the message
 * queue for the appropriate Connnection. This function is temporary code which
 * will be replaced in the final implementation.
 */
void Session::socket_thread_func()
{
  while(!stopping_){
    // udp_socket_->receive() blocks until a message arrives
    ReceivedUDPMessage msg = udp_socket_->receive();
    if(!msg.valid){
      continue;
    }

    /* The first few bytes of the message show which Connection it is for. Based on
     * this identifier, we pass it to the correct Connection's incoming message queue.
     */
    connection_id_type msg_prefix;
    if(msg.data.size() < msg_prefix.size()){
      continue;
    }
    std::copy(msg.data.begin(),msg.data.begin()+msg_prefix.size(),msg_prefix.begin());
    std::unique_ptr<Connection>& conn = connections_.at(msg_prefix);
    conn->add_message(msg);
  }

  return;
}

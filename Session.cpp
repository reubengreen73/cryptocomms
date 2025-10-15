#include "Session.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace
{
  /* constants used in calculating the number of loop passes a connection worker thread
     can dwell on a single Connection for */
  constexpr int dwell_max = 50;
  constexpr int dwell_min = 5;

  /* simple struct for storing the file descriptors for the two ends of a pipe */
  struct pipe_ends
  {
    int read_fd;
    int write_fd;
  };


  /* write_char_to_pipe() writes a char to a file descriptor representing a pipe,
   * retrying until an unrecoverable error occurs or success. The file descriptor
   * fd should be open for *blocking* write, as the function will throw an error
   * if there is a EAGAIN error.
   */
  void write_char_to_pipe(int fd, char data)
  {
    ssize_t ret = 0;
    while(ret < 1){
      ret = write(fd,&data,1);
      if(ret == -1){
        // retry if the write call was interrupted, else throw an error
        if(errno != EINTR){
          throw std::runtime_error("Session: could not write to internal pipe");}
      }
    }
  }


  /* make_internal_pipe() creates a pipe with the read end set for non-blocking reads.
   * These pipes are used internally by Session to wake threads which are blocked in a
   * call to poll() (the read end of the pipe is included amongst the file descriptors
   * passed to poll() ).
   */
  pipe_ends make_internal_pipe()
  {
    int pipe_fds[2];
    if(pipe(pipe_fds) == -1){
      throw std::runtime_error("Session: could not create internal pipe");}

    if(fcntl(pipe_fds[0],F_SETFL,O_NONBLOCK) == -1){
      throw std::runtime_error("Session: could not set internal pipe as non-blocking");}

    pipe_ends pipes;
    pipes.read_fd = pipe_fds[0];
    pipes.write_fd = pipe_fds[1];
    return pipes;
  }


  /* run_poll() calls poll() on the supplied list of file descriptors, retrying until
   * an event occurs or there is an unrecoverable error.
   */
  void run_poll(pollfd* poll_fds, int num_poll_fds)
  {
    int ret = 0;
    while(ret == 0){
      ret = poll(poll_fds,num_poll_fds,-1); // -1 means no timeout
      if(ret == -1){
        if((errno == EINTR) or (errno == EAGAIN)){ // recoverable error, try again
          ret = 0;}
        else{
          throw std::runtime_error("Session: poll() reported an error");}
      }
    }
  }

}


Session::Session(const host_id_type& self_id,
                 const std::string& self_ip_addr,
                 in_port_t self_port,
                 unsigned int default_max_packet_size,
                 const std::vector<PeerConfig>& peer_configs,
                 const std::string& segnum_file_path,
                 unsigned int num_connection_workers):
  self_id_(self_id),
  default_max_packet_size_(default_max_packet_size),
  udp_socket_(std::make_shared<UDPSocket>(self_ip_addr,self_port)),
  segnumgen_(std::make_shared<SegmentNumGenerator>(segnum_file_path,2*peer_configs.size())),
  connection_dwell_loops_(dwell_max),
  stopping_(false),
  active_(true)
{
  /* initialize the pipe used wake the thread that monitors the fifos of the Connections */
  pipe_ends pipes = make_internal_pipe();
  monitor_wake_read_fd_ = pipes.read_fd;
  monitor_wake_write_fd_ = pipes.write_fd;

  /* initialize the pipes used to signal shutdown to the udp thread */
  pipes = make_internal_pipe();
  udp_thread_stop_read_fd_ = pipes.read_fd;
  udp_thread_stop_write_fd_ = pipes.write_fd;

  /* create the Connections */
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

      connection_and_bool_type conn_and_bool{
        std::make_unique<Connection>(self_id,
                                     peer_config.name,
                                     peer_config.id,
                                     ch_spec.first,
                                     ch_spec.second,
                                     peer_config.key,
                                     peer_config.ip_addr,
                                     peer_config.port,
                                     max_packet_size,
                                     udp_socket_,
                                     segnumgen_),
        false
      };

      // add the new Connection's file descriptor to be monitored
      monitor_fds_.insert({conn_and_bool.first->from_user_fifo_fd(),full_id});

      connections_.insert({full_id,std::move(conn_and_bool)});
    }
  }

  /* spawn all of the threads */
  for(unsigned int i=0; i<num_connection_workers; i++){
    connection_worker_threads_.push_back(std::thread(&Session::connection_worker_thread_func,this));
  }
  udp_socket_thread_ = std::thread(&Session::udp_socket_thread_func,this);
  fifo_monitor_thread_ = std::thread(&Session::fifo_monitor_thread_func,this);

}


Session::~Session()
{
  /* ensure that all the threads are shut down */
  if(active_)
    stop();

  /* close all the internal pipes */
  close(monitor_wake_read_fd_);
  close(monitor_wake_write_fd_);
  close(udp_thread_stop_read_fd_);
  close(udp_thread_stop_write_fd_);
}


/* Session::stop() orchestrates the shutting down of all the threads, and waits
 * for them all to exit.
 */
void Session::stop()
{
  /* set stopping_ to true to signal the connection worker threads to shut down, and
     then wake up any that are waiting on session_condvar_ so that they see this
     signal */
  {
    const std::lock_guard<std::mutex> session_lock_guard(session_lock_);
    stopping_ = true;
  }
  session_condvar_.notify_all();

  /* We must tell both the thread which monitors the Connection fifos and the
     thread which handles incoming UDP messages to exit. For both this is done
     via a message written to an internal pipe, which wakes the threads from any
     poll() call they might be in, and lets them know it is time to stop. */
  wake_monitor(true); // for the fifo monitoring thread (the "true" value sends
                      // an exit signal)
  write_char_to_pipe(udp_thread_stop_write_fd_,0); // for the socket monitoring thread

  /* wait for all threads to stop */
  for(auto& t : connection_worker_threads_){
    t.join();}
  fifo_monitor_thread_.join();
  udp_socket_thread_.join();

  /* record that the Session has shut down */
  active_ = false;
}


/* Session::udp_socket_thread() is the worker function for the thread which monitors the
 * socket for incoming udp messages and passes them to the correct Connection.
 */
void Session::udp_socket_thread_func()
{
  /* prepare pollfd structs to represent the upd socket and the udp_thread_stop_read_fd_
   * file descriptor, which is one end of the pipe that will be used to signal this
   * thread to shut down.
   */
  pollfd poll_fds[2];
  poll_fds[0].fd = udp_socket_->file_descriptor();
  poll_fds[0].events = POLLIN;
  poll_fds[1].fd = udp_thread_stop_read_fd_;
  poll_fds[1].events = POLLIN;

  /* main thread loop */
  while(true){

    /* use poll() to wait until something has happened on the socket or the internal pipe */
    run_poll(poll_fds,2);

    /* any write to the internal pipe is a signal that we should exit */
    if(poll_fds[1].revents & POLLIN){
      return;}

    /* if there is no data on the socket, try again */
    if(not poll_fds[0].revents & POLLIN){
      continue;}

    /* pull a message out of the socket */
    ReceivedUDPMessage msg = udp_socket_->receive();
    if(!msg.valid){
      continue;
    }

    /* ignore messages which are too short to be valid */
    if(msg.data.size() < connection_id_size){
      continue;
    }

    /* look up which Connection this message is for, based on the bytes at the start
       of the message */
    connection_id_type conn_id;
    std::copy(msg.data.begin(),msg.data.begin()+connection_id_size,conn_id.begin());
    auto it = connections_.find(conn_id);
    if(it == connections_.end()){
      continue; // ignore messages which don't have a valid Connection id
    }

    /* Add  the message to the Connection's message queue. Note that "it" is an iterator
       whose value type is a std::pair with first-type connection_id_type and second-type
       another std::pair, with first-type unique_ptr to a Connection and second-type bool */
    (*it).second.first->add_message(msg);

    /* add the Connection to the queue for a connection worker thread */
    { // new block to limit scope of session_lock_guard
      const std::lock_guard<std::mutex> session_lock_guard(session_lock_);
      enqueue_connection(conn_id);
    }
    session_condvar_.notify_one();
  }

}


/* Session::fifo_monitor_thread_func() is the worker function for the thread which monitors
 * the fifos of the Connections. More specifically, the thread watches the "from user" fifos
 * of the Connections for any new data, and enqueues any Connection with new data for time
 * on a connection worker thread.
 */
void Session::fifo_monitor_thread_func()
{
  /* poll_fds stores details of file descriptors for use in a call to poll()
     poll_fds[0] will always hold details of monitor_wake_read_fd_, the read
     end of a pipe which is used to wake the thread from a poll() call and to
     pass it a message that it is time to exit */
  std::vector<pollfd> poll_fds(connections_.size()+1);
  poll_fds[0].fd = monitor_wake_read_fd_;
  poll_fds[0].events = POLLIN;
  poll_fds[0].revents = 0;
  unsigned int num_poll_fds = 1;

  /* main thread loop */
  while(true){

    /* num_to_notify will be incremented each time we enqueue a Connection.
       After we release session_lock_, we shall notify this many times on
       session_condvar_ */
    int num_to_notify = 0;

    {/* new block to limit the scope of session_lock_guard */
      const std::lock_guard<std::mutex> session_lock_guard(session_lock_);

      /* enqueue any connections whose fifos have data to read */
      for(unsigned int i=1; i<num_poll_fds; i++){
        //NB i starts at 1 to ignore monitor_wake_read_fd_
        if(poll_fds[i].revents & POLLIN){
          auto it = monitor_fds_.find(poll_fds[i].fd);
          enqueue_connection((*it).second);
          num_to_notify++;
        }
      }

      /* Ensure that there is enough space in poll_fds for all of the file descriptors
         we might want to use. Note that this resize will never be called in the current
         code, as the list of Connections does not change during the Session's lifetime.
         However, this may change in the future, so we may as well cater for this now. */
      if(poll_fds.size() < monitor_fds_.size()+1){
        poll_fds.resize(monitor_fds_.size()+1);}

      /* build the list of pollfd structs for the call to poll() */
      num_poll_fds = 1;
      for(auto const& it : monitor_fds_){
        poll_fds[num_poll_fds].fd = it.first;
        poll_fds[num_poll_fds].events = POLLIN;
        num_poll_fds += 1;
      }
    }

    /* notify session_condvar_ once for each Connection we enqueued */
    for(int i=0; i<num_to_notify; i++){
      session_condvar_.notify_one();}

    /* call poll() on the list of pollfd structs */
    run_poll(poll_fds.data(),num_poll_fds);

    /* Clear out any data from monitor_wake_read_fd_, as it has now served
       its purpose by waking the thread from poll() and we don't want it to
       do this again. Also exit if requested. */
    while(true){ // we repeatedly read from monitor_wake_read_fd_ until it is empty
      char discard_buffer[128];
      ssize_t ret = read(monitor_wake_read_fd_,discard_buffer,128);
      if(ret == -1){
        if(errno == EAGAIN){ // pipe is empty, we are done
          break;}
        if(errno != EINTR){
          throw std::runtime_error("Session: error reading from internal pipe ");}
        continue;
      }

      /* check through the bytes we read for a message that we should exit */
      for(int i=0; i<ret; i++){
        if(discard_buffer[i] == 1){ // a value of 1 means that shutdown has been signalled
          return;}
      }
    }

  }

}


/* Session::connection_worker_thread_func() is the worker function for the threads which move
 * data through the Connections.
 */
void Session::connection_worker_thread_func()
{
  /* main thread loop */
  while(true){
    std::unique_lock<std::mutex> session_unique_lock(session_lock_);

    /* wait for a Connection to process */
    while(connection_queue_.empty() and !stopping_){
      session_condvar_.wait(session_unique_lock);}

    if(stopping_){
      return;}

    /* take the first Connection id from the queue... */
    connection_id_type conn_id = connection_queue_[0];
    connection_queue_.pop_front();

    /* ... and find the associated Connection. We take a reference to this Connection
       to avoid using complicated expressions to access it. The Connection objects all
       live until after all threads in the Session have exited, so this reference cannot
       become dangling. */
    auto it = connections_.find(conn_id);
    (*it).second.second = true; // mark the Connection as "being worked on"
    Connection& conn = *((*it).second.first);

    /* Update the number of loop passes which connection worker threads dwell on a single
       Connection. The algorithm is simple: if the total number of Connections currently being
       worked on and in the queue is greater than the number of worker threads, reduce the
       dwell time, and otherwise increase it, always staying in the range [dwell_min,dwell_max]. */
    unsigned int num_active_connections =
      static_cast<unsigned int>(connections_.size()-monitor_fds_.size());
    if( (connection_dwell_loops_ > dwell_min) and
        (num_active_connections > connection_worker_threads_.size()) ){
      connection_dwell_loops_ -= 1;}
    else if( connection_dwell_loops_ < dwell_max){
      connection_dwell_loops_ += 1;}
    int my_connection_dwell_loops = connection_dwell_loops_;

    /* run the Connection's move_data() method while *not* holding session_lock_ */
    session_unique_lock.unlock();
    conn.move_data(my_connection_dwell_loops);
    session_unique_lock.lock();

    /* If there is more data to move on this Connection, enqueue it. Otherwise, add it
       for monitoring by the fifo monitoring thread. */
    (*it).second.second = false; // mark the Connection as "not being worked on"
    if(conn.is_data()){
      enqueue_connection(conn_id);}
    else{
      monitor_fds_.insert({conn.from_user_fifo_fd(),conn_id});
      wake_monitor(false); // wake the fifo monitoring thread so that it will see that it
                           // needs to monitor this fifo
    }

    session_unique_lock.unlock();
  }
}


/* Session::wake_monitor() writes to the internal fifo to break fifo_monitor_thread_func()
 * out of its poll() call to update the list of file descriptors it is monitoring. The actual
 * data written is a single char, which normally has value 0, but has value 1 if we wish the
 * thread to exit.
 */
void Session::wake_monitor(bool stop_thread)
{
  char data = stop_thread ? 1:0;
  write_char_to_pipe(monitor_wake_write_fd_,data);
}


/* Session::enqueue_connection() adds a Connection's id to the queue for time on a
 * connection worker thread if it is not already in the queue, and removes the file
 * descriptor for the Connection's input fifo from the list of fds to be monitored.
 *
 * This method should *only* be called if you hold the Session's session_lock_
 */
void Session::enqueue_connection(const connection_id_type& conn_id)
{
  /* find the Connection for this id */
  auto it = connections_.find(conn_id);
  if(it == connections_.end()){
    throw std::runtime_error("Session: unknown connection id passed to be enqueued");}

  /* if the Connection indexed by conn_id is already "being worked on", we have nothing
   to do */
  if((*it).second.second){
    return;}

  /* if conn_id is already in the queue, we have nothing to do */
  if(std::find(connection_queue_.begin(),connection_queue_.end(),conn_id)
     != connection_queue_.end()){
    return;}

  /* put the conn_id in the queue */
  connection_queue_.push_back(conn_id);

  /* remove the Connection's fifo fd from monitor_fds_ */
  int fifo_fd = (*it).second.first->from_user_fifo_fd();
  monitor_fds_.erase(fifo_fd);
}

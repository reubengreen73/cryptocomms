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
#include <map>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <thread>
#include <netinet/in.h> // for in_port_t
#include <utility>

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
          const std::string& segnum_file_path,
          unsigned int num_connection_workers = 5);
  ~Session();
  void stop();

private:
  constexpr static int connection_id_size =  host_id_size+channel_id_size;
  typedef std::array<unsigned char,connection_id_size> connection_id_type;
  typedef std::pair<std::unique_ptr<Connection>,bool> connection_and_bool_type;

  host_id_type self_id_;
  unsigned int default_max_packet_size_;
  std::shared_ptr<UDPSocket> udp_socket_;
  std::shared_ptr<SegmentNumGenerator> segnumgen_;
  std::map<connection_id_type,connection_and_bool_type> connections_;
  std::map<int,connection_id_type> monitor_fds_;
  std::mutex session_lock_;
  std::condition_variable session_condvar_;
  std::deque<connection_id_type> connection_queue_;
  unsigned int connection_dwell_loops_;
  int monitor_wake_read_fd_;
  int monitor_wake_write_fd_;
  int udp_thread_stop_read_fd_;
  int udp_thread_stop_write_fd_;
  bool stopping_;
  bool active_;

  std::thread udp_socket_thread_;
  std::thread fifo_monitor_thread_;
  std::vector<std::thread> connection_worker_threads_;

  void udp_socket_thread_func();
  void fifo_monitor_thread_func();
  void connection_worker_thread_func();

  void wake_monitor(bool stop_thread);
  void enqueue_connection(const connection_id_type& conn_id);
};

#endif

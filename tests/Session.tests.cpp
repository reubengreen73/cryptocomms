#include "testsys.h"
#include "../Session.h"
#include "../IDTypes.h"
#include "../PeerConfig.h"
#include "../SecretKey.h"

#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <netinet/in.h> // for in_port_t

/* this test creates two sessions and checks that they can communicate correctly */
TESTFUNC(Session_two_sessions_communicating)
{
  /* parameters for the two sessions */
  std::string host_A_name("host A");
  std::string host_B_name("host B");
  host_id_type host_A_id{0x01,0xab,0x00,0x53};
  host_id_type host_B_id{0x21,0x03,0x82,0x0f};
  channel_id_type channel_id{0xa5,0x07};
  std::string host_A_fifo_base_name("host_A_fifo");
  std::string host_B_fifo_base_name("host_B_fifo");
  std::string ip_addr("127.0.0.1");
  in_port_t host_A_port = 12991;
  in_port_t host_B_port = 12992;
  int max_packet_size = 1000;
  std::string segnum_file_name = "segnumfile";
  SecretKey key("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

  /* each Session needs a PeerConfig containing details of the other session */
  PeerConfig host_A_peer_config{host_A_name,
                                host_A_id,
                                key,
                                {channel_spec{channel_id,host_B_fifo_base_name}},
                                ip_addr,
                                host_A_port,
                                max_packet_size};
  PeerConfig host_B_peer_config{host_B_name,
                                host_B_id,
                                key,
                                {channel_spec{channel_id,host_A_fifo_base_name}},
                                ip_addr,
                                host_B_port,
                                max_packet_size};

  Session host_A_session(host_A_id,
                         ip_addr,host_A_port,
                         max_packet_size,
                         {host_B_peer_config},
                         segnum_file_name);
  Session host_B_session(host_B_id,
                         ip_addr,host_B_port,
                         max_packet_size,
                         {host_A_peer_config},
                         segnum_file_name);
  host_A_session.start();
  host_B_session.start();

  /* open the fifos for the sessions
   * we shall write into session A's "_OUTWARD" fifo, and read from
   * session B's "_INWARD" fifo (note that the hard-coded "_OUTWARD"
   * and "_INWARD" here need to be kept in sync with the vaules in
   * Connection.cpp -- this is not gread and should really be changed
   * somehow)
   */
  std::string write_fifo_name(host_A_fifo_base_name+"_OUTWARD");
  std::string read_fifo_name(host_B_fifo_base_name+"_INWARD");
  int write_fifo_fd = open(write_fifo_name.c_str(), O_WRONLY);
  TESTASSERT( write_fifo_fd != -1 );
  int read_fifo_fd = open(read_fifo_name.c_str(), O_RDONLY);
  TESTASSERT( read_fifo_fd != -1 );

  /* write the test data to the fifo */
  std::vector<unsigned char> write_buff{0x01,0xb4,0xa6,0x00,0xff,0xf5,0x72,0x50,0x74,0x03};
  unsigned int ret = write(write_fifo_fd,write_buff.data(),write_buff.size());
  TESTASSERT(ret == write_buff.size());

  /* read the test data from the fifo */
  std::vector<unsigned char> read_buff(write_buff.size());
  ret = read(read_fifo_fd,read_buff.data(),write_buff.size());
  TESTASSERT(ret == write_buff.size());

  TESTASSERT(write_buff == read_buff);

  host_A_session.stop();
  host_B_session.stop();
}

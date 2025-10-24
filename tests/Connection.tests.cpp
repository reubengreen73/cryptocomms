#include "testsys.h"

#include "../IDTypes.h"
#include "../UDPSocket.h"
#include "../FifoIO.h"
#include "../SecretKey.h"
#include "../SegmentNumGenerator.h"
#include "../ReceivedUDPMessage.h"
#include "../Connection.h"

#include <stdexcept>
#include <string>
#include <array>
#include <memory>
#include <fstream>

#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>


/* struct to hold what is returned by create_connection() */
struct ConnectionAndFDs
{
  std::unique_ptr<Connection> conn;
  int from_user_fifo_fd;
  int to_user_fifo_fd;
  int socket_fd;
  in_port_t socket_fd_bound_port;
  ConnectionAndFDs():
    from_user_fifo_fd(-1),
    to_user_fifo_fd(-1),
    socket_fd(-1) {}
  void close_all()
  {
    if(from_user_fifo_fd != -1){
      close(from_user_fifo_fd);}
    if(to_user_fifo_fd != -1){
      close(to_user_fifo_fd);}
    if(socket_fd != -1){
      close(socket_fd);}
  }
};


/* create_connection() is a convenience function which prepares a Connection
 * for use in testing.
 */
ConnectionAndFDs create_connection()
{
  ConnectionAndFDs conn_fds;

  /* 1 - prepare a socket to act as the Connection's "remote peer" */

  /* create a socket */
  conn_fds.socket_fd = socket(AF_INET,SOCK_DGRAM,0);
  if(conn_fds.socket_fd == -1){
    throw std::runtime_error("Test Error: could not create socket for testing");
  }

  /* prepare an address struct to bind the socket */
  sockaddr_in bind_addr;
  memset(&bind_addr,0,sizeof(sockaddr_in));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if(bind_addr.sin_addr.s_addr == (in_addr_t)(-1)){
    close(conn_fds.socket_fd);
    throw std::runtime_error("Test Error: bad ip address for binding");
  }
  bind_addr.sin_port = htons(0);

  /* bind the socket */
  if(bind(conn_fds.socket_fd,(sockaddr*)&bind_addr,sizeof(bind_addr)) == -1){
    close(conn_fds.socket_fd);
    throw std::runtime_error("Test Error: could not bind");
  }

  /* find the socket's port */
  socklen_t socklen = sizeof(bind_addr);
  if(getsockname(conn_fds.socket_fd,(sockaddr*)&bind_addr,&socklen) == -1){
    close(conn_fds.socket_fd);
    throw std::runtime_error("Test Error: could not get socket information after bind");
  }
  conn_fds.socket_fd_bound_port = ntohs(bind_addr.sin_port);

  /* 2 - create the segment number file */
  std::string segnumfile_name("segnumfile");
  std::string segnum_string("0");
  std::ofstream segnum_file(segnumfile_name,std::ios::out);
  segnum_file << segnum_string;
  segnum_file.close();

  /* 3 - make the Connection */

  /* parameters for the Connection */
  host_id_type self_id = {0x01,0x4a,0x72,0xb1};
  std::string peer_name = "another host";
  host_id_type peer_id = {0xa3,0x90,0x1c,0x00};
  channel_id_type channel_id = {0x66,0x10};
  std::string fifo_base_path = "fifo_base_name";
  SecretKey key("00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00");
  std::string peer_ip_addr = "127.0.0.1";
  unsigned int max_packet_size = 1000;
  std::shared_ptr<UDPSocket> udp_socket =
    std::make_shared<UDPSocket>("127.0.0.1",0);
  std::shared_ptr<SegmentNumGenerator> segnumgen =
    std::make_shared<SegmentNumGenerator>(segnumfile_name,1);

  conn_fds.conn = std::make_unique<Connection>(self_id,
                                               peer_name,
                                               peer_id,
                                               channel_id,
                                               fifo_base_path,
                                               key,
                                               peer_ip_addr,
                                               conn_fds.socket_fd_bound_port,
                                               max_packet_size,
                                               udp_socket,
                                               segnumgen);

  /* 4 - open the Connection's fifos
   * Note that the literal strings "_OUTWARD" and "_INWARD" need to be kept in sync
   * with the values in Connection.cpp (this is not good and should probably be fixed
   * somehow).
   */
  std::string to_user_fifo_name(fifo_base_path+"_INWARD");
  std::string from_user_fifo_name(fifo_base_path+"_OUTWARD");
  conn_fds.to_user_fifo_fd = open(to_user_fifo_name.c_str(), O_RDONLY);
  TESTASSERT( conn_fds.to_user_fifo_fd != -1 );
  conn_fds.from_user_fifo_fd = open(from_user_fifo_name.c_str(), O_WRONLY);
  TESTASSERT( conn_fds.from_user_fifo_fd != -1 );

  return conn_fds;
}


/* Test that moving data through a Connection works correctly. This test function is very
 * big, but that is mostly due to all the setup which is needed.
 */
TESTFUNC(Connection_move_data)
{
  ConnectionAndFDs conn_fds = create_connection();

  /* Test the functionality for moving data through the Connection from
   * udp message queue to fifo.
   *
   * We put a message in the queue and check that it comes out of the fifo.
   */

  // put the message in the queue
  std::vector<unsigned char> msg_data{0x01,0x4a,0x72,0xb1,0x66,0x10,0xaa,0x11,
                                      0x01,0x00,0x1b,0x73,0x3c,0x20,0x4f,0xff};
  ReceivedUDPMessage msg{true,msg_data,"127.0.0.1",conn_fds.socket_fd_bound_port};
  conn_fds.conn->add_message(msg);

  // move the data
  conn_fds.conn->move_data(10);

  // check that the correct data comes out of the fifo
  std::array<unsigned char,10> buff;
  ssize_t ret = read(conn_fds.to_user_fifo_fd,buff.data(),10);
  TESTASSERT(ret == 10);
  TESTASSERT((buff == \
              std::array<unsigned char,10>{0xaa,0x11,0x01,0x00,0x1b,0x73,0x3c,0x20,0x4f,0xff}));

  /* Test the functionality for moving data through the Connection from the fifo
   * to the udp socket.
   *
   * We put a message in the fifo and check that it gets sent out of the UDP port.
   */

  // put data into the fifo
  buff = {0xbb,0x12,0x01,0x00,0x07,0x75,0xaa,0xd2,0x5f,0x89};
  ret = write(conn_fds.from_user_fifo_fd,buff.data(),10);
  TESTASSERT(ret == 10);

  // move the data
  conn_fds.conn->move_data(10);

  // check that the correct data arrives at the "other host" socket
  std::array<unsigned char,16> buff2;
  sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);
  ret = recvfrom(conn_fds.socket_fd,buff2.data(),16,0,(sockaddr*)&source_addr,&addr_len);
  TESTASSERT(ret == 16);
  TESTASSERT((buff2 == std::array<unsigned char,16>                     \
              {0x01,0x4a,0x72,0xb1,0x66,0x10,0xbb,0x12,0x01,0x00,0x07,0x75,0xaa,0xd2,0x5f,0x89}));


  conn_fds.close_all();
}


/* test the is_data() function of Connection */
/* TEMPORARILY DISABLED due to intended change in behaviour of Connection::is_data(), this test
   will be reactivated when full encryption logic is added
T*STFUNC(Connection_is_data) //we change the macro name so the gen_tester.py script won't find it
{
  {
    ConnectionAndFDs conn_fds = create_connection();

    TESTASSERT( !conn_fds.conn->is_data() );

    // put a message in the queue
    std::vector<unsigned char> msg_data{0x01,0x4a,0x72,0xb1,0x66,0x10,0xaa,0x11,
                                        0x01,0x00,0x1b,0x73,0x3c,0x20,0x4f,0xff};
    ReceivedUDPMessage msg{true,msg_data,"127.0.0.1",conn_fds.socket_fd_bound_port};
    conn_fds.conn->add_message(msg);

    TESTASSERT( conn_fds.conn->is_data() );

    // move the data
    conn_fds.conn->move_data(10);

    TESTASSERT( !conn_fds.conn->is_data() );

    conn_fds.close_all();
  }

  {
    ConnectionAndFDs conn_fds = create_connection();

    TESTASSERT( !conn_fds.conn->is_data() );

    // put data into the fifo
    std::array<unsigned char,10> buff = {0xbb,0x12,0x01,0x00,0x07,0x75,0xaa,0xd2,0x5f,0x89};
    ssize_t ret = write(conn_fds.from_user_fifo_fd,buff.data(),10);
    TESTASSERT(ret == 10);

    TESTASSERT( conn_fds.conn->is_data() );

    // move the data
    conn_fds.conn->move_data(10);

    TESTASSERT( !conn_fds.conn->is_data() );

    conn_fds.close_all();
  }
}
*/

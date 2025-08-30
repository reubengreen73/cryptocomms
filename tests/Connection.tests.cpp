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

#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

/* Test that moving data through a Connection works correctly. This test function is very
 * big, but that is mostly due to all the setup which is needed.
 */
TESTFUNC(Connection_move_data)
{
  /* 1 - Perform setup for the tests
   *
   * The tests in this function need some pretty heavy setup - we need to create
   * a socket, bind it, create a Connection, and open the Connection's fifos
   */

  /* create a socket */
  int socket_fd = socket(AF_INET,SOCK_DGRAM,0);
  if(socket_fd == -1){
    throw std::runtime_error("Error: could not create socket for testing");
  }

  /* prepare an address struct to bind the socket */
  sockaddr_in bind_addr;
  memset(&bind_addr,0,sizeof(sockaddr_in));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if(bind_addr.sin_addr.s_addr == (in_addr_t)(-1)){
    close(socket_fd);
    throw std::runtime_error("Error: bad ip address for binding");
  }
  bind_addr.sin_port = htons(0);

  /* bind the socket */
  if(bind(socket_fd,(sockaddr*)&bind_addr,sizeof(bind_addr)) == -1){
    close(socket_fd);
    throw std::runtime_error("Error: could not bind");
  }

  /* find the socket's port */
  socklen_t socklen = sizeof(bind_addr);
  if(getsockname(socket_fd,(sockaddr*)&bind_addr,&socklen) == -1){
    close(socket_fd);
    throw std::runtime_error("Error: could not get socket information after bind");
  }
  in_port_t socket_fd_bound_port = ntohs(bind_addr.sin_port);

  /* make a Connection */
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
    std::make_shared<SegmentNumGenerator>("segnumfile",1);
  Connection conn(self_id,
		  peer_name,
		  peer_id,
		  channel_id,
		  fifo_base_path,
		  key,
		  peer_ip_addr,
		  socket_fd_bound_port,
		  max_packet_size,
		  udp_socket,
		  segnumgen);

  /* open the Connection's fifos
   * Note that the literal strings "_OUTWARD" and "_INWARD" need to be kept in sync
   * with the values in Connection.cpp (this is not good and should probably be fixed
   * somehow).
   */
  std::string read_fifo_name(fifo_base_path+"_INWARD");
  std::string write_fifo_name(fifo_base_path+"_OUTWARD");
  int read_fifo_fd = open(read_fifo_name.c_str(), O_RDONLY);
  TESTASSERT( read_fifo_fd != -1 );
  int write_fifo_fd = open(write_fifo_name.c_str(), O_WRONLY);
  TESTASSERT( write_fifo_fd != -1 );

  /* 2 - Test the functionality for moving data through the Connection from
   * udp message queue to fifo.
   *
   * We put a message in the queue and check that it comes out of the fifo.
   */

  // put the message in the queue
  std::vector<unsigned char> msg_data{0x01,0x4a,0x72,0xb1,0x66,0x10,0xaa,0x11,
				      0x01,0x00,0x1b,0x73,0x3c,0x20,0x4f,0xff};
  ReceivedUDPMessage msg{true,msg_data,"127.0.0.1",socket_fd_bound_port};
  conn.add_message(msg);

  // move the data
  conn.move_data(10);

  // check that the correct data comes out of the fifo
  std::array<unsigned char,10> buff;
  ssize_t ret = read(read_fifo_fd,buff.data(),10);
  TESTASSERT(ret == 10);
  TESTASSERT((buff == \
	      std::array<unsigned char,10>{0xaa,0x11,0x01,0x00,0x1b,0x73,0x3c,0x20,0x4f,0xff}));

  /* 3 -Test the functionality for moving data through the Connection from the fifo
   * to the udp socket.
   *
   * We put a message in the fifo and check that it gets sent out of the UDP port.
   */

  // put data into the fifo
  buff = {0xbb,0x12,0x01,0x00,0x07,0x75,0xaa,0xd2,0x5f,0x89};
  ret = write(write_fifo_fd,buff.data(),10);
  TESTASSERT(ret == 10);

  // move the data
  conn.move_data(10);

  // check that the correct data arrives at the "other host" socket
  std::array<unsigned char,16> buff2;
  sockaddr_in source_addr;
  socklen_t addr_len = sizeof(source_addr);
  ret = recvfrom(socket_fd,buff2.data(),16,0,(sockaddr*)&source_addr,&addr_len);
  TESTASSERT(ret == 16);
  TESTASSERT((buff2 == std::array<unsigned char,16>			\
	      {0x01,0x4a,0x72,0xb1,0x66,0x10,0xbb,0x12,0x01,0x00,0x07,0x75,0xaa,0xd2,0x5f,0x89}));


  close(read_fifo_fd);
  close(write_fifo_fd);
  close(socket_fd);
}

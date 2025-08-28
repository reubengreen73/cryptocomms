#include "testsys.h"
#include "../UDPSocket.h"

#include <string>
#include <vector>
#include <arpa/inet.h>


/* check that bad IP address strings give the correct error */
TESTFUNC(udpsocket_ctor_bad_ip_addr)
{
  std::string expected_err = "bad ip address for binding";
  TESTTHROW(UDPSocket("",0), expected_err);
  TESTTHROW(UDPSocket("blah",0), expected_err);
  TESTTHROW(UDPSocket("192.168.300.1",0), expected_err);
  TESTTHROW(UDPSocket(".168.1.1",0), expected_err);
}


/* check that basic sending and receiving works correctly */
TESTFUNC(udpsocket_send_receive)
{
  UDPSocket sock1{"127.0.0.1",0};
  UDPSocket sock2{"127.0.0.1",0};
  in_port_t sock2_port = sock2.bound_port();

  TESTASSERT( sock1.send({1,2,3,4,5},"127.0.0.1",sock2_port) );

  ReceivedUDPMessage udp_msg = sock2.receive();
  TESTASSERT( (udp_msg.data == std::vector<unsigned char>{1,2,3,4,5}) );
}


TESTFUNC(udpsocket_receive_sender_details)
{
  UDPSocket sock1{"127.0.0.1",0};
  UDPSocket sock2{"127.0.0.1",0};
  in_port_t sock1_port = sock1.bound_port();
  in_port_t sock2_port = sock2.bound_port();

  sock1.send({1,2,3,4,5},"127.0.0.1",sock2_port);

  ReceivedUDPMessage udp_msg = sock2.receive();

  TESTASSERT( udp_msg.source_port == sock1_port );

  in_addr_t addr1 = inet_addr("127.0.0.1");
  in_addr_t addr2 = inet_addr(udp_msg.source_addr.c_str());
  TESTASSERT( addr1 == addr2 );
}


/* check that use after move produces the correct errors */
TESTFUNC(udpsocket_use_after_move)
{
  UDPSocket sock1{"127.0.0.1",0};
  UDPSocket sock2(std::move(sock1));

  TESTTHROW( sock1.send({1,2,3,4,5},"127.0.0.1",5555), "send() after move" );
  TESTTHROW( ReceivedUDPMessage udp_msg = sock1.receive(), "receive() after move" );
}


/* check that UDPSocket reports ip address and port correctly */
TESTFUNC(udpsocket_ip_port_reporting)
{
  UDPSocket sock{"127.0.0.1",57821};
  TESTASSERT( sock.bound_port() == 57821 );

  /* we reduce both addresses to the format returned by inet_addr()
   * to avoid possible (?) different string representations of the same
   * address
   */
  in_addr_t addr1 = inet_addr("127.0.0.1");
  in_addr_t addr2 = inet_addr(sock.bound_addr().c_str());
  TESTASSERT( addr1 == addr2 );
}

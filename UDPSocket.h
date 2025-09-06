/* Class to wrap the functionality of a non-connected UDP socket.
 *
 * UDPSocket wraps a non-connected UDPSocket bound to a port number (passed to the
 * constructor). The send() and receive() functions handle all the details of UDP
 * sockets, and provide some level of error handling and retrying in the event of
 * an interrupted system call. However, these functions retain the fundamentally
 * unreliable nature of UDP.
 */

#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <netinet/in.h>
#include <string>
#include <vector>

#include "ReceivedUDPMessage.h"

class UDPSocket
{
public:
  UDPSocket(const std::string& ip_addr, in_port_t port);
  bool send(const std::vector<unsigned char>& msg, const std::string& dest_addr, in_port_t dest_port);
  ReceivedUDPMessage receive();
  const std::string& bound_addr();
  in_port_t bound_port();
  int file_descriptor();

  UDPSocket (UDPSocket&&);
  UDPSocket& operator= (UDPSocket&&);
  ~UDPSocket();

  /* We do not allow copying, as the socket_fd member is not shareable (due to the use of the MSG_PEEK
   * method in UDPSocket::receive()), and moreover there is no reason to have two UDPSockets sharing a
   * socket_fd. We only allow moves.
   */
  UDPSocket (const UDPSocket&) = delete;
  UDPSocket& operator= (const UDPSocket&) = delete;

private:
  int socket_fd_;
  std::string bound_addr_;
  in_port_t bound_port_; // stored in *host* byte order
  std::vector<unsigned char> recv_buff_;
};

#endif

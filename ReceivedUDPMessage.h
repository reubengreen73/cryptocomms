/* A simple struct to represent a UDP datagram.
 * If you #include UDPSocket.h, there is no need to #include
 * this file separately
 */

#ifndef RECEIVEDUDPMESSAGE_H
#define RECEIVEDUDPMESSAGE_H

#include <netinet/in.h>
#include <string>
#include <vector>

/* The member "valid" of ReceivedUDPMessage records whether the struct
 * represents a real message or not. This allows a function which reads from
 * the network to return a ReceivedUDPMessage while cleanly communicating to
 * the caller that there was no message.
 */
struct ReceivedUDPMessage
{
  bool valid;
  std::vector<unsigned char> data;
  std::string source_addr;
  in_port_t source_port;
};

#endif

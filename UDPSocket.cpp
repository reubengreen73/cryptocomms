#include "UDPSocket.h"

/* Note that we use the C POSIX interface exclusively in this file for functionality from
 * the C standard library. In particular, we use the POSIX compatible C headers like
 * string.h rather than cstring, and we use names in the global namespace rather than std::
 * It seems better to be consistent with POSIX rather than mixing and matching the two
 * standards.
 */
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <stdexcept>

/* UDPSocket::UDPSocket() makes a UDP socket for both sending and receiving
 * bound to the specified ip address and port.
 *
 * Note that this constructor is not thread safe, see comment about inet_ntoa()
 * in the function body.
 */
UDPSocket::UDPSocket(const std::string& ip_addr, in_port_t port)
  : recv_buff_(16)
{
  /* create the socket */
  socket_fd_ = socket(AF_INET,SOCK_DGRAM,0);
  if(socket_fd_ == -1){
    throw std::runtime_error("UDPSocket: could not create socket");
  }

  /* prepare address struct for binding */
  sockaddr_in bind_addr;
  memset(&bind_addr,0,sizeof(sockaddr_in));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = inet_addr(ip_addr.c_str());
  if(bind_addr.sin_addr.s_addr == (in_addr_t)(-1)){
    // note that POSIX states that inet_addr retuns (in_addr_t)(-1) on error
    close(socket_fd_);
    throw std::runtime_error("UDPSocket: bad ip address for binding");
  }
  bind_addr.sin_port = htons(port);

  /* bind the socket */
  if(bind(socket_fd_,(sockaddr *)&bind_addr, sizeof(bind_addr)) == -1){
    close(socket_fd_);
    throw std::runtime_error("UDPSocket: could not bind");
  }

  /* get information about the bound socket */
  socklen_t socklen = sizeof(bind_addr);
  if(getsockname(socket_fd_, (sockaddr *)&bind_addr, &socklen) == -1){
    close(socket_fd_);
    throw std::runtime_error("UDPSocket: could not get socket information after bind");
  }

  /* inet_ntoa() is not thread-safe, but cryptocomms only creates one UDPSocket and does
   * not use inet_ntoa elsewhere, so this is no problem
   */
  bound_addr_ = std::string(inet_ntoa(bind_addr.sin_addr));
  bound_port_ = ntohs(bind_addr.sin_port);
}


UDPSocket::~UDPSocket()
{
  if(socket_fd_ != -1){
    close(socket_fd_);
  }
}


UDPSocket::UDPSocket (UDPSocket&& other)
{ *this = std::move(other); }


UDPSocket& UDPSocket::operator= (UDPSocket&& other)
{
  if(this == &other){
    return *this;
  }

  socket_fd_ = other.socket_fd_;
  other.socket_fd_ = -1;

  bound_addr_ = other.bound_addr_;
  bound_port_ = other.bound_port_;

  recv_buff_ = std::move(other.recv_buff_);

  return *this;
}


/* UDPSocket::send() sends the bytes in msg to dest_port at dest_addr. It returns a bool to indicate
 * whether the message was sent correctly. If the value returned is false, the caller can call the
 * function again to retry sending if desired.
 */
bool UDPSocket::send(const std::vector<unsigned char>& msg, const std::string& dest_addr, in_port_t dest_port)
{
  if(socket_fd_ == -1){
    throw std::runtime_error("UDPSocket: send() after move");
  }

  /* prepare address struct for sending */
  sockaddr_in dest_addr_struct;
  memset(&dest_addr_struct,0,sizeof(sockaddr_in));
  dest_addr_struct.sin_family = AF_INET;
  dest_addr_struct.sin_addr.s_addr = inet_addr(dest_addr.c_str());
  if(dest_addr_struct.sin_addr.s_addr == (in_addr_t)(-1)){
    // note that POSIX states that inet_addr retuns (in_addr_t)(-1) on error
    throw std::runtime_error("UDPSocket: bad ip address for sending");
  }
  dest_addr_struct.sin_port = htons(dest_port);

  /* send the data, retrying if sendto() is interrupted */
  ssize_t sent_size;
  do{
    sent_size = sendto(socket_fd_,msg.data(),msg.size(),0,
                       (sockaddr *)&dest_addr_struct, sizeof(dest_addr_struct));
  } while(sent_size == -1 && errno == EINTR);

  if(sent_size == -1){
    return false;
  }

  /* We regard a partial write as a failure, although this should not happen since sending
   * a UDP datagram should be an atomic operation.
   */
  ssize_t msg_size = msg.size();
  return (sent_size == msg_size);
}


/* UDPSocket::receive() attempts to read a datagram from the socket, and return
 * it to the caller along with information about where it came from, all packaged
 * in to a ReceivedUDPMessage struct.
 *
 * UDPSocket::receive() communicates errors to the caller via the boolean "valid"
 * member of the returned ReceivedUDPMessage struct (a "false" value indicates an error).
 *
 * Note that UDPSocket::receive() uses the recv_buff_ member to read data from the socket,
 * and uses the non-thread-safe function inet_ntoa(), and is thus not thread-safe.
 * However, there should not be any situation in which one UDPSocket is being used to
 * receive() by two different threads.
 */
ReceivedUDPMessage UDPSocket::receive()
{
  if(socket_fd_ == -1)
    throw std::runtime_error("UDPSocket: receive() after move");

  /* We use the standard pattern of first doing recvfrom() with MSG_PEEK set to test
   * if our receiving buffer is big enough, and enlarge it if not. We then do a further
   * call to recvfrom() to actually read the datagram.
   */

  ReceivedUDPMessage invalid_msg = {false,{},"",0}; // this will be returned if an error occurs
  ssize_t recvfrom_size;
  sockaddr_in source_addr_struct;
  socklen_t addr_len = sizeof(source_addr_struct);

  /* We may need to enlarge recv_buff_ to accept the next message. We repeatedly
   * peek at the data and enlarge recv_buff_ until the number of bytes retrieved
   * is less than the buffer size (which means we did get the whole message).
   *
   * On Linux we could avoid the loop by using MSG_TRUNC as a flag for recvfrom, but
   * this is a Linux extension which is not part of POSIX.
   */
  while(true){

    /* peek at the data, retrying if recvfrom() is interrupted */
    do{
      recvfrom_size = recvfrom(socket_fd_, recv_buff_.data(), recv_buff_.size(), MSG_PEEK,
                               (sockaddr*)&source_addr_struct, &addr_len);
    } while(recvfrom_size == -1 && errno == EINTR);

    /* we use "< 0" rather than " == -1" to be totally sure that the static_cast below
     * will always have a non-negative input (the value returned by recvfrom() should
     * always be at least -1)
     */
    if(recvfrom_size < 0){
      return invalid_msg;
    }

    /* recvfrom_size has been returned from recvfrom() on a UDP socket and so will not
     * overflow an unsigned int
     */
    if(recv_buff_.size() == static_cast<unsigned int>(recvfrom_size)){
      recv_buff_.resize(2*recv_buff_.size());
    }
    else{
      break;
    }

  }

  /* read the data, retrying if recvfrom() is interrupted */
  do{
    addr_len = sizeof(source_addr_struct);
    recvfrom_size = recvfrom(socket_fd_, recv_buff_.data(), recv_buff_.size(), 0,
                             (sockaddr*)&source_addr_struct, &addr_len);
  } while(recvfrom_size == -1 && errno == EINTR);

  if(recvfrom_size == -1){
    return invalid_msg;
  }

  //note that inet_ntoa() is not thread-safe
  return {
    true,
    std::vector<unsigned char>(recv_buff_.begin(),recv_buff_.begin()+recvfrom_size),
    std::string(inet_ntoa(source_addr_struct.sin_addr)),
    ntohs(source_addr_struct.sin_port)
  };
}


const std::string& UDPSocket::bound_addr()
{ return bound_addr_; }


in_port_t UDPSocket::bound_port()
{ return bound_port_; }


int UDPSocket::file_descriptor()
{return socket_fd_;}

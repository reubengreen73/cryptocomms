#include "Connection.h"

#include <algorithm>
#include <cerrno>
#include <poll.h>

#include "HKDFUnit.h"

namespace
{
  /* suffixes for the file names of the FIFOs where data goes in and out of the program
     these should eventually become user-settable */
  constexpr char fifo_from_user_suffix[] = "_OUTWARD";
  constexpr char fifo_to_user_suffix[] = "_INWARD";

  constexpr unsigned int msgnum_len = 6;
  constexpr unsigned int segnum_len = 6;
  constexpr unsigned int tag_len = 16;
  /* an outer packet header consists of the sender id (4 bytes), the channel id (2 bytes),
     the receiver's segment number (6 bytes), the sender's segment number (6 bytes), and
     the message number (6 bytes), for a total of 24 bytes */
  constexpr unsigned int outer_header_len = 24;


  /* bytes_to_uint() converts "length" bytes from bytes_vector, beginning at position
     "offset", to an unsigned integer of type T, using the little-endian convention */
  template<typename T> // T must be an unsigned integer type
  T bytes_to_uint(const std::vector<unsigned char>& bytes_vector,
                     unsigned int offset, unsigned int length)
  {
    T val = 0;
    for(int i=length-1; i > -1; i--){
      val = (val << 8) + bytes_vector[offset+i];
    }
    return val;
  }


  /* uint_to_bytes() converts the unsigned integer value "val" of unsigned integer type T
     to a string of bytes of length "length" using the little-endian convention */
  template<typename T> // T must be an unsigned integer type
  std::vector<unsigned char> uint_to_bytes(T val,
                                           unsigned int length)
  {
    std::vector<unsigned char> bytes_vector(length);
    for(unsigned int i=0; i<length; i++){
      bytes_vector[i] = ( val >> i*8 ) & 0xff;
    }
    return bytes_vector;
  }


  /* fd_has_data() tests whether there is data waiting to be read on the file
     descriptor fd */
  bool fd_has_data(int fd)
  {
    pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    /* loop until poll() returns a non-error result */
    while(true){
      int ret = poll(&pfd,1,0); //0 means return immediately

      if(ret == -1){
        if( (errno == EINTR) or (errno == EAGAIN) ){
          // recoverable error, try again
          continue;
        }
        throw std::runtime_error("Connection: poll() reported an error");
      }

      if((ret == 1) && (pfd.revents & POLLIN)){
        return true;
      }
      return false;
    }
  }


}


Connection::Connection(const host_id_type& self_id,
                       const std::string& peer_name,
                       const host_id_type& peer_id,
                       const channel_id_type& channel_id,
                       const std::string& fifo_base_path,
                       const SecretKey& key,
                       const std::string& peer_ip_addr,
                       in_port_t peer_port,
                       unsigned int max_packet_size,
                       const std::shared_ptr<UDPSocket>& udp_socket,
                       const std::shared_ptr<SegmentNumGenerator>& segnumgen):
  self_id_(self_id),
  peer_name_(peer_name),
  peer_id_(peer_id),
  channel_id_(channel_id),
  peer_ip_addr_(peer_ip_addr),
  peer_port_(peer_port),
  max_packet_size_(max_packet_size),
  udp_socket_(udp_socket),
  segnumgen_(segnumgen),
  rtt_tracker_(std::make_shared<RTTTracker>()),
  fifo_from_user_(fifo_base_path+fifo_from_user_suffix),
  fifo_to_user_(fifo_base_path+fifo_to_user_suffix),
  current_crypto_message_tracker_(rtt_tracker_),
  old_crypto_message_tracker_(rtt_tracker_),
  current_peer_segnum_(0),
  old_peer_segnum_(0),
  current_local_segnum_(segnumgen_->next_num()),
  old_local_segnum_(0),
  local_next_msgnum_(1),
  last_hello_packet_sent_(0)
{
  /* We need to derive the sending and receiving keys to initialise the CryptoUnit.
     They are both derived by the HKDF expand operation using the shared secret (which
     is held in the "key" argument to this constructor) as the secret key, but with
     different "info" parameters. The sending key is derived using the "info" formed
     by the concatenation self_id_|peer_id_|channel_id_, from which it follows that
     the receiving key (which is the peer's sending key) is derived using the "info"
     formed by the concatenation peer_id_|self_id_|channel_id_. */

  /* create the "info" for the sending key */
  std::vector<unsigned char> send_info( (2*host_id_size)+channel_id_size );
  auto send_info_start = send_info.begin();
  std::copy(self_id_.begin(),self_id_.end(),send_info_start);
  std::copy(peer_id_.begin(),peer_id_.end(),send_info_start+host_id_size);
  std::copy(channel_id_.begin(),channel_id_.end(),
            send_info_start+(2*host_id_size));

  /* create the "info" for the receiving key */
  std::vector<unsigned char> recv_info( (2*host_id_size)+channel_id_size );
  auto recv_info_start = recv_info.begin();
  std::copy(peer_id_.begin(),peer_id_.end(),recv_info_start);
  std::copy(self_id_.begin(),self_id_.end(),recv_info_start+host_id_size);
  std::copy(channel_id_.begin(),channel_id_.end(),
            recv_info_start+(2*host_id_size));

  /* create the CryptoUnit, with the sending key for encryption and the receiving
     key for decryption */
  crypto_unit_ = std::make_unique<CryptoUnit>(hkdf_expand(key,send_info),
                                              hkdf_expand(key,recv_info));
}


/* This function is temporary code which implements very simplistic logic for
 * moving data from the "from user" FIFO to the UDP socket, and for moving data
 * that has come from the UDP socket to the "to user" FIFO.
 *
 * The function just runs a loop where, on each pass of the loop, we attempt to
 * pull one UDP message from message_queue_, decrypt it, and write its contents to
 * fifo_to_user_, and then we attempt to pull one packet's worth of data out of
 * fifo_from_user_, encrypt it, and send it via udp_socket_. The loop runs for at most
 * loop_max passes, or until neither operation has any data to work with.
 */
void Connection::move_data(unsigned int loop_max)
{
  bool no_more_data = false; // used to record if a pass of the loop moved
                             // any data
  /* When trying to make initial contact with the peer, we send empty packets to inform
     the peer of our current segment number and to elicit a response containing theirs.
     These packets are called "hello packets", and we only send one per invocation of
     move_data(). hello_packet_sent records whether a hello packet has been sent. */
  bool hello_packet_sent = false;

  for(unsigned int i=0; (i<loop_max) and (not no_more_data); i++){
    std::vector<unsigned char> fifo_data, msg_data;
    no_more_data = true;

     /* attempt to pull a UDP message off message_queue_... */
    ReceivedUDPMessage udp_message{false,{},"",0}; // initialize udp_message to
                                                   // an "invalid" state
    {// new block to limit the scope of queue_lock_guard
      const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
      if(!message_queue_.empty()){
        udp_message = message_queue_.front();
        message_queue_.pop_front();
      }
    }
    /* ...and if there was a message waiting, pass it to handle_message() for
       processing */
    if(udp_message.valid){
      no_more_data = false;
      handle_message(udp_message.data);
    }

    /* try to move some data from fifo_from_user_ to the network */
    if(current_peer_segnum_ == 0){
      /* We need to know what segment number our peer is currently using to send data, as
         otherwise any data we send will not be accepted. If current_peer_segnum_ is 0,
         then we do not know the peer's current segment number. If there is data waiting to
         be sent, then we must send an empty "hello packet" to the peer to elicit a
         response which will contain their current segment number (this packet will also
         inform the peer of our current segment number). We only send one "hello packet"
         per invocation of move_data(). */
      if( (not hello_packet_sent) and \
          fd_has_data(fifo_from_user_.file_descriptor()) ){
        udp_socket_->send(create_packet(std::vector<unsigned char>{}),
                          peer_ip_addr_,peer_port_);
        last_hello_packet_sent_ = epoch_time_millis();
        hello_packet_sent = true;
      }
    }
    else{
      /* attempt to pull one packet's worth of data out of fifo_from_user, and if
         there is some data available, we encapsulate it in an encrypted packet and
         send it via udp_socket_ */
      fifo_data = fifo_from_user_.read(max_packet_size_-(outer_header_len+tag_len));
      if(fifo_data.size() > 0){
        no_more_data = false;
        udp_socket_->send(create_packet(fifo_data),
                          peer_ip_addr_,
                          peer_port_);
      }
    }

  }

}


/* Connection::is_data() tests whether there is currently any data to be processed
 * for the Connection
 */
bool Connection::is_data()
{
  /* check if there are any messages in message_queue_ */
  {// new block to limit the scope of queue_lock_guard
    const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
    if(!message_queue_.empty()){
      return true;}
  }

  /* check if there is any data to be read on fifo_from_user_ */
  if(current_peer_segnum_ == 0){
    /* if the Connection is "closed", then we cannot process any data waiting on the
       FIFO */
    return false;
  }

  return fd_has_data(fifo_from_user_.file_descriptor());
}


/* Connection::add_message(const ReceivedUDPMessage&) adds a UDP
 * message to the incoming message queue, message_queue_ (by copying)
 */
void Connection::add_message(const ReceivedUDPMessage& msg)
{
  const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
  message_queue_.push_back(msg);
}


/* Connection::add_message(const ReceivedUDPMessage&&) adds a UDP
 * message to the incoming message queue, message_queue_ (by moving)
 */
void Connection::add_message(ReceivedUDPMessage&& msg)
{
  const std::lock_guard<std::mutex> queue_lock_guard(queue_lock_);
  message_queue_.push_back(std::move(msg));
}


/* Connection::from_user_fifo_fd() returns the file descriptor for the
 * Connection's FromUserFifo
 */
int Connection::from_user_fifo_fd()
{ return fifo_from_user_.file_descriptor(); }


/* Connection::open_status() reports whether the Connection is "open",
 * meaning that it has a segment number which it can use to send encrypted
 * packets to the peer. The first element of the return value reports whether
 * the Connection is "open", while the second is the number of milliseconds
 * since the UNIX epoch when the last "hello" packet was sent to the peer
 * in order to obtain a response containing a segment number.
 */
std::pair<bool,millis_timestamp_t> Connection::open_status()
{ return {current_peer_segnum_ != 0, last_hello_packet_sent_}; }


/* Connection::unpack_header() extracts the various entities encoded in the
 * outer header of a packet.
 */
Connection::MessageOuterHeader
Connection::unpack_header(const std::vector<unsigned char>& message_bytes)
{
  MessageOuterHeader moh;
  std::vector<unsigned char>::size_type offset = 0;
  auto msg_start = message_bytes.begin();

  /* sender id */
  std::copy(msg_start, msg_start+host_id_size, moh.sender_id.begin());
  offset += host_id_size;

  /* channel id */
  std::copy(msg_start+offset,
            msg_start+offset+channel_id_size,
            moh.channel_id.begin());
  offset += channel_id_size;

  /* receiver's segment number, both as an integer in moh.my_segnum and
     as a string of bytes in moh.ad for use as the additional data of
     the AEAD decryption */
  std::copy(msg_start+offset,
            msg_start+offset+segnum_len,
            moh.ad.begin());
  moh.my_segnum =
    bytes_to_uint<SegmentNumGenerator::segnum_t>(message_bytes,
                                                 offset,
                                                 segnum_len);
  offset += segnum_len;

  /* sender's segment number and the message number as one string of
     bytes for use as the initialization vector of the AEAD decryption */
  std::copy(msg_start+offset,
            msg_start+offset+segnum_len+msgnum_len,
            moh.iv.begin());

  /* peer's segment number */
  moh.peer_segnum =
    bytes_to_uint<SegmentNumGenerator::segnum_t>(message_bytes,offset,
                                                 segnum_len);
  offset += segnum_len;

  /* message number */
  moh.msgnum =
    bytes_to_uint<CryptoMessageTracker::msgnum_t>(message_bytes,offset,
                                                  segnum_len);

  return moh;
}


/* Connection::create_packet() creates a packet consisting of an outer
 * header and an encrypted payload holding the encryption of data_bytes
 * (and the AEAD tag). The peer_segnum argument defaults to 0, which means
 * use current_peer_segnum_, but if a non-zero value is used then this
 * segment number is used instead.
 */
std::vector<unsigned char>
Connection::create_packet(const std::vector<unsigned char>& data_bytes,
                          SegmentNumGenerator::segnum_t peer_segnum)
{
  /* If local_next_msgnum_ has gone beyond the range of values which can
     fit in a 6 byte value, get a new segment number. The maximum message
     number is (2^48 - 1). */
  if(local_next_msgnum_ > 281474976710655U){
    old_local_segnum_ = current_local_segnum_;
    current_local_segnum_ = segnumgen_->next_num();
    local_next_msgnum_ = 1;
  }

  std::vector<unsigned char> packet(data_bytes.size() +
                                    (outer_header_len+tag_len));
  std::vector<unsigned char>::size_type offset = 0;

  /* copy in our id as the sender's id */
  std::copy(self_id_.begin(),self_id_.end(),packet.begin());
  offset += host_id_size;

  /* copy in the channel id */
  std::copy(channel_id_.begin(),channel_id_.end(),
            packet.begin()+offset);
  offset += channel_id_size;

  /* convert the peer segment number to bytes and copy it in */
  std::vector<unsigned char> peer_segnum_bytes = (peer_segnum == 0) ?
    uint_to_bytes(current_peer_segnum_,segnum_len) :
    uint_to_bytes(peer_segnum,segnum_len);
  std::copy(peer_segnum_bytes.begin(),peer_segnum_bytes.end(),
            packet.begin()+offset);
  offset += segnum_len;

  std::vector<unsigned char> temp_bytes;

  /* convert our segment number to a string of bytes and copy it in */
  temp_bytes = uint_to_bytes(current_local_segnum_,segnum_len);
  std::copy(temp_bytes.begin(),temp_bytes.end(),
            packet.begin()+offset);
  offset += segnum_len;

  /* convert our next message number to a string of bytes, copy it
     it in, and increment the next message number */
  temp_bytes = uint_to_bytes(local_next_msgnum_,msgnum_len);
  std::copy(temp_bytes.begin(),temp_bytes.end(),
            packet.begin()+offset);
  offset += msgnum_len;
  local_next_msgnum_++;

  /* create the AEAD initialization vector by concatenating the byte strings
     representing our segment number and the message number */
  CryptoUnit::iv_t iv;
  std::copy(packet.begin()+host_id_size+channel_id_size+segnum_len,
            packet.begin()+outer_header_len,
            iv.begin());

  crypto_unit_->encrypt(data_bytes, peer_segnum_bytes,
                       iv, packet, outer_header_len);
  return packet;
}


/* Connection::handle_message() processes a received message.
 */
void Connection::handle_message(std::vector<unsigned char>& message_data)
{
  /* a legitimate message must have at least an outer header and an AEAD tag */
  if( message_data.size() < (outer_header_len+tag_len) ){
    return;
  }

  MessageOuterHeader msg_oh = unpack_header(message_data);
  if(msg_oh.peer_segnum == 0){
    /* no legitimate message would ever have a sender's segment number of 0 */
    return;
  }

  /* We wrap the decryption logic in a lambda expression to avoid code duplication
     below. Note that the "good_decrypt" parameter of CryptoUnit::decrypt() is
     an output parameter taken by reference, which is used to record the success
     of the decryption.  */
  auto do_decryption = [&](bool& good_decrypt)
  {
    return crypto_unit_->decrypt(message_data,
                                std::vector<unsigned char>(msg_oh.ad.begin(),
                                                           msg_oh.ad.end()),
                                msg_oh.iv,
                                outer_header_len,
                                message_data.size()-outer_header_len,
                                good_decrypt);
  };

  /* We only accept packets whose header contains a receiver segment number which is
     non-zero and equal to current_local_segnum_ or old_local_segnum_ to prevent replay
     attacks. If the header's receiver segment number does not meet this specification,
     we discard the packet. However, if the packet decrypts correctly then we send an
     empty packet to let the peer know our current segment number.

     We explicitly disallow a receiver segment number of 0 because old_local_segnum_
     will often be 0, indicating that current_local_segnum_ is the only segment number
     we have used in this session.

     We only send an empty packet if the sender segment number in the packet (i.e.
     msg_oh.peer_segnum) is greater than current_peer_segnum_, since any packet which
     does not satisfy this is too old to be worth responding to (this prevents attackers
     succeeding in the admittedly mild mischief of getting us to send many empty response
     packets to replayed old packets). */
  bool msg_my_segnum_good = (msg_oh.my_segnum != 0) and \
    ( (msg_oh.my_segnum == current_local_segnum_) or (msg_oh.my_segnum == old_local_segnum_) );
  if(not msg_my_segnum_good){
    if(msg_oh.peer_segnum <= current_peer_segnum_){
      return;
    }
    bool good_decrypt;
    do_decryption(good_decrypt);
    if(good_decrypt){
      /* we need to use the sender segment number from the packet (i.e. msg_oh.peer_segnum)
         as the receiver segment number in our empty packet so that the peer will accept
         the packet, but we don't "confirm" this peer segment number yet (see below) since
         we have not yet seen it in a packet with our current segment number */
      udp_socket_->send(create_packet({},msg_oh.peer_segnum),
                        peer_ip_addr_,
                        peer_port_);
    }
    return;
  }

  /* At this point, we know that the packet's header contains our current or previous segment
   * number, so now we turn to the sender's segment number and the message number. */

  /* If the sender's segment number (i.e. msg_oh.peer_segnum) is one that we have already
     "confirmed" to be a valid segment number in use by our peer (see below), then we just
     check that the message number has not previously been seen, and if not we decrypt the
     message and, if that succeeds, we write the decrypted data to the FIFO.

     The segment numbers that we recognise as "confirmed" are the ones stored in
     current_peer_segnum_ and old_peer_segnum_ (note that we have already confirmed that
     msg_oh.peer_segnum is not 0), noting that a non-zero segment number in old_peer_segnum_
     must be the previous value which was stored in current_peer_segnum_ (see below).
  */
  if( (msg_oh.peer_segnum == current_peer_segnum_) or
      (msg_oh.peer_segnum == old_peer_segnum_) ){
    /* grab a reference to the CryptoMessageTracker corresponding to the peer segment number
       msg_oh.peer_segnum */
    CryptoMessageTracker& cmt = (msg_oh.peer_segnum == current_peer_segnum_) ?
      current_crypto_message_tracker_ : old_crypto_message_tracker_;

    /* Check that the message number has not been seen on a previous valid packet, and if it
       has not then decrypt it, write the data to the FIFO, and log the message number */
    if(not cmt.have_seen_msgnum(msg_oh.msgnum)){
      bool good_decrypt;
      std::vector<unsigned char> plaintext_data =
        do_decryption(good_decrypt);
      if(good_decrypt){
        cmt.log_msgnum(msg_oh.msgnum);
        fifo_to_user_.write(plaintext_data);
      }
    }

    return;
  }

  /* At this point, we must have a packet which contains a peer segment number which is not one
     that has been "confirmed". We may thus be dealing with a new segment number from our peer.
     We only "confirm" a peer segment number if (A) it is greater than any previous peer segment
     number we have seen in this session, and (B) it is in the header of a valid packet which
     contains a segment number from us which we recognise as one we have used recently in this
     session. (B) is already known to hold, so we need only check (A) and that the packet is
     valid. */
  if(msg_oh.peer_segnum > current_peer_segnum_){
    bool good_decrypt;
    std::vector<unsigned char> plaintext_data =
      do_decryption(good_decrypt);
    if(good_decrypt){
      /* we now want to confirm this new peer segment number, which we do by moving the existing
         peer segment number to old_peer_segnum_ (and copying its CryptoMessageTracker to
         old_crypto_message_tracker_), putting the new segment number in current_peer_segnum_,
         and resetting current_crypto_message_tracker_ for use with the new segment number */
      old_peer_segnum_ = current_peer_segnum_;
      old_crypto_message_tracker_ = current_crypto_message_tracker_;
      current_peer_segnum_ = msg_oh.peer_segnum;
      current_crypto_message_tracker_.reset();

      // now handle the message itself
      current_crypto_message_tracker_.log_msgnum(msg_oh.msgnum);
      fifo_to_user_.write(plaintext_data);
    }
  }

}

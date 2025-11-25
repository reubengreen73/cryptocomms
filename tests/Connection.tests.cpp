#include "testsys.h"
#include "../Connection.h"
#include "../CryptoUnit.h"
#include "../FifoIO.h"
#include "../IDTypes.h"
#include "../ReceivedUDPMessage.h"
#include "../SecretKey.h"
#include "../SegmentNumGenerator.h"
#include "../UDPSocket.h"
#include "../HKDFUnit.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>


namespace
{

  void do_pause(unsigned int millis = 25)
  { std::this_thread::sleep_for(std::chrono::milliseconds(millis)); }


  std::vector<unsigned char> read_from_fifo(int fd, unsigned int size)
  {
    std::vector<unsigned char> fifo_buff(size);
    ssize_t ret = -1;
    while(ret == -1){
      ret = read(fd,fifo_buff.data(),size);
      if(ret == -1){
        if(errno == EINTR){
          continue;
        }
        TESTERROR("could not read from FIFO");
      }
    }
    return std::vector<unsigned char>(fifo_buff.begin(),fifo_buff.begin()+ret);
  }


  void write_to_fifo(int fd, const std::vector<unsigned char>& data)
  {
    ssize_t ret = -1;
    while(ret == -1){
      ret = write(fd,data.data(),data.size());
      if(ret == -1){
        if(errno == EINTR){
          continue;
        }
        TESTERROR("could not write to FIFO");
      }
    }
    TESTASSERT(static_cast<unsigned int>(ret) == data.size());
  }


  /* make_data() generates "len" bytes for use as dummy data for sending */
  std::vector<unsigned char> make_data(unsigned int len)
  {
    static unsigned char x = 13;
    std::vector<unsigned char> data(len);
    for(unsigned int i; i<len; i++){
      data[i] = x;
      int y = x;
      x = (y+7) % 256;
    }
    return data;
  }


  /* struct to hold what is returned by create_connection() */
  struct ConnectionAndRelated
  {
    std::shared_ptr<Connection> conn;
    std::shared_ptr<CryptoUnit> crypto;
    host_id_type conn_id;
    host_id_type peer_id;
    channel_id_type channel_id;
    int from_user_fifo_fd;
    int to_user_fifo_fd;
    int socket_fd;
    in_port_t socket_fd_bound_port;
    ConnectionAndRelated():
      from_user_fifo_fd(-1),
      to_user_fifo_fd(-1),
      socket_fd(-1) {}
    ~ConnectionAndRelated()
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
   * for use in testing, and returns it together with related objects necessary
   * for using it.
   */
  ConnectionAndRelated create_connection()
  {
    ConnectionAndRelated conn_etc;

    /* 1 - prepare a socket to act as the Connection's "remote peer" */

    /* create a socket */
    conn_etc.socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(conn_etc.socket_fd == -1){
      TESTERROR("could not create socket for testing");
    }

    /* prepare an address struct to bind the socket */
    sockaddr_in bind_addr;
    memset(&bind_addr,0,sizeof(sockaddr_in));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(bind_addr.sin_addr.s_addr == (in_addr_t)(-1)){
      close(conn_etc.socket_fd);
      TESTERROR("bad IP address for binding");
    }
    bind_addr.sin_port = htons(0);

    /* bind the socket */
    if(bind(conn_etc.socket_fd,(sockaddr*)&bind_addr,sizeof(bind_addr)) == -1){
      close(conn_etc.socket_fd);
      TESTERROR("could not bind");
    }

    /* find the socket's port */
    socklen_t socklen = sizeof(bind_addr);
    if(getsockname(conn_etc.socket_fd,(sockaddr*)&bind_addr,&socklen) == -1){
      close(conn_etc.socket_fd);
      TESTERROR("could not get socket information after bind");
    }
    conn_etc.socket_fd_bound_port = ntohs(bind_addr.sin_port);

    /* 2 - create the segment number file */
    std::string segnumfile_name("segnumfile");
    std::string segnum_string("0");
    std::ofstream segnum_file(segnumfile_name,std::ios::out);
    segnum_file << segnum_string;
    segnum_file.close();

    /* 3 - make the Connection */

    /* parameters for the Connection */
    conn_etc.conn_id = {0x01,0x4a,0x72,0xb1};
    conn_etc.peer_id = {0xa3,0x90,0x1c,0x00};
    conn_etc.channel_id = {0x66,0x10};
    std::string peer_name = "another host";
    std::string fifo_base_path = "fifo_base_name";
    SecretKey key("00010a0Aa0A0ffFF00010203c1c2c3f0fafbfc01234567890abcdef0ABCDEF00");
    std::string peer_ip_addr = "127.0.0.1";
    unsigned int max_packet_size = 1000;
    std::shared_ptr<UDPSocket> udp_socket =
      std::make_shared<UDPSocket>("127.0.0.1",0);
    std::shared_ptr<SegmentNumGenerator> segnumgen =
      std::make_shared<SegmentNumGenerator>(segnumfile_name,1);

    conn_etc.conn = std::make_shared<Connection>(conn_etc.conn_id,
                                                 peer_name,
                                                 conn_etc.peer_id,
                                                 conn_etc.channel_id,
                                                 fifo_base_path,
                                                 key,
                                                 peer_ip_addr,
                                                 conn_etc.socket_fd_bound_port,
                                                 max_packet_size,
                                                 udp_socket,
                                                 segnumgen);

    /* 4 - open the Connection's FIFOs
     * Note that the literal strings "_OUTWARD" and "_INWARD" need to be kept in sync
     * with the values in Connection.cpp (this is not good and should probably be fixed
     * somehow).
     */
    std::string to_user_fifo_name(fifo_base_path+"_INWARD");
    std::string from_user_fifo_name(fifo_base_path+"_OUTWARD");
    conn_etc.to_user_fifo_fd = open(to_user_fifo_name.c_str(), O_RDONLY);
    TESTASSERT( conn_etc.to_user_fifo_fd != -1 );
    conn_etc.from_user_fifo_fd = open(from_user_fifo_name.c_str(), O_WRONLY);
    TESTASSERT( conn_etc.from_user_fifo_fd != -1 );

    /* 5 - create the CryptoUnit */
    std::vector<unsigned char> enc_info( (2*host_id_size)+channel_id_size );
    auto enc_info_start = enc_info.begin();
    std::copy(conn_etc.peer_id.begin(),conn_etc.peer_id.end(),enc_info_start);
    std::copy(conn_etc.conn_id.begin(),conn_etc.conn_id.end(),enc_info_start+host_id_size);
    std::copy(conn_etc.channel_id.begin(),conn_etc.channel_id.end(),
              enc_info_start+(2*host_id_size));

    std::vector<unsigned char> dec_info( (2*host_id_size)+channel_id_size );
    auto dec_info_start = dec_info.begin();
    std::copy(conn_etc.conn_id.begin(),conn_etc.conn_id.end(),dec_info_start);
    std::copy(conn_etc.peer_id.begin(),conn_etc.peer_id.end(),dec_info_start+host_id_size);
    std::copy(conn_etc.channel_id.begin(),conn_etc.channel_id.end(),
              dec_info_start+(2*host_id_size));

    conn_etc.crypto = std::make_shared<CryptoUnit>(hkdf_expand(key,enc_info),
                                                   hkdf_expand(key,dec_info));

    return conn_etc;
  }


  /* make_packet() creates an encrypted protocol packet using the supplied parameters */
  std::vector<unsigned char> make_packet(host_id_type sender_id,
                                         channel_id_type channel_id,
                                         SegmentNumGenerator::segnum_t recv_segnum,
                                         SegmentNumGenerator::segnum_t send_segnum,
                                         CryptoMessageTracker::msgnum_t msgnum,
                                         const std::vector<unsigned char>& plaintext,
                                         const std::shared_ptr<CryptoUnit>& crypto)
  {
    /* The packet format consists of an unencrypted 24 byte header, then the encrypted
       payload, then the 16 byte AEAD tag. We define a buffer "packet" of the appropriate
       size, where we shall construct the packet. */
    std::vector<unsigned char> packet(24+16+plaintext.size());
    auto packet_start = packet.begin();

    /* insert_int is a convenience function for writing an unsigned integer into the packet
       in little-endian format */
    auto insert_int = [](std::vector<unsigned char>& dest,
                         std::vector<unsigned char>::size_type offset,
                         auto val,
                         unsigned int len)
    {
      for(unsigned int i=0; i<len; i++){
        dest[offset+i] = (val >> i*8) & 0xff;
      }
    };

    /* write the 24 byte outer header */
    std::vector<unsigned char>::size_type offset = 0;
    std::copy(sender_id.begin(), sender_id.end(), packet_start);
    offset += host_id_size;
    std::copy(channel_id.begin(), channel_id.end(), packet_start+offset);
    offset += channel_id_size;
    insert_int(packet,offset,recv_segnum,6);
    offset += 6;
    insert_int(packet,offset,send_segnum,6);
    offset += 6;
    insert_int(packet,offset,msgnum,6);
    offset += 6;

    /* encrypt the payload and write the ciphertext and AEAD tag to the packet */
    std::vector<unsigned char> iv_vec(12);
    insert_int(iv_vec,0,send_segnum,6);
    insert_int(iv_vec,6,msgnum,6);
    CryptoUnit::iv_t iv;
    std::copy(iv_vec.begin(),iv_vec.end(),iv.begin());
    std::vector<unsigned char> ad(6);
    insert_int(ad,0,recv_segnum,6);
    crypto->encrypt(plaintext, ad, iv, packet, 24);

    return packet;
  }


  /* struct to hold all the data from the header of a packet, and the decrypted contents */
  struct OpenedPacket
  {
    bool valid; // records whether the original packet decrypted and authenticated correctly
    host_id_type sender_id;
    channel_id_type channel_id;
    SegmentNumGenerator::segnum_t recv_segnum;
    SegmentNumGenerator::segnum_t send_segnum;
    CryptoMessageTracker::msgnum_t msgnum;
    std::vector<unsigned char> contents;
  };

  /* check_packet() checks that a received packet is as expected. Note the
   * usage of the send_segnum parameter: if this is non-zero, then the
   * packet's send_segnum must match it, but if it is zero then the
   * packet's send_segnum may be any non-zero value.
   */
  void check_packet(const OpenedPacket& op,
                    const ConnectionAndRelated& conn_etc,
                    SegmentNumGenerator::segnum_t recv_segnum,
                    SegmentNumGenerator::segnum_t send_segnum,
                    std::set<CryptoMessageTracker::msgnum_t>& conn_msgnums,
                    const std::vector<unsigned char>& expected_contents)
  {
    TESTASSERT(op.valid);
    TESTASSERT(op.sender_id == conn_etc.conn_id);
    TESTASSERT(op.channel_id == conn_etc.channel_id);

    TESTASSERT(op.recv_segnum == recv_segnum);

    if(send_segnum == 0){
      TESTASSERT(op.send_segnum != 0);
    }
    else{
      TESTASSERT(op.send_segnum == send_segnum);
    }

    /* check that the message number has not been seen already, and log it */
    TESTASSERT(conn_msgnums.count(op.msgnum) == 0);
    conn_msgnums.insert(op.msgnum);
    TESTASSERT(op.contents == expected_contents);
  }


  /* struct to hold the state needed to simulate a Connection object, to allow
   * us to test communication with a real Connection object
   */
  struct ConnState
  {
    SegmentNumGenerator::segnum_t conn_segnum;
    SegmentNumGenerator::segnum_t peer_segnum;
    CryptoMessageTracker::msgnum_t peer_next_msgnum;
    std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;
  };


  /* create_and_send_good_packet() creates an encrypted packet as would be produced
   * by a Connection which is the peer of the Connection in conn_etc, with state
   * as held in conn_state, and add it to the incoming message queue of the Connection
   * in conn_etc as though it had arrived over the network.
   */
  void create_and_send_good_packet(const ConnectionAndRelated& conn_etc,
                                   ConnState& conn_state,
                                   const std::vector<unsigned char>& contents,
                                   bool do_move = true)
  {
    std::vector<unsigned char> packet_data = make_packet(conn_etc.conn_id,
                                                         conn_etc.channel_id,
                                                         conn_state.conn_segnum,
                                                         conn_state.peer_segnum,
                                                         conn_state.peer_next_msgnum++,
                                                         contents,
                                                         conn_etc.crypto);
    conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                  conn_etc.socket_fd_bound_port});
    if(do_move){
      conn_etc.conn->move_data(1);
    }
  }


  /* get_packet_from_socket() reads a single packet from the network socket in conn_etc,
   * and then unpacks its header and decrypts its contents. Note that the returned OpenedPacket
   * structure records whether the AEAD decryption was valid in its "valid" member.
   */
  OpenedPacket get_packet_from_socket(const ConnectionAndRelated& conn_etc,
                                      unsigned int buffsize)
  {
    /* 1 - get a packet from the socket */
    std::vector<unsigned char> socket_buff(buffsize);
    sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    ssize_t ret = -1;
    while(ret == -1) {
      ret = recvfrom(conn_etc.socket_fd,socket_buff.data(),buffsize,0,
                     (sockaddr*)&source_addr,&addr_len);
      if(ret == -1){
        if(errno == EINTR){
          continue;
        }
        TESTERROR("could not read from socket");
      }
    }

    /* 2 - unpack and decrypt the packet */

    /* a valid packet must have at least a 24 byte header and a 16 byte AEAD tag, so
       it cannot be smaller than 40 bytes */
    TESTASSERT(ret >= 40);
    std::vector<unsigned char> packet_data =
      std::vector<unsigned char>(socket_buff.begin(),
                                 socket_buff.begin()+ret);
    OpenedPacket op;
    auto packet_start = packet_data.begin();

    /* extract_int is a convenience function to read an unsigned integer from
       a little-endian byte string in the  packet data */
    auto extract_int = [](const std::vector<unsigned char>& src,
                          std::vector<unsigned char>::size_type offset,
                          unsigned int len)
    {
      std::uint_least64_t val = 0; // all our unsigned int types fit in 64 bits
      for(int i=len-1; i>-1; i--){
        val = (val << 8) + src[offset+i];
      }
      return val;
    };

    /*  unpack the packet header */
    std::vector<unsigned char>::size_type offset = 0;
    std::copy(packet_start,
              packet_start+host_id_size,
              op.sender_id.begin());
    offset += host_id_size;
    std::copy(packet_start+offset,
              packet_start+offset+channel_id_size,
              op.channel_id.begin());
    offset += channel_id_size;
    std::vector<unsigned char> ad(packet_start+offset,
                                  packet_start+offset+6);
    op.recv_segnum = extract_int(packet_data,offset,6);
    offset += 6;
    CryptoUnit::iv_t iv;
    std::copy(packet_start+offset,
              packet_start+offset+12,
              iv.begin());
    op.send_segnum = extract_int(packet_data,offset,6);
    offset += 6;
    op.msgnum = extract_int(packet_data,offset,6);
    offset += 6;

    /* decrypt the packet's contents */
    op.contents = conn_etc.crypto->decrypt(packet_data, ad, iv, offset,
                                           packet_data.size()-offset,
                                           op.valid);

    return op;
  }


  /* init_from_peer() simulates a peer Connection initiating communication with the
   * the Connection in conn_etc, storing the state of the communication in conn_state
   * and conn_msgnums (which records which message numbers the Connection in conn_etc
   * has sent). The simulated peer Connection uses the segment number peer_segnum.
   */
  void init_from_peer(const ConnectionAndRelated& conn_etc,
                      ConnState& conn_state,
                      std::set<CryptoMessageTracker::msgnum_t>& conn_msgnums,
                      SegmentNumGenerator::segnum_t peer_segnum)
  {
    /* the initiation packets we shall send need to have receiver segment number 0 */
    conn_state.conn_segnum=0;
    conn_state.peer_segnum=peer_segnum;
    conn_state.peer_next_msgnum=1;

    create_and_send_good_packet(conn_etc,conn_state,{});
    OpenedPacket op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,conn_state.peer_segnum,0,conn_msgnums,{});
    conn_state.conn_segnum = op.send_segnum; // store the Connection's segment number for
                                             // future use
  }


  /*  init_from_conn() makes the Connection object in conn_etc initiate communications with
   *  its peer, and simulates that peer to complete the initialisation
   */
  void init_from_conn(const ConnectionAndRelated& conn_etc,
                      ConnState& conn_state,
                      std::set<CryptoMessageTracker::msgnum_t>& conn_msgnums,
                      SegmentNumGenerator::segnum_t peer_segnum)
  {
    conn_state.conn_segnum=0;
    conn_state.peer_segnum=peer_segnum;
    conn_state.peer_next_msgnum=1;

    /* get the Connection in conn_etc to initiate communication with its peer by writing some
       data to the FIFO and calling move_data()  */
    std::vector<unsigned char> data = make_data(17);
    write_to_fifo(conn_etc.from_user_fifo_fd,data);
    conn_etc.conn->move_data(1);

    /* check that the Connection sent an initialisation packet correctly, and retrieve the
       segment number from the packet */
    OpenedPacket op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,0,0,conn_msgnums,{});
    conn_state.conn_segnum = op.send_segnum;

    /* send a response packet to tell the Connection our segment number, then check that
       the Connection sent the data correctly */
    create_and_send_good_packet(conn_etc,conn_state,{});
    op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,conn_state.peer_segnum,
                 conn_state.conn_segnum,conn_msgnums,data);
  }


  /* send_data_into_conn() creates some bytes, puts them in a protocol packet,
   * inserts this packet into the message queue of the Connection in conn_etc,
   * and then checks that the correct data comes out of the Connection's FIFO
   */
  void send_data_into_conn(const ConnectionAndRelated& conn_etc,
                           ConnState& conn_state,
                           unsigned int num_bytes)
  {
    std::vector<unsigned char> data = make_data(num_bytes);
    create_and_send_good_packet(conn_etc,conn_state,data);
    std::vector<unsigned char> fifo_data =
      read_from_fifo(conn_etc.to_user_fifo_fd,data.size());
    TESTASSERT(fifo_data == data);
  }


  /* send_data_from_conn() writes some data into the FIFO of the Connection in
   * conn_etc, and then checks that the correct packets are sent out by the
   * Connection
   */
  void send_data_from_conn(const ConnectionAndRelated& conn_etc,
                           ConnState& conn_state,
                           std::set<CryptoMessageTracker::msgnum_t>& conn_msgnums,
                           unsigned int num_bytes)
  {
    std::vector<unsigned char> data = make_data(num_bytes);
    write_to_fifo(conn_etc.from_user_fifo_fd,data);
    conn_etc.conn->move_data(1);
    OpenedPacket op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,conn_state.peer_segnum,conn_state.conn_segnum,
                 conn_msgnums,data);
  }


  /* check_no_action() checks that the Connection conn_etc does not send any response
   * packet or write anything to its output FIFO when it receives the packet packet_data
   */
  void check_no_action(const ConnectionAndRelated& conn_etc,
                       const std::vector<unsigned char>& packet_data)
  {
    /* put packet_data into the Connection's Message queue and call move_data() */
    conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                  conn_etc.socket_fd_bound_port});
    conn_etc.conn->move_data(1);
    do_pause(); // pause to ensure that any response data has time to become readable

    std::vector<int> fds{conn_etc.to_user_fifo_fd,conn_etc.socket_fd};
    for(int fd : fds){
      pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      int ret = -1;
      /* loop until poll() returns a non-error result on fd */
      while(ret == -1){
        ret = poll(&pfd,1,0); //0 means return immediately

        if(ret == -1){
          if( (errno == EINTR) or (errno == EAGAIN) ){
            //recoverable error, try again
            continue;
          }
          TESTERROR("poll() reported an error");
        }

        /* check that there is no data */
        bool is_data = (ret == 1) && (pfd.revents & POLLIN);
        TESTASSERT(not is_data);
      }
    }
  }

}


/* test initiation of communication by the Connection's peer and a short exchange of
 * packets
 */
TESTFUNC(Connection_init_from_peer_and_talk)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  // the simulated peer uses segment number 1
  init_from_peer(conn_etc, conn_state, conn_msgnums, 1);

  for(int i=0; i<100; i++){
    send_data_into_conn(conn_etc, conn_state,(i%30)+1);
    send_data_from_conn(conn_etc, conn_state,conn_msgnums,(i%30)+1);
  }
}


/* test initiation of communication by the Connection and a short exchange of
 * packets
 */
TESTFUNC(Connection_init_from_conn_and_talk)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  // the simulated peer uses segment number 1
  init_from_conn(conn_etc,conn_state, conn_msgnums,1);

  for(int i=0; i<100; i++){
    send_data_from_conn(conn_etc, conn_state,conn_msgnums,(i%30)+1);
    send_data_into_conn(conn_etc, conn_state,(i%30)+1);
  }
}


/* test that the Connection can correctly accept packets which arrive out
 * of order (by message number)
 */
TESTFUNC(Connection_packets_reordered)
{
  /* This test only uses message numbers in the range 0-255, as this represents
   * one block of message numbers for the Connection's CryptoMessageTracker.
   * This ensures that the Connection will accept a valid packet whose message
   * number has not been seen. Note that this test is not aimed at testing the
   * correct functioning of the Connection's CryptoMessageTracker, as that is
   * tested in the CryptoMessageTracker's own tests.
   *
   * It is of course not ideal to build this implementation detail of
   * CryptoMessageTracker into this test, but it seems the best option. If the
   * block size of CryptoMessageTracker is reduced below 256, this test may
   * not work correctly.
   */

  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  // initiate communication, with the simulated peer using segment number 1
  init_from_peer(conn_etc, conn_state, conn_msgnums, 1);

  /* The test works by creating a vector of out-of-order message numbers
   * (without duplicates) and then working through the vector, sending messages
   * with the listed message numbers. We generate the list of message numbers
   * by choosing a list of "centres", and then for each centre we generate a
   * list of message numbers near that centre by adding a fixed list of "offsets".
   * All of these lists are then concatenated.
   */
  std::vector<CryptoMessageTracker::msgnum_t> centres =
    {200,100,150,125,101,95,250,110,30,50,70,20,40,60};
  std::vector<int> offsets =
    {0,20,-20,30,29,27,-19,-18,26,25,1,5,7,4,
     3,13,31,32,33,-31,-6,-7,-5,-8};
  std::vector<CryptoMessageTracker::msgnum_t> peer_msgnums;
  auto insert_num = [&](CryptoMessageTracker::msgnum_t n)
  {
    /* function to check that a message number is in the range
       [2,255] and that it is not already in peer_msgnums, and if
       so to add it to peer_msgnums */
    bool n_good = (n > 1) and (n < 256) and \
      ( std::find(peer_msgnums.begin(),peer_msgnums.end(),n) ==
        peer_msgnums.end() );
    if(n_good){
      peer_msgnums.push_back(n);
    }
  };
  for(auto x : centres){
    auto y = [&](CryptoMessageTracker::msgnum_t n){insert_num(x+n);};
    std::for_each(offsets.begin(),offsets.end(),y);
  }

  /* for each message number in our list, send a packet with that number and
     check that the Connection accepts the data it contains */
  auto it = peer_msgnums.begin();
  while(it != peer_msgnums.end()){
    conn_state.peer_next_msgnum = *it;
    send_data_into_conn(conn_etc,conn_state,29);
    it++;
  }

}


/* test that the Connection works correctly when its peer changes to a
   new segment number */
TESTFUNC(Connection_peer_change_segnum)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state_1;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  // initiate communication with the simulated peer using segment number 1
  init_from_peer(conn_etc, conn_state_1, conn_msgnums, 1);

  // talk back and forth for a bit
  for(int i=0; i<100; i++){
    send_data_into_conn(conn_etc, conn_state_1,(i%30)+1);
    send_data_from_conn(conn_etc, conn_state_1,conn_msgnums,(i%30)+1);
  }

  // the simulated peer will change to segment number 5
  ConnState conn_state_2;
  conn_state_2.conn_segnum = conn_state_1.conn_segnum;
  conn_state_2.peer_segnum = 5;
  conn_state_2.peer_next_msgnum = 1;

  // send new segment number with some data
  send_data_into_conn(conn_etc,conn_state_2,21);
  /* check that the Connection uses new segment number when sending to
     the peer */
  send_data_from_conn(conn_etc,conn_state_2,conn_msgnums,21);
  /* send from the simulated peer using the old segment number, and check
     that the packet is accepted */
  send_data_into_conn(conn_etc,conn_state_1,21);
  /* check that the Connection still uses new the new segment number when
     sending to the peer */
  send_data_from_conn(conn_etc,conn_state_2,conn_msgnums,21);
  // check again that we can send with the new segment number
  send_data_into_conn(conn_etc,conn_state_2,21);
}


/* test that the Connection correctly handles a restart by its peer */
TESTFUNC(Connection_peer_restart)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  // initiate the communication with the simulated peer using segment number 1
  init_from_peer(conn_etc,conn_state,conn_msgnums,1);

  // talk back and forth a bit
  for(int i=0; i<100; i++){
    send_data_into_conn(conn_etc, conn_state,(i%30)+1);
    send_data_from_conn(conn_etc, conn_state,conn_msgnums,(i%30)+1);
  }

  // re-initiate communication as if we've restarted
  init_from_peer(conn_etc,conn_state,conn_msgnums,2);
  /* send data via network (this confirms our (new) segment number to
     the Connection) */
  send_data_into_conn(conn_etc,conn_state,21);
  /* send data through the Connection to ensure connection uses our new
     segment number */
  send_data_from_conn(conn_etc, conn_state,conn_msgnums,21);
}


/* test that the Connection works correctly when the peer sends several
 * "hello" packets to initiate communication
 */
TESTFUNC(Connection_peer_repeats_hello)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  /* repeatedly send a "hello" packet to initiate communication, and check
     that the Connection sends the correct response. Note that
     init_from_peer() sends a "hello" packet and checks the response, but
     does not send any further packet to the Connection which would "confirm"
     our segment number, so the Connection should respond to each resent "hello"
     packet as though it were the first. */
  for(int i=0; i<10; i++){
    init_from_peer(conn_etc,conn_state,conn_msgnums,1);
  }

  /* talk back and forth a bit */
  for(int i=0; i<100; i++){
    send_data_into_conn(conn_etc, conn_state,(i%30)+1);
    send_data_from_conn(conn_etc, conn_state,conn_msgnums,(i%30)+1);
  }
}


/* test that the Connection correctly resends "hello" packets to initiate
 * communication when the peer does not respond
 */
TESTFUNC(Connection_7)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  conn_state.conn_segnum=0;
  conn_state.peer_segnum=1;
  conn_state.peer_next_msgnum=1;

  /* we cause the Connection to try initiating communication by writing some data
     to the Connection's input FIFO */
  std::vector<unsigned char> data = make_data(17);
  write_to_fifo(conn_etc.from_user_fifo_fd,data);

  /* repeatedly call the Connection's move_data() and check that it sends a "hello"
     packet each time */
  OpenedPacket op;
  for(int i=0; i<10; i++)
  {
    conn_etc.conn->move_data(1);
    op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,0,0,conn_msgnums,{});
  }

  /* respond to the Connection's "hello" packet to allow it to send data,
     and check that the expected data does get sent */
  conn_state.conn_segnum = op.send_segnum;
  create_and_send_good_packet(conn_etc,conn_state,{});
  op = get_packet_from_socket(conn_etc,100);
  check_packet(op,conn_etc,conn_state.peer_segnum,conn_state.conn_segnum,
               conn_msgnums,data);

  /* talk back and forth a bit */
  for(int i=0; i<100; i++){
    send_data_from_conn(conn_etc, conn_state,conn_msgnums,(i%30)+1);
    send_data_into_conn(conn_etc, conn_state,(i%30)+1);
  }
}


/* check that Connection::move_data() functions correctly during an initiation
 * of communication from the peer and subsequent data exchanges
 */
TESTFUNC(Connection_is_data_1)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  conn_state.conn_segnum=0;
  conn_state.peer_segnum=1;
  conn_state.peer_next_msgnum=1;

  /* send a "hello" packet to the Connection to initiate communications,
     checking at each point that is_data() gives the correct result */
  TESTASSERT(not conn_etc.conn->is_data());
  create_and_send_good_packet(conn_etc,conn_state,{},false);
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());

  /* check that the Connection send a valid response, and extract the
     Connection's segment number */
  OpenedPacket op = get_packet_from_socket(conn_etc,100);
  conn_state.conn_segnum = op.send_segnum;

  /* send a packet with data to the Connection and read the data out
     of the Connection's FIFO, while checking the return value of
     is_data() at each stage */
  create_and_send_good_packet(conn_etc,conn_state,make_data(17),false);
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  read_from_fifo(conn_etc.to_user_fifo_fd,17);

  /* write data to the Connection's FIFO so that it will be sent out
     in a network packet, while checking the return value of is_data()
     at each stage */
  write_to_fifo(conn_etc.from_user_fifo_fd,make_data(17));
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  get_packet_from_socket(conn_etc,100);
}


/* check that Connection::move_data() functions correctly during an initiation
 * of communication from the Connection and subsequent data exchanges
 */
TESTFUNC(Connection_is_data_2)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  conn_state.conn_segnum=0;
  conn_state.peer_segnum=1;
  conn_state.peer_next_msgnum=1;

  /* write data to the Connection's FIFO and call move_data() to
     trigger the sending of a "hello" packet from the Connection,
     checking the return of is_data() at each step */
  TESTASSERT(not conn_etc.conn->is_data());
  write_to_fifo(conn_etc.from_user_fifo_fd,make_data(17));
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());

  /* get the packet which the Connection sent and extract
     the Connection's segment number */
  OpenedPacket op = get_packet_from_socket(conn_etc,100);
  conn_state.conn_segnum = op.send_segnum;

  /* send a response packet with the simulated peer's segment number
     and then get the packet from the Connection containing the data
     which was put into the FIFO, checking the return of is_data() at
     each step */
  create_and_send_good_packet(conn_etc,conn_state,{},false);
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  get_packet_from_socket(conn_etc,100);

  /* write more data into the Connection's FIFO and monitor the
     return value of is_data() while the Connection handles this
     data */
  write_to_fifo(conn_etc.from_user_fifo_fd,make_data(17));
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  get_packet_from_socket(conn_etc,100);

  /* send a packet of data to the Connection from the simulated peer
     and monitor the return value of is_data() while the Connection
     handles this data */
  create_and_send_good_packet(conn_etc,conn_state,make_data(17),false);
  do_pause();
  TESTASSERT(conn_etc.conn->is_data());
  conn_etc.conn->move_data(1);
  do_pause();
  TESTASSERT(not conn_etc.conn->is_data());
  read_from_fifo(conn_etc.to_user_fifo_fd,17);
}


/* check that the Connection does not respond to cryptographically invalid
 * "hello" packets
 */
TESTFUNC(Connection_bad_hello_packet)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  conn_state.conn_segnum=0;
  conn_state.peer_segnum=1;
  conn_state.peer_next_msgnum=1;

  /* create a valid "hello" packet, which has no data payload */
  std::vector<unsigned char> packet_data =
    make_packet(conn_etc.conn_id,
                conn_etc.channel_id,
                conn_state.conn_segnum,
                conn_state.peer_segnum,
                conn_state.peer_next_msgnum++,
                {},
                conn_etc.crypto);

  /* We shall modify various different bytes of the packet to check that
     tampering or corruption is detected. byte_offsets holds the offsets of
     the bytes in packet_data that we shall modify */
  std::vector<std::vector<unsigned char>::size_type> byte_offsets = {
    6,  // first byte of receiver segment number
    12, // first byte of sender segment number
    18, // first byte of message number
    24, // first byte of AEAD tag
    39  // last byte of AEAD tag
  };
  for(auto i : byte_offsets){
    /* Note that, as per the C++ standard, unsigned types do not have overflow,
       and operations are performed modulo 2^N where N is the number of bits in
       the type. */
    packet_data[i] += 1;
    check_no_action(conn_etc,packet_data); // check that no response is sent
    packet_data[i] -= 1;
  }

  // send the correct packet
  conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                conn_etc.socket_fd_bound_port});
  conn_etc.conn->move_data(1);

  /* get the Connection's response and retrieve the Connection's segment number */
  OpenedPacket op = get_packet_from_socket(conn_etc,100);
  conn_state.conn_segnum = op.send_segnum;

  /* send a packet to the Connection to allow it to confirm our segment number
     and to check that the data is accepted correctly */
  send_data_into_conn(conn_etc,conn_state,17);

  // resend the correct packet and check that it gets no response
  check_no_action(conn_etc,packet_data);
}


/*  check that the Connection will not accept a cryptographically invalid
    packet sent before it has confirmed the peer's segment number */
TESTFUNC(Connection_bad_empty_response_packet)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  conn_state.conn_segnum=0;
  conn_state.peer_segnum=1;
  conn_state.peer_next_msgnum=1;

  /* cause the Connection in conn_etc to send a "hello" packet by writing
     data to the Connection's input FIFO */
  std::vector<unsigned char> data = make_data(17);
  write_to_fifo(conn_etc.from_user_fifo_fd,data);
  conn_etc.conn->move_data(1);

  /* get the "hello" packet from the socket and extract the Connection's
     segment number */
  OpenedPacket op = get_packet_from_socket(conn_etc,100);
  check_packet(op,conn_etc,0,0,conn_msgnums,{});
  conn_state.conn_segnum = op.send_segnum;

  /* create a valid packet with no data payload */
  std::vector<unsigned char> packet_data =
    make_packet(conn_etc.conn_id,
                conn_etc.channel_id,
                conn_state.conn_segnum,
                conn_state.peer_segnum,
                conn_state.peer_next_msgnum++,
                {},
                conn_etc.crypto);

  /* We shall modify various different bytes of the packet to check that
     tampering or corruption is detected. byte_offsets holds the offsets of
     the bytes in packet_data that we shall modify */
  std::vector<std::vector<unsigned char>::size_type> byte_inds = {
    6,  // first byte of receiver segment number
    12, // first byte of sender segment number
    18, // first byte of message number
    24, // first byte of AEAD tag
    39  // last byte of AEAD tag
  };
  for(auto i : byte_inds){
    /* Note that, as per the C++ standard, unsigned types do not have overflow,
       and operations are performed modulo 2^N where N is the number of bits in
       the type. */
    packet_data[i] += 1;
    conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                  conn_etc.socket_fd_bound_port});
    packet_data[i] -= 1;
    conn_etc.conn->move_data(1);

    /* check that the Connection sends another "hello" packet */
    OpenedPacket op = get_packet_from_socket(conn_etc,100);
    check_packet(op,conn_etc,0,0,conn_msgnums,{});
  }

  /* send the valid packet and check that the Connection then sends a packet
     containing the original data */
  conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                conn_etc.socket_fd_bound_port});
  conn_etc.conn->move_data(1);
  op = get_packet_from_socket(conn_etc,100);
  check_packet(op,conn_etc,conn_state.peer_segnum,conn_state.conn_segnum,
               conn_msgnums,data);
}


/* check that the Connection does not accept cryptographically invalid data packets */
TESTFUNC(Connection_bad_data_packets)
{
  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  /* set up communication with the Connection in conn_etc and send data to allow it to
     confirm our segment number */
  init_from_peer(conn_etc, conn_state, conn_msgnums, 1);
  send_data_into_conn(conn_etc, conn_state,17);

  /* create a valid packet with a data payload */
  std::vector<unsigned char> data = make_data(12);
  std::vector<unsigned char> packet_data =
    make_packet(conn_etc.conn_id,
                conn_etc.channel_id,
                conn_state.conn_segnum,
                conn_state.peer_segnum,
                conn_state.peer_next_msgnum++,
                data,
                conn_etc.crypto);

  /* We shall modify various different bytes of the packet to check that
     tampering or corruption is detected. byte_offsets holds the offsets of
     the bytes in packet_data that we shall modify */
  std::vector<std::vector<unsigned char>::size_type> byte_inds = {
    6,  // first byte of receiver segment number
    12, // first byte of sender segment number
    18, // first byte of message number
    24, // first byte of ciphertext
    23+data.size(), // last byte of ciphertext
    24+data.size(), // first byte of AEAD tag
    39+data.size()  // last byte of AEAD tag
  };
  for(auto i : byte_inds){
    /* Note that, as per the C++ standard, unsigned types do not have overflow,
       and operations are performed modulo 2^N where N is the number of bits in
       the type. */
    packet_data[i] += 1;
    /* check_no_action() checks that the data in the packet does not get accepted
       and written to the Connection's output FIFO */
    check_no_action(conn_etc,packet_data);
    packet_data[i] -= 1;
  }

  /* send the valid packet, and check that it is accepted */
  conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                  conn_etc.socket_fd_bound_port});
  conn_etc.conn->move_data(1);
  std::vector<unsigned char> fifo_data =
    read_from_fifo(conn_etc.to_user_fifo_fd,data.size());
  TESTASSERT(fifo_data == data);
}


/* check that the Connection rejects replayed packets */
TESTFUNC(Connection_packet_replay)
{
  /* This test only uses message numbers in the range 0-255, as this represents
   * one block of message numbers for the Connection's CryptoMessageTracker.
   * This ensures that the Connection will accept a valid packet whose message
   * number has not been seen. Note that this test is not aimed at testing the
   * correct functioning of the Connection's CryptoMessageTracker, as that is
   * tested in the CryptoMessageTracker's own tests.
   *
   * It is of course not ideal to build this implementation detail of
   * CryptoMessageTracker into this test, but it seems the best option. If the
   * block size of CryptoMessageTracker is reduced below 256, this test may
   * not work correctly.
   */

  ConnectionAndRelated conn_etc = create_connection();
  ConnState conn_state;
  std::set<CryptoMessageTracker::msgnum_t> conn_msgnums;

  /* set up communication with the Connection in conn_etc and send data to allow it to
     confirm our segment number */
  init_from_peer(conn_etc, conn_state, conn_msgnums, 1);
  send_data_into_conn(conn_etc, conn_state,17);

  /* we have a list of message numbers which the peer will use, which is intentionally
     out of order */
  std::vector<CryptoMessageTracker::msgnum_t> peer_msgnums =
    {200,199,201,198,202,195,205,190,192,210,211,209,
     10,12,17,9,14,222,8,197,250,150,50,170,255,4};
  // "packets" will be used to store generated packets for replaying
  std::vector<std::vector<unsigned char>> packets;

  /* for each of our message numbers, we create a packet with that message number which
     contains some data, send it to the Connection and test that it *is* accepted, then
     send it again and test that it is *not* accepted */
  for(CryptoMessageTracker::msgnum_t x : peer_msgnums){
    std::vector<unsigned char> data = make_data(23);
    std::vector<unsigned char> packet_data =
      make_packet(conn_etc.conn_id,
                  conn_etc.channel_id,
                  conn_state.conn_segnum,
                  conn_state.peer_segnum,
                  x,
                  data,
                  conn_etc.crypto);
    packets.push_back(packet_data); // store the packet for use in the final replay test
                                    // below

    /* give the packet to the Connection in conn_etc and check that it is accepted */
    conn_etc.conn->add_message(ReceivedUDPMessage{true,packet_data,"127.0.0.1",
                                                  conn_etc.socket_fd_bound_port});
    conn_etc.conn->move_data(1);
    std::vector<unsigned char> fifo_data =
      read_from_fifo(conn_etc.to_user_fifo_fd,data.size());
    TESTASSERT(fifo_data == data);

    /* give the packet to the Connection a second time and check that it is not accepted */
    check_no_action(conn_etc,packet_data);
  }

  /* replay all of the packets we send and check that they are not accepted */
  for(std::vector<unsigned char> packet_data : packets){
    check_no_action(conn_etc,packet_data);
  }
}

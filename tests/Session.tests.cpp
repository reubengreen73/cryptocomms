#include "testsys.h"
#include "../Session.h"
#include "../IDTypes.h"
#include "../PeerConfig.h"
#include "../SecretKey.h"

#include <string>
#include <memory>
#include <random>
#include <stdexcept>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h> // for in_port_t
#include <errno.h>

/* This file contains tests for Session. Because Session is the main interface to the
 * cryptocomms functionality, the tests here are in effect integration tests for the
 * whole system. The method of testing is simple: we create some Sessions and put data
 * through them.
 */

/* SessionAndFDs stores a Session together with file descriptors for its fifos */
struct SessionAndFDs
{
  std::unique_ptr<Session> sess;
  std::map<channel_id_type,int> to_user_fifos;
  std::map<channel_id_type,int> from_user_fifos;
  void close_all()
  {
    for(auto& x : to_user_fifos){
      close(x.second);}
    for(auto& x : from_user_fifos){
      close(x.second);}
  }
};


/* convenience method for making a Session and opening its fifos */
SessionAndFDs make_session(const host_id_type& self_id,
                           const std::string& self_ip_addr,
                           in_port_t self_port,
                           unsigned int default_max_packet_size,
                           const std::vector<PeerConfig>& peer_configs,
                           const std::string& segnum_file_path,
                           unsigned int num_connection_workers)
{
  SessionAndFDs session_and_fds;
  session_and_fds.sess = std::make_unique<Session>(self_id, self_ip_addr, self_port,
                                                   default_max_packet_size, peer_configs,
                                                   segnum_file_path, num_connection_workers);

  /* open the fifos for the session
   * note that the hard-coded "_OUTWARD" and "_INWARD" here need to be kept in sync with
   * the vaules in Connection.cpp (this is not great and should really be changed somehow)
   */
  for(const PeerConfig& pc : peer_configs){
    for(const channel_spec& cs: pc.channels){
      std::string from_user_fifo_name = cs.second+"_OUTWARD";
      int from_user_fifo_fd = open(from_user_fifo_name.c_str(),O_WRONLY);
      TESTASSERT( from_user_fifo_fd != -1 );
      session_and_fds.from_user_fifos.insert({cs.first,from_user_fifo_fd});

      std::string to_user_fifo_name = cs.second+"_INWARD";
      int to_user_fifo_fd = open(to_user_fifo_name.c_str(),O_RDONLY);
      TESTASSERT( to_user_fifo_fd != -1 );
      session_and_fds.to_user_fifos.insert({cs.first,to_user_fifo_fd});
    }
  }

  return session_and_fds;
}


/* TestBytes is used to create a stream of bytes for testing a connection. The usage is
 * simple: repeatedly pull a chunk of bytes out of the stream using TestBytes::take_bytes(),
 * and pass them into the connection you are testing. Then pull some bytes out of the other
 * end of the connection, and pass them to TestBytes::give_bytes(). give_bytes() keeps track
 * of the bytes that it has seen, and will return true if the bytes you pass it are indeed
 * the next unseen bytes from the stream, and false otherwise.
 */
class TestBytes
{
public:
  TestBytes();
  std::vector<unsigned char> take_bytes(unsigned int size);
  bool give_bytes(const std::vector<unsigned char>& bytes);
private:
  std::array<unsigned char,4> seed_;
  unsigned int take_pos_;
  unsigned int give_pos_;
  std::vector<unsigned char> gen_bytes(unsigned int pos, unsigned int size);
};

TestBytes::TestBytes():
  take_pos_(0), give_pos_(0)
{
  /* create a random seed for the data stream */
  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<std::mt19937::result_type> dist(0,255);
  for(unsigned int i=0; i<seed_.size(); i++){
    seed_[i] = dist(rng);}
}

std::vector<unsigned char> TestBytes::take_bytes(unsigned int size)
{
  std::vector<unsigned char> bytes = gen_bytes(take_pos_,size);
  take_pos_ += size;
  return bytes;
}

bool TestBytes::give_bytes(const std::vector<unsigned char>& bytes)
{
  std::vector<unsigned char> expected_bytes = gen_bytes(give_pos_,bytes.size());
  give_pos_ += bytes.size();
  return(bytes == expected_bytes);
}

/* TestBytes::gen_bytes() returns "size" bytes from the stream, starting at "pos".
 * The algorithm used to generate the stream is simple: in every chunk of 8 bytes,
 * the first, third, and fifth bytes record the position in the stream modulo 65536,
 * while the others are kinda-pseudo-random-ish-ly generated from the seed and the
 * position via a simple polynomial formula.
 */
std::vector<unsigned char> TestBytes::gen_bytes(unsigned int pos, unsigned int size)
{
  std::vector<unsigned char> bytes(size);
  for(unsigned int i=0;i<size;i++){
    unsigned int j = pos+i;
    if( (j%8) == 0 ){
      bytes[i] = (j/(256*256))%256;}
    else if( (j%8) == 2 ){
      bytes[i] = (j/256)%256;}
    else if( (j%8) == 4 ){
      bytes[i] = j%256;}
    else{
      unsigned int x = seed_[0];
      for(int k=1; k<4; k++){
        x  = ( (seed_[k]+x)*j )%256;
      }
      bytes[i]=x;
    }
  }
  return bytes;
}

/* Writes num_bytes of data into write_fd and checks that the same bytes come out of read_fd.
 * Writes chunk_size bytes at a time and then checks that they come out before writing the
 * next chunk.
 */
bool move_data(int write_fd, int read_fd, unsigned int num_bytes, unsigned int chunk_size)
{
  TestBytes test_bytes;
  unsigned int bytes_done = 0;

  while(bytes_done < num_bytes){
    /* get the bytes to send in this iteration */
    std::vector<unsigned char> send_chunk =
      test_bytes.take_bytes(std::min(chunk_size,num_bytes-bytes_done));

    /* write the bytes to write_fd */
    unsigned int bytes_written = 0;
    while(bytes_written < send_chunk.size()){
      int ret = write(write_fd,send_chunk.data()+bytes_written,send_chunk.size()-bytes_written);
      if(ret == -1){
        if(errno == EINTR){
          ret=0;}
        else{
          throw std::runtime_error("Error: could not write");}
      }
      bytes_written += ret;
    }

    /* Repeatedly pull some bytes out of read_fd and check that these are the expected
       next bytes in our stream. Keep doing this until the whole chunk that was written
       to write_fd has been recovered. */
    unsigned int bytes_read = 0;
    std::vector<unsigned char> recv_chunk(send_chunk.size());
    while(bytes_read < send_chunk.size()){

      /* read some bytes... */
      int ret = read(read_fd,recv_chunk.data(),send_chunk.size()-bytes_read);
      if(ret == -1){
        if(errno == EINTR){
          ret=0;}
        else{
          throw std::runtime_error("Error: could not read");}
      }

      /* ...and check that they are the bytes we expected */
      std::vector<unsigned char> read_bytes(recv_chunk.begin(),recv_chunk.begin()+ret);
      if(not test_bytes.give_bytes(read_bytes)){
        return false;}

      bytes_read += ret;
    }

    bytes_done += send_chunk.size();
  }

  return true;
}


/* this test creates two sessions and checks that they can communicate correctly */
TESTFUNC(Session_monolithic)
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

  SessionAndFDs host_A = make_session(host_A_id,
                                      ip_addr,host_A_port,
                                      max_packet_size,
                                      {host_B_peer_config},
                                      segnum_file_name,
                                      5);
  SessionAndFDs host_B = make_session(host_B_id,
                                      ip_addr,host_B_port,
                                      max_packet_size,
                                      {host_A_peer_config},
                                      segnum_file_name,
                                      5);

  int write_fifo_fd = host_A.from_user_fifos[channel_id];
  int read_fifo_fd = host_B.to_user_fifos[channel_id];

  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<std::mt19937::result_type> dist100(-100,100);

  /* repeatedly test our connection with different amounts of data and different
     sizes of chunk to be written to the connection at a time */
  for(int i=0; i<100; i++){
    unsigned int num_bytes = 500'000 +(dist100(rng)*100)+dist100(rng);
    unsigned int chunk_size = 1000+dist100(rng);
    TESTASSERT( move_data(write_fifo_fd,read_fifo_fd,num_bytes,chunk_size) );
  }

  host_A.close_all();
  host_B.close_all();
}

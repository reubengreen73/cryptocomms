#include "testsys.h"
#include "../FifoIO.h"

#include <string>
#include <stdexcept>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

/* test that a failure to create a fifo generates the correct error */
TESTFUNC(FifoIO_cannot_create_fifo)
{
  std::string testdir = "test_dir";
  std::string testfifo = "test_fifo";

  // create a directory where we cannot write
  int res = mkdir(testdir.c_str(),S_IRUSR|S_IRGRP|S_IROTH|S_IXUSR|S_IXGRP|S_IXOTH);
  if(res == -1){
    throw std::runtime_error("Test error: could not create directory for testing");
  }

  TESTTHROW(FifoFromUser((testdir+"/"+testfifo).c_str()),"could not create FIFO at");
}


/* test that a failure to stat a fifo generates the correct error */
TESTFUNC(FifoIO_cannot_stat_fifo)
{
  std::string testdir = "test_dir";
  std::string testfifo = "test_fifo";

  // create a directory where we cannot stat
  int res = mkdir(testdir.c_str(),S_IRUSR|S_IRGRP|S_IROTH);
  if(res == -1){
    throw std::runtime_error("Test error: could not create directory for testing");
  }

  TESTTHROW(FifoFromUser(testdir+"/"+testfifo),"could not stat file at");
}


/* test that a non-fifo file where the fifo should be generates the correct error */
TESTFUNC(FifoIO_file_not_fifo)
{
  std::string filename = "test_not_fifo";
  int fd = open(filename.c_str(), O_CREAT|O_WRONLY);
  if(fd == -1){
    throw std::runtime_error("Test error: could not create file "+filename);
  }
  close(fd);

  TESTTHROW(FifoFromUser{filename},"is not a FIFO");

}


/* test that opening a read-only fifo for writing generates the correct error */
TESTFUNC(FifoIO_read_only_fifo_to_user)
{
  std::string filename = "read_only_fifo";

  if(mkfifo(filename.c_str(),S_IRUSR|S_IRGRP|S_IROTH) == -1){
    throw std::runtime_error("Test error: could not create fifo "+filename);
  }

  TESTTHROW(FifoToUser{filename},"could not open "+filename);
}


/* test that using a FifoToUser or a FifoFromUser after a move generates the
 * correct error
 */
TESTFUNC(FifoIO_use_after_move)
{
  std::string fifo_name = "testfifo";

  /* first, create a connected FifoFromUser/FifoToUser pair... */
  FifoFromUser ffu1{fifo_name};
  FifoToUser ftu1{fifo_name};

  /* ...and check that the connection works */
  std::vector<unsigned char> data1 = {1,2,3,4,5};
  std::pair<unsigned int, bool> write_res = ftu1.write(data1);
  TESTASSERT( write_res.first == 5 );
  TESTASSERT( write_res.second == false );
  std::vector<unsigned char> data2 = ffu1.read(5);
  TESTASSERT( data1 == data2 );

  /* we then move from both the FifoFromUser and the FifoToUser, and
   * check that reading or writing on them produces the correct error
   */
  FifoToUser ftu2{std::move(ftu1)};
  FifoFromUser ffu2{std::move(ffu1)};

  TESTTHROW( ftu1.write(data1), "FifoToUser write after move" );
  TESTTHROW( data2 = ffu1.read(5), "FifoFromUser read after move" );
}


/* check that reading and writing work correctly */
TESTFUNC(FifoIO_fifo_read_and_write)
{
  std::string fifo_name = "testfifo";

  /* create a connected FifoFromUser/FifoToUser pair */
  FifoFromUser ffu{fifo_name};
  FifoToUser ftu{fifo_name};

  std::vector<unsigned char> data1 = {1,2,3,4,5};
  std::pair<unsigned int, bool> write_res = ftu.write(data1);
  TESTASSERT( write_res.first == 5 );
  TESTASSERT( write_res.second == false );
  std::vector<unsigned char> data2 = ffu.read(5);
  TESTASSERT( data1 == data2 );
}


/* test that reading from a disconnected FifoFromUser works as expected */
TESTFUNC(FifoIO_read_disconn_fifo)
{
  std::string fifo_name("test_fifo");
  FifoFromUser ffu{fifo_name};

  std::vector<unsigned char> data = ffu.read(1000);
  TESTASSERT( data.size() == 0 );

  data = ffu.read(10);
  TESTASSERT( data.size() == 0 );

  data = ffu.read(1);
  TESTASSERT( data.size() == 0 );
}


/* check that reading from a FifoFromUser when no data is available works
 * as expected
 */
TESTFUNC(FifoIO_fifo_read_no_data)
{
  std::string fifo_name = "testfifo";

  /* create a connected fifo pair */
  FifoFromUser ffu{fifo_name};
  FifoToUser ftu{fifo_name};

  /* check that the connection is working */
  std::vector<unsigned char> data1 = {1,2,3,4,5};
  std::pair<unsigned int, bool> write_res = ftu.write(data1);
  TESTASSERT( write_res.first == 5 );
  TESTASSERT( write_res.second == false );
  std::vector<unsigned char> data2 = ffu.read(5);
  TESTASSERT( data1 == data2 );

  data2 = ffu.read(1000);
  TESTASSERT( data2.size() == 0 );

  data2 = ffu.read(10);
  TESTASSERT( data2.size() == 0 );

  data2 = ffu.read(1);
  TESTASSERT( data2.size() == 0 );
}

/* test that writing to a disconnected FifoToUser works as expected */
TESTFUNC(FifoIO_fifo_disconnected)
{
  std::string fifo_name("test_fifo");
  FifoToUser ftu{fifo_name};

  std::vector<unsigned char> data = {1,2,3,4,5};
  std::pair<unsigned int, bool> write_res = ftu.write(data);
  TESTASSERT( write_res.first == 0 );
  TESTASSERT( write_res.second == true );
}

#include "FifoIO.h"

/* Note that we use the C POSIX interface exclusively in this file. In particular, for
 * functionality from the C standard library we use the POSIX compatible C headers like
 * signal.h rather than csignal, and we use names in the global namespace rather than std::
 * It seems better to be consistent with POSIX rather than mixing and matching the two
 * standards.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

namespace
{
  /* this enum is used as an argument to open_fifo() below */
  enum class FifoMode{
    read,
    write
  };

  /* open_fifo() opens a fifo for non-blocking reading or writing at the specified path,
   * creating the fifo first if necessary. Whether the fifo is opened for reading or
   * writing is controlled by the mode parameter.
   *
   * Careful checking is performed to ensure that the file descriptor returned really does
   * represent a fifo. This function is used by the primary constructor of both FifoFromUser
   * and FifoToUser.
   *
   * The return value of open_fifo() is always a valid file descriptor on a fifo at "path",
   * opened with mode O_RDONLY|O_NONBLOCK for "read" and O_WRONLY|O_NONBLOCK for "write".
   */
  int open_fifo(const std::string& path, FifoMode mode)
  {
    /* Check if there is already a file at path. If so, check that it is a fifo.
     * If not, create a fifo there with suitable permissions.
     */
    struct stat stat_info;
    int res = stat(path.c_str(),&stat_info);
    if( (res == -1) && (errno == ENOENT) ){
      /* no file at path, so create a fifo */
      if(mkfifo(path.c_str(), S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH) == -1){
        throw std::runtime_error("could not create FIFO at "+path);
      }
    }
    else if(res == -1){
      throw std::runtime_error("could not stat file at "+path);
    }
    else{
      /* there is a file at path, so check that it is a fifo */
      if( (stat_info.st_mode & S_IFIFO) == 0 ){
        throw std::runtime_error(path+" is not a FIFO");
      }
    }

    /* open the file at path for non-blocking reading or writing */
    int fd = -1;
    int open_flags = (mode == FifoMode::read) ?
      O_RDONLY|O_NONBLOCK : O_WRONLY|O_NONBLOCK;
    while(fd == -1){
      fd = open(path.c_str(), open_flags);
      if( (fd == -1) && (errno != EINTR) ){
        throw std::runtime_error("could not open "+path);
      }
    }

    /*  check that the file descriptor represents a fifo */
    if(fstat(fd,&stat_info) == -1){
      close(fd);
      throw std::runtime_error("could not stat file at "+path);
    }
    if( (stat_info.st_mode & S_IFIFO) == 0 ){
      close(fd);
      throw std::runtime_error(path+" is not a FIFO");
    }

    return fd;
  }

}


/* FifoFromUser() opens two file descriptors. fd_ is the file descriptor which will
 * actually be used to read from. write_fd_ is never used by the FifoFromUser itself,
 * but is needed to allow fd_ to be monitored by poll() in the way we want.
 *
 * Suppose FifoFromUser did not keep write_fd_ open at all times. In this situation,
 * whenever a user opened the fifo corresponding to fd_, wrote to it, and then closed it,
 * the fifo would be in a "disconnected" state (until/unless it was opened for writing
 * again). While the fifo is thus disconnected, any call to poll() with fd_ amongst the
 * file descriptors to monitor would return immediately with a POLLHUP event for fd_,
 * preventing us from using poll() to listen for incoming data. Keeping write_fd_
 * open prevents this.
 */
FifoFromUser::FifoFromUser(const std::string& path):
  path_(path)
{
  /* Note that we do not need to do any error handling with these file descriptors,
     as open_fifo() always either returns a valid file descriptor or else throws an
     error. */
  fd_ = open_fifo(path,FifoMode::read);
  write_fd_ = open_fifo(path,FifoMode::write);
}


FifoFromUser::FifoFromUser(FifoFromUser&& other)
{ *this = std::move(other); }


FifoFromUser& FifoFromUser::operator=(FifoFromUser&& other)
{
  if(this != &other){
    fd_ = other.fd_;
    other.fd_ = -1;
    write_fd_ = other.write_fd_;
    other.write_fd_ = -1;
  }
  return *this;
}


FifoFromUser::~FifoFromUser()
{
  if(fd_ != -1){
    close(fd_);
  }
  if(write_fd_ != -1){
    close(write_fd_);
  }
}

/* FifoFromUser::read() attempts to read up to count bytes from the
 * underlying fifo without blocking. Repeated attempts are made to read
 * from the fifo to get to the requested count, until either there is
 * no more data waiting in the fifo, or the write end of the fifo is closed.
 */
std::vector<unsigned char> FifoFromUser::read(unsigned int count)
{
  if(fd_ == -1){
    throw std::runtime_error("FifoIO: FifoFromUser read after move");
  }

  if(read_buff_.size() < count){
    read_buff_.resize(count);
  }

  /* keep making reads from the fifo into read_buff_ until one of the following
   * occurs: we get enough bytes; the read would block if it were a blocking fifo
   * (meaning that the write end of the fifo is open but there is no data to read);
   * the fifo is at end-of-file (meaning that the write end of the fifo is closed)
   */
  ssize_t total_read = 0;
  ssize_t ret;
  while(total_read < count){
    ret = ::read(fd_,read_buff_.data()+total_read,count-total_read);
    if(ret == -1){
      if(errno == EINTR){
        continue;
      }
      if(errno == EAGAIN){ // the read would block if fd_ were not O_NONBLOCK
        break;
      }
      throw FifoIOError("error reading from fifo "+path_);
    }
    if(ret == 0){ // end-of-file on fifo
      break;
    }
    total_read += ret;
  }

  return std::vector<unsigned char>(read_buff_.begin(),read_buff_.begin()+total_read);
}


int FifoFromUser::file_descriptor()
{
  return fd_;
}


/* FifoToUser::FifoToUser() opens a non-blocking fifo for writing. We have to work around the
 * restriction that POSIX does not allow us to open a fifo for writing unless it is already open
 * for reading. We do this by first opening the fifo for reading, then opening it for writing, and
 * then closing the reading file descriptor.
 *
 * The constructor also ensures that the the action for a SIGPIPE signal is to ignore. This is
 * necessary, as we want to be able to attempt to write to a fifo even if it might not be open
 * for reading.
 */
FifoToUser::FifoToUser(const std::string& path):
  path_(path)
{
  /* sigpipe_off_ is a static member of FifoToUser which is initialized to false */
  if(not sigpipe_off_){
    signal(SIGPIPE, SIG_IGN);
    sigpipe_off_ = true;
  }

  /* Note that we do not need to do any error handling with these file descriptors,
     as open_fifo() always either returns a valid file descriptor or else throws an
     error. */
  int fd = open_fifo(path,FifoMode::read);
  fd_ = open_fifo(path,FifoMode::write);
  close(fd);
}


FifoToUser::FifoToUser(FifoToUser&& other)
{ *this = std::move(other); }


FifoToUser& FifoToUser::operator=(FifoToUser&& other)
{
  if(this != &other){
    fd_ = other.fd_;
    other.fd_ = -1;
  }
  return *this;
}


FifoToUser::~FifoToUser()
{
  if(fd_ != -1){
    close(fd_);
  }
}


/* FifoToUser::write() attempts to write its argument data to the underlying fifo.
 * FifoToUser::write() repeatedly tries to perform the write, giving up only if
 * it detects that the fifo is not open for writing or that there is no room left
 * in the fifo.  FifoToUser::write() thus represents a best effort at delivering
 * the bytes contained in its argument data.
 *
 * FifoToUser::write() returns a pair. The first element reports how many bytes were
 * successfully written to the fifo, while the second element records if a broken pipe
 * was detected (this is useful for a caller who might want to retry the write later).
 */
std::pair<unsigned int,bool> FifoToUser::write(const std::vector<unsigned char>& data)
{
  if(fd_ == -1){
    throw std::runtime_error("FifoIO: FifoToUser write after move");
  }

  /* repeatedly try to write out the data, until either all data has been written, or
   * the fifo is found not to be open for reading
   */
  unsigned int total_written = 0;
  ssize_t ret;
  while(total_written < data.size()){
    ret = ::write(fd_,data.data()+total_written,data.size()-total_written);
    if(ret == -1){
      if(errno == EINTR){
        continue;
      }
      if(errno == EPIPE){
        // EPIPE indicates a "broken pipe", i.e. the fifo is not open for reading
        return {total_written,true};
      }
      if(errno == EAGAIN){
        // EAGAIN means the pipe is full
        break;
      }
      throw FifoIOError("error writing to fifo "+path_);
    }
    total_written += ret;
  }

  return {total_written,false};
}


int FifoToUser::file_descriptor()
{
  return fd_;
}


bool FifoToUser::sigpipe_off_ = false;

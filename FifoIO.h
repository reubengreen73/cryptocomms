/* FifoFromUser and FifoToUser are simple wrappers for the read and write ends
 * of a fifo (i.e. a named pipe), respectively. They also support getting the
 * underlying file descriptor of the fifo to allow poll() based non-blocking IO.
 */

#ifndef FIFOIO_H
#define FIFOIO_H

#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

class FifoFromUser
{
public:
  FifoFromUser(const std::string& path);
  FifoFromUser(FifoFromUser&& other);
  FifoFromUser& operator=(FifoFromUser&& other);
  ~FifoFromUser();

  /* We do not allow copying of a FifoFromUser, as the
   * file descriptor fd_ is not shareable, and moreover
   * there is no valid reason to copy a FifoFromuser.
   */
  FifoFromUser(const FifoFromUser& other) = delete;
  FifoFromUser& operator=(const FifoFromUser& other) = delete;

  std::vector<unsigned char> read(unsigned int count);
  int file_descriptor();

private:
  int fd_;
  int write_fd_; // see the comments before the definition of the
                 // parameterized constructor for the reason for
                 // write_fd_
  std::vector<unsigned char> read_buff_;
  const std::string path_;
};


class FifoToUser
{
public:
  FifoToUser(const std::string& path);
  FifoToUser(FifoToUser&& other);
  FifoToUser& operator=(FifoToUser&& other);
  ~FifoToUser();

  /* We do not allow copying of a FifoFromUser, as the
   * file descriptor fd_ is not shareable, and moreover
   * there is no valid reason to copy a FifoFromuser.
   */
  FifoToUser(const FifoToUser& other) = delete;
  FifoToUser& operator=(const FifoToUser& other) = delete;

  std::pair<unsigned int,bool> write(const std::vector<unsigned char>& data);
  int file_descriptor();

private:
  int fd_;
  static bool sigpipe_off_;
  const std::string path_;
};


class FifoIOError: public std::runtime_error{
public:
  FifoIOError(const std::string& what_arg): runtime_error(what_arg){}
};


#endif

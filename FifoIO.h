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
  FifoFromUser(const FifoFromUser& other) = delete;
  FifoFromUser& operator=(const FifoFromUser& other) = delete;
  ~FifoFromUser();

  std::vector<unsigned char> read(unsigned int count);
  int file_descriptor();

private:
  int _fd;
  std::vector<unsigned char> _read_buff;
  const std::string _path;
};


class FifoToUser
{
public:
  FifoToUser(const std::string& path);
  FifoToUser(FifoToUser&& other);
  FifoToUser& operator=(FifoToUser&& other);
  FifoToUser(const FifoToUser& other) = delete;
  FifoToUser& operator=(const FifoToUser& other) = delete;
  ~FifoToUser();

  std::pair<unsigned int,bool> write(const std::vector<unsigned char>& data);
  int file_descriptor();

private:
  int _fd;
  static bool _sigpipe_off;
  const std::string _path;
};


class FifoIOError: public std::runtime_error{
public:
  FifoIOError(const std::string& what_arg): runtime_error(what_arg){}
};


#endif

/* SegmentNumGenerator manages the creation and allocation of unique segment
 * numbers for use by Connections. The uniqueness of segment numbers is vital
 * for security, so SegmentNumGenerator uses both the system clock and a
 * persistent storage of used segment numbers to make segment number reuse as
 * unlikely as possible. The only public functionality exposed by this class
 * (aside from the constructor) are the functions next_num() and set_reserved(),
 * which are thread-safe.
 */

#ifndef SEGMENTNUMGENERATOR_H
#define SEGMENTNUMGENERATOR_H

#include <mutex>
#include <string>
#include <cstdint>

class SegmentNumGenerator
{
public:
  SegmentNumGenerator(std::string path, unsigned int reserved = 1000);
  std::uint_least64_t next_num();
  void set_reserved(unsigned int reserved);

private:
  std::string _path;
  std::mutex _lock;
  unsigned int _reserved;
  std::uint_least64_t _next_num;
  std::uint_least64_t _new_reserve_needed;
  void reserve_nums();
};

#endif

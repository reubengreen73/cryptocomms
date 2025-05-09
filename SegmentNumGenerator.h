/* SegmentNumGenerator manages the creation and allocation of unique segment
 * numbers for use by Connections. The uniqueness of segment numbers is vital
 * for security, so SegmentNumGenerator uses both the system clock and a
 * persistent storage of used segment numbers to make segment number reuse as
 * unlikely as possible. The only public functionality exposed by this class
 * (aside from the constructor) is the function next_num(), which is thread safe.
 */

#ifndef SEGMENTNUMGENERATOR_H
#define SEGMENTNUMGENERATOR_H

#include <mutex>
#include <string>
#include <cstdint>

class SegmentNumGenerator
{
public:
  SegmentNumGenerator(std::string path, int reserved);
  uint_least64_t next_num();

private:
  std::string _path;
  std::mutex _lock;
  int _reserved;
  uint_least64_t _next_num;
  uint_least64_t _new_reserve_needed;
  void reserve_nums();
  uint_least64_t get_saved_segnum();
  uint_least64_t get_segnum_sysclock();
  void save_segnum(uint_least64_t segnum);
};

#endif

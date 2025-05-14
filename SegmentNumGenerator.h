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
  SegmentNumGenerator(std::string path, uint reserved = 1000);
  uint_least64_t next_num();
  void set_reserved(uint reserved);

private:
  std::string _path;
  std::mutex _lock;
  uint _reserved;
  uint_least64_t _next_num;
  uint_least64_t _new_reserve_needed;
  void reserve_nums();
  uint_least64_t get_saved_segnum();
  uint_least64_t get_segnum_sysclock();
  void save_segnum(uint_least64_t segnum);
};

#endif

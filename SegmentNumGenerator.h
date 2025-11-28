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
  typedef std::uint_least64_t segnum_t;

  SegmentNumGenerator(std::string path, unsigned int reserved = 1000);
  segnum_t next_num();
  void set_reserved(unsigned int reserved);

  /* We explicitly delete the copy and move assignment/constructors. It
   * is vital for cryptographic security that segment numbers are never
   * reused, and banning copying/moving of a SegmentNumGenerator helps
   * protect against this.
   */
  SegmentNumGenerator(SegmentNumGenerator&& other) = delete;
  SegmentNumGenerator& operator=(SegmentNumGenerator&& other) = delete;
  SegmentNumGenerator(const SegmentNumGenerator& other) = delete;
  SegmentNumGenerator& operator=(const SegmentNumGenerator& other) = delete;

private:
  std::string path_first_;
  std::string path_second_;
  std::mutex lock_;
  unsigned int reserved_;
  segnum_t next_num_;
  segnum_t new_reserve_needed_;
  void reserve_nums();
};

#endif

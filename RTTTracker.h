/* stub version of RTTTracker which just uses simple averaging */

/* NOTE: we use std::uint_least32_t as the type for all round-trip times.
 * We use this rather than unsigned int since the C++ specification only
 * demands that unsigned int must be at least 16 bits. Round-trip times
 * are expressed in milliseconds, and (2^16 - 1) milliseconds is about 65
 * seconds. Since it is possible we might in very rare circumstances get a
 * round-trip of more than 65 seconds, we use the bigger type.
 */

#ifndef RTTTRACKER_H
#define RTTTRACKER_H

#include <cstdint>

class RTTTracker
{
 public:
  std::uint_least32_t current_rtt();
  void update_rtt(std::uint_least32_t rtt_measurement);

private:
  bool unused_ = true;
  std::uint_least32_t current_rtt_;
};

#endif

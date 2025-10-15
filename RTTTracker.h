/* stub version of RTTTracker which just uses simple averaging */

/* NOTE: we use unsigned long int as the type for all round-trip times.
 * We use this rather than unsigned int since the C++ specification demands
 * that unsigned long int be at least 32 bits, while unsigned int must only
 * be 16 bits. Round-trip times are expressed in milliseconds, and (2^16 - 1)
 * milliseconds is about 65 seconds. Since it is possible we might in very
 * rare circumstances get a round-trip of more than 65 seconds, we use the
 * bigger type.
 */

#ifndef RTTTRACKER_H
#define RTTTRACKER_H

class RTTTracker
{
 public:
  unsigned long int current_rtt();
  void update_rtt(unsigned long int rtt_measurement);

private:
  bool unused_ = true;
  unsigned long int current_rtt_;
};

#endif

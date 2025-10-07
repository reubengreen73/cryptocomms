/* stub version of RTTTracker which just returns 50 milliseconds */

#ifndef RTTTRACKER_H
#define RTTTRACKER_H

class RTTTracker
{
 public:
  unsigned int current_rtt()
  { return 50; }
};

#endif

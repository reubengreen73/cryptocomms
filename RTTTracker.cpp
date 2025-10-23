#include "RTTTracker.h"

std::uint_least32_t RTTTracker::current_rtt()
{ return current_rtt_; }

void RTTTracker::update_rtt(std::uint_least32_t rtt_measurement)
{
  if(unused_){
    current_rtt_ = rtt_measurement;
    unused_=false;
  }
  else{
    /* the simple formula used here is from the original TCP specification */
    std::uint_least64_t temp = current_rtt_-rtt_measurement;
    current_rtt_ = rtt_measurement + 0.9*temp;
  }
}

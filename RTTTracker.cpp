#include "RTTTracker.h"

unsigned long int RTTTracker::current_rtt()
{ return current_rtt_; }

void RTTTracker::update_rtt(unsigned long int rtt_measurement)
{
  if(unused_){
    current_rtt_ = rtt_measurement;
    unused_=false;
  }
  else{
    /* the simple formula used here is from the original TCP specification */
    long long int temp = current_rtt_-rtt_measurement;
    current_rtt_ = rtt_measurement + 0.9*temp;
  }
}

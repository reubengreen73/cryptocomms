#include "EpochTime.h"

#include <chrono>

/* return the number of milliseconds since the UNIX epoch */
millis_timestamp_t epoch_time_millis()
{
  auto now = std::chrono::system_clock::now();
  auto now_since_epoch = now.time_since_epoch();
  millis_timestamp_t millis_since_epoch =
    std::chrono::duration_cast<std::chrono::milliseconds>(now_since_epoch).count();
  return millis_since_epoch;
}

/* simple typedef and method for getting and holding the number of
 *  milliseconds since the UNIX epoch
*/

#ifndef EPOCH_TIME_H
#define EPOCH_TIME_H

#include <cstdint>

/* Using std::uint_least64_t to represent the number of milliseconds since
 * the epoch means that we can represent time points until the year
 * 584,544,015 CE, which I think will be sufficient for our needs.
 */
typedef std::uint_least64_t millis_timestamp_t;

millis_timestamp_t epoch_time_millis();

#endif

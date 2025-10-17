/* CryptoMessageTracker tracks which message numbers have been received and which have not
 * within a given segment. Message numbers need to fit in 6 bytes, and so are allowed to range
 * between 0 and (2^48 - 1) inclusive.
 *
 * Aside from the constructor, the public interface consists of the member functions reset(),
 * have_seen_msgnum(), and log_msgnum(), as well as the constants block_size and max_blocks.
 * The basic function of CryptoMessageTracker can be summarised as follows. Message numbers
 * are logged as having been seen with log_msgnum(), and one can query whether a message number
 * has been logged with have_seen_msgnum(). CryptoMessageTracker does not keep a complete log
 * of all message numbers logged for reasons of space and speed, so have_seen_msgnum() can
 * return true for message numbers which have not been logged.
 *
 * The results of have_seen_msgnum() satisfy the following conditions ("logging" a message
 * number msgnum means calling log_msgnum(msgnum) )
 *   >> If the message number msgnum has been logged, then have_seen_msgnum(msgnum) will
 *      be true
 *   >> There is a message number, msgnum_bound (which can increase at a call to log_msgnum() ),
 *      such that if msgnum >= msgnum_bound, then have_seen_msgnum(msgnum) will be true if and
 *      only if msgnum has been logged. See below for how to calculate msgnum_bound.
 *   >> For any message number msgnum, two successive calls have_seen_msgnum(msgnum) will
 *      return the same result unless there has been an intervening call log_msgnum(msgnum_higher)
 *      where msgnum_higher is higher than the previous highest message number passed to
 *      log_msgnum()
 * The message number msgnum_bound is calculated as follows. Let msgnum_highest be the highest
 * logged message number, and rtt_current be the round-trip time reported by rtt_tracker_ at the
 * point when msgnum_highest was logged. Let x be the smallest multiple of block_size which is
 * strictly greater than msgnum_highest, let y be ( x - (block_size*max_blocks) ), and let z be
 * the lowest message number such that both
 *   >> z was logged within rtt_current milliseconds before msgnum_highest was logged
 *   >> z >= y
 * Then msgnum_bound is the greatest multiple of block_size which is less than or equal to z.
 *
 * See the large comment later in this file for a description of the implementation of the
 * internal state of CryptoMessageTracker.
 */


#ifndef CRYPTOMESSAGETRACKER_H
#define CRYPTOMESSAGETRACKER_H

#include <cstdint>
#include <array>
#include <memory>
#include <vector>
#include <utility>

#include "RTTTracker.h"

class CryptoMessageTracker
{
public:
  /* We use uint_least64_t to store message numbers, as this ensures that there is room
   * for 48 bits (i.e. six bytes)
   */
  typedef std::uint_least64_t msgnum_t;

  /* The values block_size and max_blocks are public members because they feature in the
   * above-mentioned guarantees of which message numbers can have their status recalled
   * exactly.
   */
  static constexpr unsigned int block_size = 256;
  static constexpr unsigned int max_blocks = 64;

  CryptoMessageTracker(const std::shared_ptr<RTTTracker>& rtt_tracker);
  void reset();
  bool have_seen_msgnum(msgnum_t msgnum);
  void log_msgnum(msgnum_t msgnum);

private:
  /* Note the use of different types for arguments and return values below when
   * dealing with numbers of blocks. When dealing with numbers of _physical blocks_,
   * i.e. chunks of msg_records_, unsigned int is used (since this is the type of
   * max_blocks, which limits the number of physical blocks). When dealing with numbers
   * of _logical blocks_, i.e. chunks of the 48-bit space of message numbers,
   * std::uint_least64_t is used to ensure any block number can be represented.
   */
  typedef std::uint_least64_t millis_timestamp_t;
  std::vector<bool>::size_type records_pos(msgnum_t msgnum);
  unsigned int how_many_extra_blocks(std::uint_least64_t num_blocks_forward,
                                     millis_timestamp_t millis_since_epoch,
                                     unsigned long int current_rtt);
  void move_records_window(std::uint_least64_t num_blocks_forward);
  void reallocate_records(std::uint_least64_t num_blocks_forward,
                          unsigned int num_extra_blocks);

  std::shared_ptr<RTTTracker> rtt_tracker_;

  /* The following variables hold the internal state of CryptoMessageTracker,
   * see DESIGN at the top of CryptoMessageTracker.cpp for a discussion of their
   * use. */
  std::vector<std::pair<unsigned int,millis_timestamp_t>> block_records_;
  std::vector<bool> msg_records_;
  unsigned int current_block_;
  msgnum_t base_msgnum_;
};

#endif

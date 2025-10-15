#include "CryptoMessageTracker.h"

#include <stdexcept>
#include <algorithm>
#include <chrono>

/* DESIGN
 This is a discussion of the internal implementation of CryptoMessageTracker.
 For an explanation of the public interface, see CryptoMessageTracker.h .

 CryptoMessageTracker's internal state is stored in four variables, as follows
   std::vector<std::pair<unsigned int,millis_timestamp_t>> block_records_
   std::vector<bool> msg_records_
   unsigned int current_block_
   msgnum_int_t base_msgnum_
 msg_records_ is used to implement a ring buffer to store booleans, each of
 which records whether one message has been logged (via log_msgnum() ) or not.
 msg_records_ starts out with size one block, but if large volumes of message
 numbers need to be handled in a short period of time then it can be enlarged
 to up to max_blocks blocks (i.e. a total size of block_size*max_blocks).

 The ring buffer implemented with msg_records_ represents a moving window into
 the whole space of possible message numbers. msg_records_ is conceptually split
 into a number of blocks of size block_size (the size of msg_records_ is always
 a multiple of block_size), and some metadata about these blocks is stored in
 block_records_. The first member of an entry of block_records_ counts the
 number of message in that block which have been logged, and the second member
 is a timestamp recording when the most recently logged message number in that
 block was logged.

 current_block_ holds the index (0 based) of the block of msg_records_ which is
 the current first block of the ring buffer. base_msgnum_ holds the message
 number which is currently associated to the first entry of the ring buffer of
 booleans. Thus, to be concrete, the boolean at index current_block_*block_size
 in msg_records_ records the status of the message number in base_msgnum_. The
 ring buffer starts at the block of msg_records_ with index current_block_,
 then goes to the end of msg_records_ before looping back to the start of
 msg_records_ and from there to the block just before the one with index
 current_block_.

 The internal state is not changed by calls to have_seen_msgnum(). It is reset
 by a call to reset(). Calls to log_msgnum() can trigger moving of the window
 of message numbers, and reallocation of msg_records_ to a different size. This
 process is implemented via the methods how_many_extra_blocks(),
 move_records_window(), and reallocate_records(). If a message number msgnum
 which lies beyond the top of the current window of message numbers is passed
 to log_msgnum(), then the window must be moved forward to bring msgnum into
 range. The preferred way to do this is to advance current_block_ and discard
 the records stored in the blocks which it passes over, which thus allows these
 blocks to be reused for records for higher message numbers. This standard
 "ring buffer" method allows the non-discarded records to remain unmoved in
 msg_records_, which yields an efficient system. This process is done by the
 move_records_window() function.

 The above method is always used if msg_records_ has reached its maximum size.
 However, if this is not the case, then before a block is discarded as described
 above, the metadata stored for it in block_records_ is consulted. If there are
 still message numbers represented by records in the block which have not been
 received, and the block was written to within the current round-trip time, then
 a reallocation operation will be performed where msg_records_ is enlarged
 to try to avoid discarding this block. This process is done by the
 reallocate_records() function.
 */


CryptoMessageTracker::CryptoMessageTracker(const std::shared_ptr<RTTTracker>& rtt_tracker):
  rtt_tracker_(rtt_tracker),
  block_records_(1,std::pair<unsigned int,millis_timestamp_t>{0,0}),
  msg_records_(block_size,false),
  current_block_(0),
  base_msgnum_(0)
{}


/* CryptoMessageTracker::reset() causes the CryptoMessageTracker to "forget" all the
 * message numbers it has seen, ready to be used in a new message session.
 */
void CryptoMessageTracker::reset()
{
  std::fill(msg_records_.begin(),msg_records_.end(),false);
  std::fill(block_records_.begin(),block_records_.end(),
            std::pair<unsigned int,millis_timestamp_t>{0,0});
  current_block_ = 0;
  base_msgnum_ = 0;
}


/* CryptoMessageTracker::have_seen_msgnum() tests whether a message number has been
 * logged with CryptoMessageTracker::log_msgnum(). This function is guaranteed to
 * return true of msgnum has been logged, but may also return true if it has not.
 * See the comment at the top of CryptoMessageTracker.h for a detailed description
 * of how this function behaves.
 */
bool CryptoMessageTracker::have_seen_msgnum(msgnum_int_t msgnum)
{
  /* if msgnum is below base_msgnum_ then any record we might have had of it has
     been discarded, so we must assume it's been seen */
  if(msgnum < base_msgnum_){
    return true;
  }

  /* if msgnum is beyond the current upper limit of the window of message numbers,
     we have certainly not seen it */
  if(msgnum >=  base_msgnum_+msg_records_.size()){
    return false;
  }

  /* msgnum lies within our current window of message numbers, so return the record */
  return msg_records_[records_pos(msgnum)];
}


/* CryptoMessageTracker::log_msgnum() tells the CryptoMessageTracker than the message
 * number msgnum has been seen. A record is made of this fact, which may involve moving
 * the window of message numbers for which a record is kept, or growing the vectors
 * msg_records_ and block_records_. See DESIGN above for more information.
 */
void CryptoMessageTracker::log_msgnum(msgnum_int_t msgnum)
{
  // if msgnum is below base_msgnum_, we cannot make a record for it
  if(msgnum < base_msgnum_){
    return;
  }

  // get the number of milliseconds since the epoch
  auto now = std::chrono::system_clock::now();
  auto now_since_epoch = now.time_since_epoch();
  millis_timestamp_t millis_since_epoch =
    std::chrono::duration_cast<std::chrono::milliseconds>(now_since_epoch).count();

  /* if msgnum is beyond the current upper limit of the window of message numbers,
     we need to move the window */
  if(msgnum >=  base_msgnum_+msg_records_.size()){

    /* calculate the number of blocks by which the window needs to move forward,
       and how many (if any) blocks msg_records_ should grow by */
    std::uint_least64_t num_blocks_forward =
      (( msgnum - (base_msgnum_ + msg_records_.size()) )/block_size) + 1;
    unsigned int num_extra_blocks = how_many_extra_blocks(num_blocks_forward,
                                                          millis_since_epoch,
                                                          rtt_tracker_->current_rtt());

    /* move the window of message numbers on, reallocating the vector if necessary */
    if(num_extra_blocks == 0){
      move_records_window(num_blocks_forward);
    }
    else{
      reallocate_records(num_blocks_forward,num_extra_blocks);
    }

  }

  /* msgnum now lies within our window of message numbers, so we record that it has been
     seen and update the block metadata */
  std::vector<bool>::size_type i = records_pos(msgnum);
  msg_records_[i] = true; // msgnum has been seen
  block_records_[i/block_size].first += 1; // number of messages in this block which have been seen
  block_records_[i/block_size].second = millis_since_epoch; // last modification time of this block
}


/* CryptoMessageTracker::record_pos() converts a message number msgnum to the index
 * of that message number's record in msgnum_records_. This method assumes that
 * msgnum is in range [ base_msgnum_, base_msgnum_+msg_records_.size() )
 */
std::vector<bool>::size_type CryptoMessageTracker::records_pos(msgnum_int_t msgnum)
{
  std::uint_least64_t msgnum_offset = msgnum - base_msgnum_;
  std::uint_least64_t msg_records_offset = (block_size*current_block_);
  return (msgnum_offset + msg_records_offset) % msg_records_.size();
}


/* CryptoMessageTracker::how_many_extra_blocks() calculates how many blocks
 * msg_records_ should be enlarged by, if we want to move the window forward by
 * num_blocks_forward blocks. See DESIGN above for more information.
 */
unsigned int
CryptoMessageTracker::how_many_extra_blocks(std::uint_least64_t num_blocks_forward,
                                            millis_timestamp_t millis_since_epoch,
                                            unsigned long int current_rtt)
{
  /* if we have already reached the maximum size then no extra blocks
     note that we always have block_records_.size()*block_size = msg_records_.size() */
  if(block_records_.size() == max_blocks){
    return 0;
  }

  /* Look through the blocks whose records we might want to discard to move
     num_blocks_forward blocks forward. If we find a block which contains
     records for message numbers which have not been logged yet, and which
     has been written to in the last current_rtt milliseconds, then we
     will try to retain this block by reallocating msg_records_ with extra
     blocks. The variable i will, after the loop has finished, hold the offset
     from current_block_ of the first block we would like to retain.
   */
  std::uint_least64_t i;
  std::uint_least64_t i_limit = block_records_.size() < num_blocks_forward ?
    block_records_.size() : num_blocks_forward;
  for(i=0; i < i_limit; i++){
    auto& block_record = block_records_[(i+current_block_) % block_records_.size()];
    if( (block_record.first < block_size) and
        ((millis_since_epoch-block_record.second) <= current_rtt) ){
      break;
    }
  }

  /* To be able to keep the block which is i blocks on from current_block_ in
     the ring buffer, we would need to allocate (num_blocks_forward-i) new blocks.
     If we can return this number we do, but we must always ensure that msg_records_
     is at most max_blocks.
   */
  return num_blocks_forward-i < max_blocks-block_records_.size() ?
    num_blocks_forward-i : max_blocks-block_records_.size();
}


/* CryptoMessageTracker::move_records_window() moves the window of message numbers
 * represented by the ring buffer on by num_blocks_forward blocks. It resets the
 * block whose records are discarded to be used for new message numbers. "resetting"
 * a block means setting all of its booleans to false (i.e. "message number not seen
 * yet"), and resetting the metadata stored in block_records_.
 */
void CryptoMessageTracker::move_records_window(std::uint_least64_t num_blocks_forward)
{
  /* 1 - reset blocks */
  unsigned int num_blocks_to_reset = block_records_.size() < num_blocks_forward ?
    block_records_.size() : num_blocks_forward;

  for(unsigned int i=0; i<num_blocks_to_reset; i++){
    unsigned int this_block_offset = (i+current_block_) % block_records_.size();
    block_records_[this_block_offset] = std::pair<unsigned int,millis_timestamp_t>{0,0};
    std::fill(msg_records_.begin()+(this_block_offset*block_size),
              msg_records_.begin()+((this_block_offset+1)*block_size),
              false);
  }

  /* 2 - move window */
  current_block_ = (current_block_+num_blocks_forward) % block_records_.size();
  base_msgnum_ += num_blocks_forward*block_size;
}


/* CryptoMessageTracker::reallocate_records() moves the records in msg_records_ and
 * block_records_ to new bigger vectors, with space for num_extra_blocks blocks. It
 * also moves the window of message numbers on by num_blocks_forward blocks.
 * The block pointed to by current_block_ is always the first block in msg_records_
 * after this function returns.
 */
void CryptoMessageTracker::reallocate_records(std::uint_least64_t num_blocks_forward,
                                              unsigned int num_extra_blocks)
{
  std::vector<bool> new_msg_records((block_records_.size()+num_extra_blocks)*block_size,false);
  std::vector<std::pair<unsigned int,millis_timestamp_t>> \
    new_block_records(block_records_.size()+num_extra_blocks,
                      std::pair<unsigned int,millis_timestamp_t>{0,0});

  /* Find how many blocks of records need to be copied to the new vectors. We shall only copy
     blocks whose data will not be discarded due to the window move operation. This can mean that
     no blocks get copied. */
  unsigned int num_blocks_to_copy = (num_blocks_forward-num_extra_blocks) > block_records_.size() ? 0 :
    block_records_.size() - (num_blocks_forward-num_extra_blocks);
  // point current_block_ to the first block to (possibly) copy (skipping over blocks we are going to discard)
  current_block_ = (current_block_ - num_blocks_to_copy) % block_records_.size();

  /* copy the blocks of records and their metadata into the new vectors */
  for(unsigned int i=0; i<num_blocks_to_copy; i++){
    unsigned int this_block_offset = (current_block_+i) % block_records_.size();
    new_block_records[i] = block_records_[this_block_offset];
    std::copy(msg_records_.begin()+(this_block_offset*block_size),
              msg_records_.begin()+((this_block_offset+1)*block_size),
              new_msg_records.begin()+(i*block_size) );
  }

  msg_records_ = std::move(new_msg_records);
  block_records_ = std::move(new_block_records);
  current_block_ = 0;
  base_msgnum_ += (num_blocks_forward-num_extra_blocks)*block_size;
}

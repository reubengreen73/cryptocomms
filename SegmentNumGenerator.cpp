#include "SegmentNumGenerator.h"

#include <stdexcept>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>

#include "EpochTime.h"

namespace
{

  /* our segment numbers are stored as unsigned 6 byte integers, so the maximum
   * value of a segment number is (2^48 - 1)
   */
  constexpr SegmentNumGenerator::segnum_t segnum_max = 281474976710655U;

  /* get_saved_segnum() loads the stored segment number from the file. A return
   * value of 0 indicates an error (note that 0 is not a valid segment number).
   *
   * The application never creates this file, and for the application to run it must
   * already exist and contain a valid stored segment number. Allowing the application
   * to auto-create this file would weaken the effectiveness of recording which segment
   * numbers have been used.
   *
   * The format of the segment number file is as follows. The first two lines must be
   * identical, and contain only the characters 0123456789. Any subsequent lines must
   * be empty (not even whitespace is allowed). The number on the first two lines is the
   * stored segment number. The strict formatting required of this file, together with
   * the repetition of the stored number, means that any accidental corruption will be
   * detected with high probability.
   */
  SegmentNumGenerator::segnum_t get_saved_segnum(const std::string& path)
  {
    std::ifstream segnum_file(path);
    if(!segnum_file){
      return 0;
    }

    /* read the first two lines and check that they match */
    std::string file_line_1, file_line_2;
    bool good_read = true;
    good_read = good_read and std::getline(segnum_file,file_line_1);
    good_read = good_read and std::getline(segnum_file,file_line_2);
    if( (not good_read) or (file_line_1 != file_line_2) ){
      return 0;
    }

    /* check that any additional lines are empty */
    std::string file_line;
    while(std::getline(segnum_file,file_line)){
      if(file_line.size() > 0){
        return 0;
      }
    }

    /* check that the first line contains only digits */
    auto check_func = [&](char c){return (std::string("0123456789").find(c) !=
                                          std::string::npos);};
    if(not std::all_of(file_line_1.begin(),file_line_1.end(),check_func) ){
      return 0;
    }

    /* convert the first line to an integer */
    SegmentNumGenerator::segnum_t saved_segnum;
    try{
      saved_segnum = std::stoull(file_line_1);
    } catch (const std::exception& ex) {
      return 0;
    }

    /* The segment number stored in the file should either be a value stored by a previous
     * run of the application, or else a valid initial value set at installation (if there has
     * not been any previous run of the application which stored a value). We check that the
     * value from the file is strictly less than segnum_max, since the maximum number which a
     * previous run of the application could have stored is (segnum_max -1), and the initial
     * value set at installation should be vastly smaller than this.
     */
    if(not (saved_segnum < segnum_max) ){
      return 0;
    }

    return saved_segnum;
  }


  /* get_segnum_sysclock() generates a fresh segment number from the system clock, by computing
   * the number of milliseconds since the UNIX epoch.
   */
  SegmentNumGenerator::segnum_t get_segnum_sysclock()
  {
    millis_timestamp_t millis_since_epoch = epoch_time_millis();
    if(millis_since_epoch > segnum_max){
      /* We check that the number of milliseconds since the epoch is at most
       * segnum_max. The actual number of milliseconds since the epoch will not
       * exceed this value until after 10,000 CE, and so this is just a sanity
       * check on the system clock.
       */
      throw std::runtime_error("SegmentNumGenerator: timestamp from the system is too big");
    }

    return millis_since_epoch;
  }


  /* save_segnum() stores a segment number to back to the file. The number stored represents
   * the highest segment number which could already have been handed out by next_num().
   * save_segment() makes some effort to ensure that the file has been written to permanent
   * storage before returning. The argument segnum must not be 0, as 0 is not a valid
   * segment number.
   */
  void save_segnum(SegmentNumGenerator::segnum_t segnum, const std::string& path)
  {
    std::string segnum_string = std::to_string(segnum);
    SegmentNumGenerator::segnum_t reread_segnum;

    /* In this loop, we try to write the new value to the file and check for
     * a successful write by reading the value back from the file, retrying
     * until we succeed.
     */
    while(true){
      std::ofstream segnum_file(path,std::ios::trunc);
      if(!segnum_file){
        throw std::runtime_error(std::string("SegmentNumGenerator: could not open stored segment number file: ")
                                 +path);
      }

      /* see the comment before get_saved_segnum() for the format of the segment number file */
      segnum_file << segnum_string << std::endl;
      segnum_file << segnum_string;
      segnum_file.close();

      /* We want to be as sure as we can that the new value has been written to the file,
       * so we read the value back from the file and check. This should work first time
       * pretty much always...
       */
      reread_segnum = get_saved_segnum(path);
      /* get_saved_segnum() returns 0 on error, but segnum will not be 0, so this is fine */
      if(reread_segnum == segnum){
        break;
      }

      /* ... but if it does not work then we just sleep for 0.1 second and try again. This
       * is very crude but it's the only think I can think of other than just throwing an
       * error. Since save_segnum() is called once at application start-up, and very
       * infrequently (if ever) thereafter (assuming reserved_ is set to a reasonable value),
       * this behaviour is acceptable.
       */
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

}


/* "path" is the path to the file which records used segment numbers,
 * "reserved" is how many segment numbers to reserve for use each time
 * a fresh reservation of numbers happens
 */
SegmentNumGenerator::SegmentNumGenerator(std::string path, unsigned int reserved):
  path_(path)
{
  set_reserved(reserved);

  /* Setting both next_num_ and new_reserve_needed_ to the same value will cause
   * reserve_nums() to be called on the first invocation of next_num(). This allows
   * set_reserve() to be called to set a better value for reserved_ before a
   * reservation of numbers happens. We do not use the segment number 0, as this
   * value is used internally to indicate that a segement number has not been set.
   */
  next_num_ = 1;
  new_reserve_needed_ = 1;
}


/* SegmentNumGenerator::next_num() returns a fresh segment number. The internal
 * state of SegmentNumGenerator is simple: the next segment number to be handed
 * out by next_num() is stored in next_num_, and the last segment number which
 * can be handed out before a new internal reservation of segment numbers is needed
 * is (new_reserve_needed_ - 1), so the condition for calling reserve_nums() is
 * (next_num_ == new_reserve_needed_).
 */
SegmentNumGenerator::segnum_t SegmentNumGenerator::next_num()
{
const std::lock_guard<std::mutex> guard_for_lock(lock_);

 if(next_num_ == new_reserve_needed_){
   reserve_nums();
 }

 return next_num_++;
}


/* SegmentNumGenerator::set_reserved() sets how many segment numbers to reserve at
 * each call of reserve_nums().
 */
void SegmentNumGenerator::set_reserved(unsigned int reserved)
{
const std::lock_guard<std::mutex> guard_for_lock(lock_);

  if(reserved == 0){
    throw std::runtime_error("SegmentNumGenerator: set_reserved called with 0");
  }

  reserved_ = reserved;
}



/* SegmentNumGenerator::reserve_nums() uses the system clock and the stored record
 * of which segment numbers have been used to reserve a range of fresh segment numbers
 * to be handed out by next_num(), and updates the stored record of used segment numbers
 * to mark all numbers in this reserved range as used.
 *
 * The generation of segment numbers is based on the number of milliseconds since the UNIX
 * epoch, combined with a record of used segment numbers on permanent storage to further
 * guard against reuse in the event of changes to the system clock.
 */
void SegmentNumGenerator::reserve_nums()
{
  segnum_t saved_segnum = get_saved_segnum(path_);
  if(saved_segnum == 0){
    throw std::runtime_error(std::string("SegmentNumGenerator: error reading saved") +\
                             std::string(" segment number from file ") +  path_ );
  }

  /* Generate a segment number from the system clock. We want to ensure that
   * this is a segment number that no previous run of the application could
   * have generated from the system clock (assuming that the system clock has
   * always increased monotonically), so we ensure that we see an increment in
   * the generated segment number before using it. This is acceptable since
   * reserve_nums() is called once at application start-up, and very infrequently
   * (if ever) thereafter (assuming reserved_ is set to a reasonable value).
   */
  segnum_t base_sysclock_segnum, sysclock_segnum;
  sysclock_segnum = base_sysclock_segnum = get_segnum_sysclock();
  while(sysclock_segnum == base_sysclock_segnum){
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sysclock_segnum = get_segnum_sysclock();
  }

  /* calculate the next segment number, and update the upper limit on segment
   * numbers before another reserve_nums() call is needed
   */
  next_num_ = std::max(saved_segnum+1,sysclock_segnum);
  segnum_t next_new_reserve_needed = next_num_ + reserved_;
  if(next_new_reserve_needed > segnum_max){
    throw std::runtime_error("SegmentNumGenerator: new upper segment number limit is too high");
  }
  new_reserve_needed_ = next_new_reserve_needed;

  save_segnum(new_reserve_needed_-1,path_);
}

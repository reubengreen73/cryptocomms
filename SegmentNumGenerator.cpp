#include "SegmentNumGenerator.h"

#include <stdexcept>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>

#include "EpochTime.h"

/*  SEGMENT NUMBER FILES
 *  Cryptocomms keeps a record of which segment numbers have been used on non-volatile
 *  storage. This record helps ensure that segment numbers will never be reused, which
 *  is vital for cryptographic security. The record consists of two files, which are
 *  located at path_first_ and path_second_. These files will normally be identical.
 *
 *  The format of a segment number file is as follows. The first two lines must be
 *  identical, and contain only the characters 0123456789. Any subsequent lines must be
 *  empty (not even whitespace is allowed). The number on the first two lines is the
 *  stored segment number. The strict formatting required of these files, together with
 *  the repetition of the stored number, means that any accidental corruption will be
 *  detected with high probability.
 *
 *  Whenever the record of segment numbers is read from non-volatile storage (this
 *  happens, in particular, at application start-up), at least one of these files must
 *  be present, correctly formatted, and contain a valid segment number. If not, an
 *  error is thrown and the application aborts. After reading the segment numbers, the
 *  application will select some range of segment numbers to reserve for use, and will
 *  write the highest segment number reserved back to non-volatile storage, writing one
 *  of the files completely before writing the other, to ensure at least one file
 *  always contains a valid record. This allows recovery from unexpected shutdowns
 *  during these file writes (which might leave a file corrupted).
 *
 *  Note that at least one segment number storage file must be initialized with a
 *  positive segment number before Cryptocomms runs for the first time. 1 is a good
 *  choice for the segment number to use.
 */

namespace
{

  /* our segment numbers are stored as unsigned 6 byte integers, so the maximum
   * value of a segment number is (2^48 - 1)
   */
  constexpr SegmentNumGenerator::segnum_t segnum_max = 281474976710655U;


  /* get_saved_segnum() loads the stored segment number from a file. A return
   * value of 0 indicates an error (note that 0 is not a valid segment number).
   * See the comment at the top of the file for information on the format of the
   * segment number file.
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
    auto check_func = [&](char c){
      return (std::string("0123456789").find(c) != std::string::npos);
    };
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
     *
     * If the segment number is too big here, we throw an error rather than return 0, as
     * returning 0 would allow a smaller number from the other file to be used, which might
     * cover up some serious problem.
     */
    if(not (saved_segnum < segnum_max) ){
      throw std::runtime_error("SegmentNumGenerator: segment number too large in file "+path);
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


  /* save_segnum() stores a segment number to the file at the given path. The number stored
   * represents the highest segment number which could already have been handed out by
   * next_num(). save_segnum() makes some effort to ensure that the file has been written to
   * permanent storage before returning. The argument segnum must not be 0, as 0 is not a
   * valid segment number.
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
        throw std::runtime_error("SegmentNumGenerator: could not open stored segment number file: "
                                 +path);
      }

      /* see the comment at the top of the file for the format of the segment number file */
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


/* "path" is the path to the files which record used segment numbers,
 * "reserved" is how many segment numbers to reserve for use each time
 * a fresh reservation of numbers happens
 */
SegmentNumGenerator::SegmentNumGenerator(std::string path, unsigned int reserved):
  path_first_(path+"_FIRST"),
  path_second_(path+"_SECOND")
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
  /* Read from both of the segment number files, and take the higher. If reading both
     files returns an error, there is no usable record of segment numbers and we must
     abort. get_saved_segnum() returns 0 if the given file is missing or corrupt. */
  segnum_t saved_segnum_first = get_saved_segnum(path_first_);
  segnum_t saved_segnum_second = get_saved_segnum(path_second_);
  segnum_t saved_segnum = std::max(saved_segnum_first,saved_segnum_second);
  if(saved_segnum == 0){
    throw std::runtime_error("SegmentNumGenerator: error reading saved segment number");
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

  /* Write the segment number to the first file, and then write it to the second file.
     save_segnum() ensures that the write has been completed successfully before returning,
     so this two-step process ensures that there is always one file which holds a segment
     number at least as great as any which has been returned from next_num(). This allows
     recovery from an unexpected shutdown during either of the calls to save_segnum() which
     leaves the file in a corrupt state. */
  save_segnum(new_reserve_needed_-1,path_first_);
  save_segnum(new_reserve_needed_-1,path_second_);
}

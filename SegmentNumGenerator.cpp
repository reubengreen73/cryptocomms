#include "SegmentNumGenerator.h"

#include <stdexcept>
#include <chrono>
#include <thread>
#include <fstream>


namespace
{

  /* our segment numbers are stored as unsigned 6 byte integers, so the maximum
   * value of a segment number is (2^48 - 1)
   */
  constexpr uint_least64_t segnum_max = 281474976710655U;

  /* get_saved_segnum() loads the stored segment number from the file. This number
   * is the highest segment number which could have been used by a previous run of the
   * application.
   *
   * The application never creates this file, and for the application to run it must
   * already exist and contain a valid stored segment number. Allowing the application
   * to auto-create this file would weaken the effectiveness of recording which segment
   * numbers have been used.
   */
  uint_least64_t get_saved_segnum(const std::string& path)
  {
    std::ifstream segnum_file(path);
    if(!segnum_file){
      throw std::runtime_error(std::string("SegmentNumGenerator: could not open stored segment number file: ")
			       +path);
    }

    std::string saved_segnum_string;
    if(!std::getline(segnum_file,saved_segnum_string)){
      throw std::runtime_error(std::string("SegmentNumGenerator: could not read stored segment number file: ")
			       +path);
    }

    unsigned long long int saved_segnum;
    try{
      saved_segnum = std::stoull(saved_segnum_string);
    } catch (const std::exception& ex) {
      throw std::runtime_error(std::string("SegmentNumGenerator: could not parse stored segment number in file: ")
			       +path+std::string(" (exception was :")+ex.what()+std::string(")"));
    }

    /* The segment number stored in the file should either be a value stored by a previous
     * run of the application, or else a valid initial value set at installation (if there has
     * not been any previous run of the application which stored a value). We check that the
     * value from the file is strictly less than segnum_max, since the maximum number which a
     * previous run of the application could have stored is (segnum_max -1), and the initial
     * value set at installation should be vastly smaller than this.
     */
    if( not(saved_segnum < segnum_max) ){
      throw std::runtime_error(std::string("SegmentNumGenerator: stored segment number in file: ")+path+
			       std::string(" is too big"));
    }

    return saved_segnum;
  }


  /* get_segnum_sysclock() generates a fresh segment number from the system clock, by computing
   * the number of milliseconds since the UNIX epoch.
   */
  uint_least64_t get_segnum_sysclock()
  {
    auto now = std::chrono::system_clock::now();
    auto now_since_epoch = now.time_since_epoch();
    unsigned long long int millis_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(now_since_epoch).count();
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
   * storage before returning.
   */
  void save_segnum(uint_least64_t segnum, const std::string& path)
  {
    std::string segnum_string = std::to_string(segnum);
    uint_least64_t reread_segnum;

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

      segnum_file << segnum_string;
      segnum_file.close();

      /* We want to be as sure as we can that the new value has been written to the file,
       * so we read the value back from the file and check. This should work first time
       * pretty much always...
       */
      reread_segnum = get_saved_segnum(path);
      if(reread_segnum == segnum)
	break;

      /* ... but if it does not work then we just sleep for 0.1 second and try again. This
       * is very crude but it's the only think I can think of other than just throwing an
       * error. Since save_segnum() is called once at application start-up, and very
       * infrequently (if ever) thereafter (assuming _reserved is set to a reasonable value),
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
SegmentNumGenerator::SegmentNumGenerator(std::string path, uint reserved):
  _path(path)
{
  set_reserved(reserved);

  /* Setting both _next_num and _new_reserve_needed to the same value will cause
   * reserve_nums() to be called on the first invocation of next_num(). This allows
   * set_reserve() to be called to set a better value for _reserved before a
   * reservation of numbers happens
   */
  _next_num = 0;
  _new_reserve_needed = 0;
}


/* SegmentNumGenerator::next_num() returns a fresh segment number. The internal
 * state of SegmentNumGenerator is simple: the next segment number to be handed
 * out by next_num() is stored in _next_num, and the last segment number which
 * can be handed out before a new internal reservation of segment numbers is needed
 * is (_new_reserve_needed - 1), so the condition for calling reserve_nums() is
 * (_next_num == _new_reserve_needed).
 */
uint_least64_t SegmentNumGenerator::next_num()
{
const std::lock_guard<std::mutex> _lock_guard(_lock);

 if(_next_num == _new_reserve_needed){
   reserve_nums();
 }

 return _next_num++;
}


/* SegmentNumGenerator::set_reserved() sets how many segment numbers to reserve at
 * each call of reserve_nums().
 */
void SegmentNumGenerator::set_reserved(uint reserved)
{
const std::lock_guard<std::mutex> _lock_guard(_lock);

  if(reserved == 0){
    throw std::runtime_error("SegmentNumGenerator: set_reserved called with 0");
  }

  _reserved = reserved;
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
  uint_least64_t saved_segnum = get_saved_segnum(_path);

  /* Generate a segment number from the system clock. We want to ensure that
   * this is a segment number that no previous run of the application could
   * have generated from the system clock (assuming that the system clock has
   * always increased monotonically), so we ensure that we see an increment in
   * the generated segment number before using it. This is acceptable since
   * reserve_nums() is called once at application start-up, and very infrequently
   * (if ever) thereafter (assuming _reserved is set to a reasonable value).
   */
  uint_least64_t base_sysclock_segnum, sysclock_segnum;
  sysclock_segnum = base_sysclock_segnum = get_segnum_sysclock();
  while(sysclock_segnum == base_sysclock_segnum){
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sysclock_segnum = get_segnum_sysclock();
  }

  /* calculate the next segment number, and update the upper limit on segment
   * numbers before another reserve_nums() call is needed
   */
  _next_num = std::max(saved_segnum+1,sysclock_segnum);
  uint_least64_t next_new_reserve_needed = _next_num + _reserved;
  if(next_new_reserve_needed > segnum_max){
    throw std::runtime_error("SegmentNumGenerator: new upper segment number limit is too high");
  }
  _new_reserve_needed = next_new_reserve_needed;

  save_segnum(_new_reserve_needed-1,_path);
}

#include "testsys.h"
#include "../SegmentNumGenerator.h"

#include <thread>
#include <vector>
#include <set>
#include <fstream>
#include <iostream>
#include <cstdint>

void stress_test_segnumgen_uniqueness_thread_func(std::vector<std::uint_least64_t>& segnums, SegmentNumGenerator& sng)
{
  for(int i = 0; i < 200; i++){
    segnums.push_back(sng.next_num());
  }
}

/* Stress-test the SegmentNumGenerator to ensure no repetition of segment numbers
 * in a multi-threaded environment with frequent re-allocation of reserved segment
 * numbers. This test creates multiple threads and has them all use a shared
 * SegmentNumGenerator to generate lots of segment numbers in parallel. All the
 * segment numbers generated are then checked for repetitions.
 */
TESTFUNC(stress_test_segnumgen_uniqueness)
{
  int num_threads = 20;
  std::vector<std::vector<std::uint_least64_t>> segnum_vectors(num_threads);

  std::ofstream segnum_file("testfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Test error: could not open \"testfile\""));
  }
  segnum_file << std::to_string(0);
  segnum_file.close();

  for(int j = 0; j < 10; j++){
    SegmentNumGenerator sng("testfile",j+1);
    std::vector<std::thread> threads;

    for(int i = 0; i < num_threads; i++){
      threads.push_back(std::thread(stress_test_segnumgen_uniqueness_thread_func,
                                    std::ref(segnum_vectors[i]),std::ref(sng)));
    }

    for(int i = 0; i < num_threads; i++){
      threads[i].join();
    }

  }

  /* Collect all of the generated segment numbers together into one vector,
   * create a set of all the elements, and compare the size of the vector to
   * the size of the set. If they are the same then no segment numbers were
   * repeated.
   */

  std::vector<std::uint_least64_t> all_segnums;
  for(int i=0; i < num_threads; i++){
    all_segnums.insert(all_segnums.end(),segnum_vectors[i].begin(),segnum_vectors[i].end());
  }

  std::set<std::uint_least64_t> all_segnums_set(all_segnums.begin(),all_segnums.end());
  TESTASSERT(all_segnums.size() == all_segnums_set.size());
}


/* Check that a missing stored segnum file causes the correct error to be thrown
 */
TESTFUNC(segnumgen_file_missing){
  std::ofstream segnum_file("testfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Test error: could not open \"testfile\""));
  }

  segnum_file.close();
  if(std::remove("testfile") != 0){
    std::cout << "Test warning: could not delete \"testfile\"" << std::endl;
  }

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: could not open stored segment number file");
}


/* Check that a stored segnum file with non-numerical content causes the correct error to be thrown
 */
TESTFUNC(segnumgen_file_is_nonsense){
  std::ofstream segnum_file("testfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Test error: could not open \"testfile\""));
  }
  segnum_file << "blah blah";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: could not parse stored segment number in file");
}


/* Check that a stored segnum file with a value that is too big causes the correct error to be thrown
 */
TESTFUNC(segnumgen_file_value_too_high){
  std::ofstream segnum_file("testfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Test error: could not open \"testfile\""));
  }
  segnum_file << "281474976710655";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: stored segment number in file");
}


/* Check that calling set_reserved() with argument 0 throws the expected error
 */
TESTFUNC(set_reserved_with_zero){
  std::ofstream segnum_file("testfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Test error: could not open \"testfile\""));
  }
  segnum_file << std::to_string(0);;
  segnum_file.close();

  TESTTHROW(SegmentNumGenerator sng("testfile",0),"SegmentNumGenerator: set_reserved called with 0");

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.set_reserved(0),"SegmentNumGenerator: set_reserved called with 0");
}

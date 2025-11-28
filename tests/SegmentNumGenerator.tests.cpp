#include "testsys.h"
#include "../SegmentNumGenerator.h"

#include <thread>
#include <vector>
#include <set>
#include <fstream>
#include <iostream>

void stress_test_segnumgen_uniqueness_thread_func(std::vector<SegmentNumGenerator::segnum_t>& segnums,
                                                  SegmentNumGenerator& sng)
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
TESTFUNC(SegmentNumGenerator_stress_test_segnumgen_uniqueness)
{
  int num_threads = 20;
  std::vector<std::vector<SegmentNumGenerator::segnum_t>> segnum_vectors(num_threads);

  std::vector<std::string> testfile_names{"testfile_FIRST","testfile_SECOND"};
  for(const std::string& testfile_name : testfile_names){
    std::ofstream segnum_file(testfile_name,std::ios::trunc);
    if(!segnum_file){
      TESTERROR("could not open \""+testfile_name+"\"");
    }
    segnum_file << "1\n1";
    segnum_file.close();
  }

  TESTMSG("stress-testing SegmentNumGenerator, this may take some time")
  for(int j = 0; j < 10; j++){
    TESTMSG(" pass "+std::to_string(j+1)+" of 10");
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

  std::vector<SegmentNumGenerator::segnum_t> all_segnums;
  for(int i=0; i < num_threads; i++){
    all_segnums.insert(all_segnums.end(),segnum_vectors[i].begin(),segnum_vectors[i].end());
  }

  std::set<SegmentNumGenerator::segnum_t> all_segnums_set(all_segnums.begin(),all_segnums.end());
  TESTASSERT(all_segnums.size() == all_segnums_set.size());
}


/* Check that missing stored segnum files cause the correct error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_files_missing){
  std::vector<std::string> testfile_names = {"testfile_FIRST","testfile_SECOND"};
  for(const std::string& testfile_name : testfile_names){
    std::ofstream segnum_file(testfile_name,std::ios::trunc);
    if(!segnum_file){
      TESTERROR("could not open \""+testfile_name+"\"");
    }

    segnum_file.close();
    if(std::remove(testfile_name.c_str()) != 0){
      std::cout << "Test warning: could not delete \""+testfile_name+"\"\n";
    }
  }

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum file with just one line causes the correct error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_one_line){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "130607";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum with non-digit characters causes the correct error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_non_digit){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "13o607\n13o607";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum with trailing whitespace causes the correct error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_trailing_whitespace){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "130607 \n130607 ";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum with leading whitespace causes the correct error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_leading_whitespace){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << " 130607\n 130607";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum file with extra non-empty lines causes the correct
 * error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_extra_lines){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "130607\n130607\n ";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum file with non-matching lines causes the correct error
 * to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_non_matching_lines){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "11023\n11213";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: error reading saved segment number");
}


/* Check that a stored segnum file with a value that is too big causes the correct
 * error to be thrown
 */
TESTFUNC(SegmentNumGenerator_segnumgen_file_value_too_high){
  std::ofstream segnum_file("testfile_FIRST",std::ios::trunc);
  if(!segnum_file){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file << "281474976710655\n281474976710655";
  segnum_file.close();

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.next_num(),"SegmentNumGenerator: segment number too large in file");
}


/* Check that if the first segment number file is corrupt, the other is used
 */
TESTFUNC(SegmentNumGenerator_first_file_corrupt)
{
  /* The segment number stored in the second file is 281474976710600, which is very
     near the maximum segment number, and will not be produced from the system clock
     until after 10,000CE. The result of SegmentNumGenerator::next_num() is checked
     to ensure it is greater than 281474976710600, which shows that the number from
     the file was used. */

  std::ofstream segnum_file_first("testfile_FIRST",std::ios::trunc);
  if(!segnum_file_first){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file_first << "2814749767106a0\n281474976710600";
  segnum_file_first.close();

  std::ofstream segnum_file_second("testfile_SECOND",std::ios::trunc);
  if(!segnum_file_second){
    TESTERROR("could not open \"testfile_SECOND\"");
  }
  segnum_file_second << "281474976710600\n281474976710600";
  segnum_file_second.close();

  SegmentNumGenerator sng("testfile",8);
  SegmentNumGenerator::segnum_t segnum = sng.next_num();
  TESTASSERT(segnum > 281474976710600U);
}


/* Check that if the second segment number file is corrupt, the other is used
 */
TESTFUNC(SegmentNumGenerator_second_file_corrupt)
{
  /* The segment number stored in the first file is 281474976710600, which is very
     near the maximum segment number, and will not be produced from the system clock
     until after 10,000CE. The result of SegmentNumGenerator::next_num() is checked
     to ensure it is greater than 281474976710600, which shows that the number from
     the file was used. */

  std::ofstream segnum_file_first("testfile_FIRST",std::ios::trunc);
  if(!segnum_file_first){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file_first << "281474976710600\n281474976710600";
  segnum_file_first.close();

  std::ofstream segnum_file_second("testfile_SECOND",std::ios::trunc);
  if(!segnum_file_second){
    TESTERROR("could not open \"testfile_SECOND\"");
  }
  segnum_file_second << "2814749767106a0\n281474976710600";
  segnum_file_second.close();

  SegmentNumGenerator sng("testfile",8);
  SegmentNumGenerator::segnum_t segnum = sng.next_num();
  TESTASSERT(segnum > 281474976710600U);
}


/* Check that if the two files hold different numbers, the greater is used
 */
TESTFUNC(SegmentNumGenerator_greater_file_number_used)
{
   /* The segment number stored in the first file is 281474976700000, which is very
     near the maximum segment number, and will not be produced from the system clock
     until after 10,000CE. */
  std::ofstream segnum_file_first("testfile_FIRST",std::ios::trunc);
  if(!segnum_file_first){
    TESTERROR("could not open \"testfile_FIRST\"");
  }
  segnum_file_first << "281474976700000\n281474976700000";
  segnum_file_first.close();

  /* The segment number stored in the second file is 281474976710600, which is very
     near the maximum segment number, and will not be produced from the system clock
     until after 10,000CE. The result of SegmentNumGenerator::next_num() is checked
     to ensure it is greater than 281474976710600, which shows that the number from
     the file was used. */
  std::ofstream segnum_file_second("testfile_SECOND",std::ios::trunc);
  if(!segnum_file_second){
    TESTERROR("could not open \"testfile_SECOND\"");
  }
  segnum_file_second << "281474976710600\n281474976710600";
  segnum_file_second.close();

  SegmentNumGenerator sng("testfile",8);
  SegmentNumGenerator::segnum_t segnum = sng.next_num();
  TESTASSERT(segnum > 281474976710600U);
}


/* Check that calling set_reserved() with argument 0 throws the expected error
 */
TESTFUNC(SegmentNumGenerator_set_reserved_with_zero){
  std::vector<std::string> testfile_names{"testfile_FIRST","testfile_SECOND"};
  for(const std::string& testfile_name : testfile_names){
    std::ofstream segnum_file(testfile_name,std::ios::trunc);
    if(!segnum_file){
      TESTERROR("could not open \""+testfile_name+"\"");
    }
    segnum_file << "1\n1";
    segnum_file.close();
  }

  TESTTHROW(SegmentNumGenerator sng("testfile",0),"SegmentNumGenerator: set_reserved called with 0");

  SegmentNumGenerator sng("testfile");
  TESTTHROW(sng.set_reserved(0),"SegmentNumGenerator: set_reserved called with 0");
}

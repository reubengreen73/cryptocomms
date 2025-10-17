#include <memory>
#include <vector>
#include <algorithm>

#include "testsys.h"
#include "../CryptoMessageTracker.h"
#include "../RTTTracker.h"

typedef CryptoMessageTracker::msgnum_t msgnum_t;

/* check a small set set of message numbers can be logged and recalled correctly */
TESTFUNC(CryptoMessageTracker_check_few_msgnums)
{
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  rtt_tracker->update_rtt(3600000);

  CryptoMessageTracker cmt(rtt_tracker);

  std::vector<msgnum_t> nums{
    0,1,2,3,4,5,6,7,8,9,10,11,12,
    15,17,19,20,21,24,25,
    50,55,56,61,62,63,68,70,73,74,75,79,80,
    100,200,500,1000,1001,2000,2002,
    4999,5000,5001,5002,
    20000,20001,20002,60000,60001,60002,
    1048576, //2^20
    33554432, //2^25
    1073741824, //2^30
    34359738368, //2^35
    1099511627776, //2^40
    35184372088832, //2^45
    281474976710655 // (2^48 - 1), maximum message number
  };

  for(msgnum_t n : nums){
    TESTASSERT(not cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    TESTASSERT(not cmt.have_seen_msgnum(n));
    cmt.log_msgnum(n);
    TESTASSERT(cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    TESTASSERT(cmt.have_seen_msgnum(n));
  }
}


/* check that a large range of message numbers can be logged and recalled correctly */
TESTFUNC(CryptoMessageTracker_test_range)
{
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  rtt_tracker->update_rtt(3600000);

  CryptoMessageTracker cmt(rtt_tracker);

  unsigned int length = 1000000;

  for(msgnum_t n=0; n < length; n++){
    TESTASSERT(not cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n=0; n < length; n++){
    TESTASSERT(not cmt.have_seen_msgnum(n));
    cmt.log_msgnum(n);
    TESTASSERT(cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n=0; n < length; n++){
    TESTASSERT(cmt.have_seen_msgnum(n));
  }
}


/* check that logging message numbers out of order works */
TESTFUNC(CryptoMessageTracker_test_spatter)
{
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  rtt_tracker->update_rtt(3600000);

  CryptoMessageTracker cmt(rtt_tracker);

  std::vector<msgnum_t> nums{
    1000,990,1011,999,1005,1031,991,992,993,1007,
    1027,985,1026,984,986,1001,1002,997,1030,998
  };

  for(msgnum_t n : nums){
    TESTASSERT(not cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    cmt.log_msgnum(n);
    TESTASSERT(cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    TESTASSERT(cmt.have_seen_msgnum(n));
  }
}


/* check that logging and recalling only some message numbers over a large range works
 * correctly */
TESTFUNC(CryptoMessageTracker_test_3_5_7_multiples)
{
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  rtt_tracker->update_rtt(3600000);

  CryptoMessageTracker cmt(rtt_tracker);

  std::vector<msgnum_t> nums;
  for(int i=1;i<106000;i++){
    if( (i%3 == 0) or (i%5 == 0) or (i%7 == 0) ){
      nums.push_back(i);
    }
  }

  for(msgnum_t n : nums){
    TESTASSERT(not cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    cmt.log_msgnum(n);
    TESTASSERT(cmt.have_seen_msgnum(n));
  }

  for(msgnum_t n : nums){
    TESTASSERT(cmt.have_seen_msgnum(n));
  }
}


/* CryptoMessageTracker::have_seen_msgnum() returns true even for an unlogged message number
 * if that message number is below the lower limit of the current message number window.
 * Within the current window (see CryptoMessageTracker.h for details of how this window is
 * calculated), however, have_seen_msgnum() returns true if and only if a message number has
 * been logged. This test checks that this behaviour is correctly implemented.
 */
TESTFUNC(CryptoMessageTracker_check_exact_results)
{
  /* This test has quite a lot of setup in the form of lambda functions which will be used
     repeatedly during the actual test, and we set these up first */
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  CryptoMessageTracker cmt(rtt_tracker);

  // these variables will be captured by reference by the lambdas defined below
  std::vector<msgnum_t> all_msgnums_logged; // list of all the message numbers logged
  msgnum_t msgnum_bound, range_limit;

  /* function to check that have_seen_msgnum() returns true if and only if a message number
     has been logged */
  auto check_all_logged_msgnums = [&]()
  {
    for(msgnum_t n = msgnum_bound; n<(range_limit+CryptoMessageTracker::block_size); n++){
      if( std::find(all_msgnums_logged.begin(),all_msgnums_logged.end(),n) ==
          all_msgnums_logged.end() ){
        TESTASSERT(not cmt.have_seen_msgnum(n));
      }
      else{
        TESTASSERT(cmt.have_seen_msgnum(n));
      }
    }
  };

  /* function to log a vector of message numbers with checks that have_seen_msgnum()
     behaves as it should */
  auto log_msgnums = [&](const std::vector<msgnum_t>& nums)
  {
    for(msgnum_t n : nums){
      if( std::find(all_msgnums_logged.begin(),all_msgnums_logged.end(),n) ==
          all_msgnums_logged.end() ){
        TESTASSERT(not cmt.have_seen_msgnum(n));
        cmt.log_msgnum(n);
        TESTASSERT(cmt.have_seen_msgnum(n));
        all_msgnums_logged.push_back(n);
      }
    }
  };

  /* helper function to generate a list of multiples of a certain number, for generating
     lists of message numbers to log */
  auto get_multiples = [&](unsigned int f)
  {
    std::vector<msgnum_t> nums;
    for(msgnum_t n=msgnum_bound+15;n<range_limit-15;n++){
      if(n%f == 0){
        nums.push_back(n);
      }
    }
    return nums;
  };


  /* the actual test code starts here */

  /* the numbers calculated here are as described in CryptoMessageTracker.h */
  msgnum_t msgnum_highest =
    (CryptoMessageTracker::max_blocks + 1)*CryptoMessageTracker::block_size +
    (CryptoMessageTracker::max_blocks/2);
  msgnum_t x = (CryptoMessageTracker::max_blocks + 2)*CryptoMessageTracker::block_size;
  msgnum_t y = x - (CryptoMessageTracker::block_size*CryptoMessageTracker::max_blocks);
  msgnum_t z = y + CryptoMessageTracker::max_blocks/2;
  msgnum_bound = y;

  /* the highest number we shall check when we check through the numbers to see if they
     have been logged */
  range_limit = x;

  rtt_tracker->update_rtt(3600000);

  /* logging these two message numbers establishes the desired message number window state */
  log_msgnums({z,msgnum_highest});

  check_all_logged_msgnums();

  /* test lots of different numbers */
  for(auto m : std::vector<unsigned int>{619,103,309,71,19,17,7,499}){
    log_msgnums(get_multiples(m));
    check_all_logged_msgnums();
  }

  /* test that things work correctly at the ends of the window */
  std::vector<msgnum_t> end_nums = {
    msgnum_bound,range_limit-1,msgnum_bound+1,range_limit-2,
    msgnum_bound+7,msgnum_bound+5,range_limit-8,range_limit-6,
    msgnum_bound+10,range_limit-13,msgnum_bound+12,range_limit-11
  };
  log_msgnums(end_nums);
  check_all_logged_msgnums();

  /* check that the above-added message numbers are recalled correctly after
     the message number window moves */
  msgnum_t block_offset = CryptoMessageTracker::block_size*3;
  log_msgnums({msgnum_highest+block_offset});
  msgnum_bound += block_offset;
  range_limit += block_offset;
  check_all_logged_msgnums();
}


/* check that resetting a CryptoMessageTracker works correctly */
TESTFUNC(CryptoMessageTracker_reset)
{
  std::shared_ptr<RTTTracker> rtt_tracker = std::make_shared<RTTTracker>();
  rtt_tracker->update_rtt(3600000);

  CryptoMessageTracker cmt(rtt_tracker);

  for(msgnum_t n=0; n<(CryptoMessageTracker::block_size*10); n++){
    cmt.log_msgnum(n);
    TESTASSERT(cmt.have_seen_msgnum(n));
  }

  cmt.reset();

  for(msgnum_t n=0; n<(CryptoMessageTracker::block_size*10); n++){
    TESTASSERT(not cmt.have_seen_msgnum(n));
  }
}

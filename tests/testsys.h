/* This header defines macros for the test system, to be used in
 * .tests.cpp files
 */

#include <stdexcept>
#include <string>
#include <stdexcept>

/* The TESTFUNC macro does more than just generate a function
 * signature with a slightly mangled name. It tells the python
 * script which scans all of the .test.cpp files that this function
 * should be added to the test runner source code which the script
 * generates.
 */
#define TESTFUNC(FUNC_NAME) void testsys_function_##FUNC_NAME()

#define TESTSYS_EXPAND_STRINGIFY(X) #X
#define TESTSYS_STRINGIFY(X) TESTSYS_EXPAND_STRINGIFY(X)

/* The TESTASSERT macro is used in test functions to test
 * a condition and report a test failure if the condition is
 * not met.
 */
#define TESTASSERT(COND) if(not (COND)){\
    throw std::runtime_error(#COND " is false | " __FILE__ " at line " TESTSYS_STRINGIFY(__LINE__)); \
  }

/* The TESTTHROW macro tests whether the code STMT throws an exception whose what()
 * string contains WHAT_SUBSTR, and reports a failure if it does not.
 */
#define TESTTHROW(STMT,WHAT_SUBSTR) {\
  bool testsys_testthrow_exception_thrown = false; \
  try{ STMT ;} \
  catch(std::exception & ex){ \
    if(std::string(ex.what()).find(WHAT_SUBSTR) != std::string::npos){  \
      testsys_testthrow_exception_thrown = true; \
    } \
    else{ \
      throw std::runtime_error(#STMT " threw unexpected error -- " + std::string(ex.what()) + " | " \
                             __FILE__ " at line " TESTSYS_STRINGIFY(__LINE__)); \
    } \
  } \
  if(not testsys_testthrow_exception_thrown){ \
    throw std::runtime_error(#STMT " did not throw expected error -- " #WHAT_SUBSTR " | " \
                             __FILE__ " at line " TESTSYS_STRINGIFY(__LINE__)); \
  } \
}

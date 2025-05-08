#include "testsys.h"
#include "../Session.h"

TESTFUNC(msg)
{
  Session session("testing");
  TESTASSERT(session.msg() == std::string("testing")); // this test should pass
  TESTASSERT(session.msg() == std::string("wrong string")); // this test should fail
}

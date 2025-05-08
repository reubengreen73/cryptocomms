#include "Session.h"

Session::Session(std::string msg): _msg(msg)
{}

std::string Session::msg()
{
  return _msg;
}

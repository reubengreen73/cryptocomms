#include "testsys.h"

#include <string>
#include <stdexcept>
#include <ctime>

/* time_now_string() returns the current local time as a string in the
 * format hh::mm::ss
 */
std::string time_now_string()
{
 std::time_t now_time = std::time(nullptr);
 if(now_time == static_cast<std::time_t>(-1)){
   throw std::runtime_error("could not get current time");
 }

 std::tm* now_time_local = std::localtime(&now_time);
 if(now_time_local == nullptr){
   throw std::runtime_error("could not convert time to local time");
 }

 char buff[9];
 if(std::strftime(buff,9,"%T",now_time_local) != 8){
   throw std::runtime_error("could not format time string");
 }

 return std::string(buff);
}

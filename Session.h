#ifndef SESSION_H
#define SESSION_H

#include <string>

class Session{
public:
  Session(std::string msg);
  std::string msg();
private:
  std::string _msg;
};

#endif

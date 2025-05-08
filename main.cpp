#include <iostream>

#include "Session.h"

int main(){
  std::cout << "Hello!" << std::endl;

  Session session(std::string("Hello, session!"));
  std::cout << session.msg() << std::endl;

  return 0;
}

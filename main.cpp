#include <iostream>
#include <fstream>

#include "SegmentNumGenerator.h"
#include "PeerConfig.h"
#include "Session.h"

int main(){
  std::cout << "Hello!" << std::endl;

  std::ofstream segnum_file("segnumfile",std::ios::trunc);
  if(!segnum_file){
       throw std::runtime_error(std::string("Could not open \"segnumfile\""));
  }
  segnum_file << std::to_string(0);
  segnum_file.close();

  Session session("segnumfile");
  std::cout << session.segnumgen.next_num() << std::endl;

  return 0;
}

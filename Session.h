#ifndef SESSION_H
#define SESSION_H

#include <string>

#include "SegmentNumGenerator.h"

class Session{
public:
  Session(std::string segnum_file_path, int segnums_reserved);
  SegmentNumGenerator segnumgen;
private:
};

#endif

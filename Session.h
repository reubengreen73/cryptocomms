#ifndef SESSION_H
#define SESSION_H

#include <string>

#include "SegmentNumGenerator.h"
#include "PeerConfig.h"

class Session{
public:
  Session(std::string segnum_file_path);
  SegmentNumGenerator segnumgen;
private:
};

#endif

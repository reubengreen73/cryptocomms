#include "Session.h"

Session::Session(std::string segnum_file_path):
  segnumgen(segnum_file_path)
{
  segnumgen.set_reserved(100);
}

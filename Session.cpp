#include "Session.h"

Session::Session(std::string segnum_file_path, int segnums_reserved):
  segnumgen(segnum_file_path,segnums_reserved)
{
}

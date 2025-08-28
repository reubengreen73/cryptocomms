/* this header defines types used as id values for hosts and channels */

#ifndef IDTYPES_H
#define IDTYPES_H

#include <array>

constexpr int host_id_size = 4;
constexpr int channel_id_size = 2;

typedef std::array<unsigned char,host_id_size> host_id_type;
typedef std::array<unsigned char,channel_id_size> channel_id_type;

#endif

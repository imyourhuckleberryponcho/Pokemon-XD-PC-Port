#ifndef XD_N3DS_DUMP_H
#define XD_N3DS_DUMP_H

#include <string>

namespace xd {

bool IdentifyN3dsDump(const std::string& path, std::string& gameId, std::string& title, std::string& error);

}

#endif

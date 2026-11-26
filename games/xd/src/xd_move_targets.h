#ifndef XD_MOVE_TARGETS_H
#define XD_MOVE_TARGETS_H

#include <string>

namespace xd::movetargets {

int PatchPackMoves(std::string& text, bool gen57);

int SetField(std::string& text, const std::string& move, const std::string& field, int value);

}

#endif

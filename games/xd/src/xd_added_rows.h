#ifndef XD_ADDED_ROWS_H
#define XD_ADDED_ROWS_H

#include <mods.h>

#include <map>
#include <string>
#include <vector>

namespace xd::added {

void Install(const engine::mods::LoadResult& result);

const std::vector<engine::mods::AddedRow>& Rows();
const std::map<std::string, uint64_t>& Digests();

}

#endif

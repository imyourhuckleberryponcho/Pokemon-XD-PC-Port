#pragma once
#include "n3ds_data.h"

namespace n3ds {

struct PackFiles {
  std::string root;
  bool crlf = false;
  void write(const std::string& rel, const std::string& text, bool binary_lf = false, bool ansi = false) const;
  void write_bytes(const std::string& rel, const Bytes& b) const;
};
std::string slurp_text(const std::string& path);
Bytes slurp_bytes(const std::string& path);
Json read_json(const std::string& path);
std::string slurp_lines_lf(const std::string& path);
const Json& jget(const Json& o, const char* k);
bool jtruthy(const Json* v);
Json jints(const std::vector<int>& v);
Json jstrs(const std::vector<std::string>& v);

struct RowBridge { Json row, rep; std::vector<std::string> notes; };
RowBridge build_row_bridge(Usum& u, const Json& tables, const XdTablesCache& xd, const std::set<int>& pre, int nat, int record, int pkx_row, int standin);

struct MovesBuilt {
  std::vector<std::vector<std::pair<std::string, Json>>> rows;
  Json battle;
  Json report;
};
MovesBuilt build_all_moves(Usum& u, const Json& tables, const Json& xd_cache, const std::vector<std::string>& langs);
void build_all_abilities(Usum& u, const Json& tables, Json& defs, Json& report);
std::string ability_lines(Usum& u, const Json& tables, const std::vector<int>& ids, const std::set<int>& built);
std::vector<std::pair<int, int>> build_hidden(Usum& u, int last);

}

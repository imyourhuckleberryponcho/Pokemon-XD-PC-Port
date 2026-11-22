#include "pack_tables.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace xd::pack {

bool ReadText(const std::filesystem::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool ParseTables(const std::string& text, PackTables& out, std::string& error) {
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "format") {
      int v = 0;
      ls >> v;
      if (v != 1) {
        error = "tables.txt has format " + std::to_string(v) + ", this game reads 1";
        return false;
      }
    } else if (key == "pkx") {
      PkxRow r{};
      ls >> r.row >> r.group >> r.res;
      out.pkx.push_back(r);
    } else if (key == "pkxcount") {
      ls >> out.pkxExpect;
    } else if (key == "row") {
      RowSpec r;
      ls >> r.table >> r.row >> r.count >> r.copyOf;
      std::string s;
      while (ls >> s) {
        unsigned a = 0, b = 0, c = 0;
        if (std::sscanf(s.c_str(), "%u:%u:%u", &a, &b, &c) != 3) {
          error = "tables.txt: \"" + s + "\" is not offset:size:value";
          return false;
        }
        r.sets.push_back(a), r.sets.push_back(b), r.sets.push_back(c);
      }
      out.rows.push_back(std::move(r));
    } else if (key == "wave") {
      std::string hex;
      unsigned dex = 0;
      ls >> dex >> hex;
      std::vector<uint8_t> w;
      for (size_t k = 0; k + 1 < hex.size(); k += 2) w.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(k, 2).c_str(), nullptr, 16)));
      if (w.empty() || w.size() != static_cast<size_t>(w[0]) + 1) {
        error = "tables.txt: the wave of species " + std::to_string(dex) + " has the wrong length";
        return false;
      }
      out.waves.push_back(std::move(w));
    } else if (key == "face") {
    } else if (key == "goods") {
      GoodsRow g;
      ls >> g.item;
      out.goods.push_back(std::move(g));
    } else if (key == "goodscopy") {
      GoodsRow g;
      ls >> g.copyOf;
      out.goods.push_back(std::move(g));
    } else if (key == "species") {
      SpeciesRef r;
      ls >> r.dex >> r.id >> r.key;
      out.species.push_back(r);
    } else {
      error = "tables.txt: unknown line \"" + key + "\"";
      return false;
    }
  }
  return true;
}

}

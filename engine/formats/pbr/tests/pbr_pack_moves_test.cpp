#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "folder_disc.h"
#include "pbr_pack.h"

class FakeXd : public pbr::XdTables {
 public:
  explicit FakeXd(const char* path) {
    std::ifstream in(path);
    std::string line;
    std::vector<pbr::XdField>* cur = nullptr;
    while (std::getline(in, line)) {
      if (line.rfind("R ", 0) == 0) {
        std::istringstream ls(line.substr(2));
        unsigned n;
        ls >> n;
        std::string name;
        std::getline(ls, name);
        if (!name.empty()) name.erase(0, 1);
        names_[n] = name;
        cur = &rows_[n];
      } else if (line.rfind("F ", 0) == 0 && cur) {
        std::istringstream ls(line.substr(2));
        pbr::XdField f;
        uint32_t v;
        ls >> f.name >> v;
        f.values.push_back(v);
        cur->push_back(f);
      }
    }
  }
  std::vector<std::pair<uint32_t, std::string>> names(const std::string&) override { return {}; }
  bool row(const std::string& table, uint32_t entry, std::string& name, std::vector<pbr::XdField>& fields) override {
    if (table != "waza_data" || !rows_.count(entry)) return false;
    name = names_[entry];
    fields = rows_[entry];
    return true;
  }

 private:
  std::map<uint32_t, std::vector<pbr::XdField>> rows_;
  std::map<uint32_t, std::string> names_;
};

static std::string slurp(const char* p) {
  std::ifstream f(p, std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), o;
  for (char c : s)
    if (c != '\r') o += c;
  return o;
}

int main(int argc, char** argv) {
  if (argc < 5) return 2;
  FolderDisc disc(argv[1], "RPBE01");
  pbr::PbrData pbr(disc);
  FakeXd xd(argv[2]);
  pbr::MovesOut m = pbr::gen4_moves(pbr, xd);
  const std::string want = slurp(argv[3]);
  const std::string cls = pbr::gen4_moves_classification(pbr, xd);
  const std::string wantc = slurp(argv[4]);
  const std::string& wantcn = wantc;
  std::printf("moves.json: %s (%zu bytes)\n", m.json == want ? "IDENTICAL" : "DIFFERENT", m.json.size());
  std::printf("classification: %s\n", cls == wantcn ? "IDENTICAL" : "DIFFERENT");
  std::printf("built: %zu\n", m.built.size());
  if (cls != wantcn) {
    size_t j = 0;
    while (j < cls.size() && j < wantcn.size() && cls[j] == wantcn[j]) j++;
    std::printf("classification differs at %zu: mine ...%s\n theirs ...%s\n", j, cls.substr(j > 40 ? j - 40 : 0, 100).c_str(),
                wantcn.substr(j > 40 ? j - 40 : 0, 100).c_str());
  }
  if (m.json != want) {
    size_t i = 0;
    while (i < m.json.size() && i < want.size() && m.json[i] == want[i]) i++;
    std::printf("first difference at %zu: ...%s\n", i, m.json.substr(i > 60 ? i - 60 : 0, 120).c_str());
  }
  return m.json == want && cls == wantcn ? 0 : 1;
}

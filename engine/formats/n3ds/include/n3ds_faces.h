#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "n3ds_dump.h"
#include "n3ds_gf.h"

namespace n3ds {

struct FaceView {
  double x = 0, y = 0, z = 0, r = 0, yaw = 18.0, pitch = 6.0;
  std::string how;
};

bool face_view(const GfModel& gm, const Bytes& pkx, int nat, bool wide, FaceView* out);
std::string view_key(int nat, const std::string& file_hash, bool wide);
double framing_share(int nat);
Bytes fsys_pkx(const Bytes& fsys);

using RunProcessFn = std::function<int(const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>* env, const std::string& logfile)>;

struct FacesArgs {
  std::string xd_exe, xd_disc, pack, gen4_pack, work, data_dir, dump_fp;
  const Garc* models = nullptr;
  std::map<int, int> nat_group;
  std::set<int> nats;
  int jobs = 8, xd_runs = 1;
  bool eol_crlf = false;
  RunProcessFn run;
  std::function<void(int, int, const std::string&)> progress;
  std::function<void(const std::string&)> log;
};
void make_faces(const FacesArgs& a);

}

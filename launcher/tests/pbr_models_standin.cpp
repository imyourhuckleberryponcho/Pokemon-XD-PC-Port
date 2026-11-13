#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Env(const char* name, const char* fallback) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' ? v : fallback;
}

struct Model {
  std::string name, archive, entry;
  fs::path source;
};

}

int main(int argc, char** argv) {
  std::string pbr, xd, out;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--version") {
      std::printf("pbr_models %s (stand-in)\n", Env("XD_STANDIN_VERSION", "1.0.0").c_str());
      return 0;
    }
    if (i + 1 < argc && a == "--pbr-disc") pbr = argv[++i];
    else if (i + 1 < argc && a == "--xd-disc") xd = argv[++i];
    else if (i + 1 < argc && a == "--out") out = argv[++i];
  }
  std::error_code ec;
  if (pbr.empty() || xd.empty() || out.empty()) {
    std::printf("error needs --pbr-disc, --xd-disc and --out\n");
    return 1;
  }
  if (!fs::is_regular_file(pbr, ec) || !fs::is_regular_file(xd, ec)) {
    std::printf("error a disc image is not there\n");
    return 1;
  }
  if (Env("XD_STANDIN_WRONG_DISC", "1") != "0") {
    char id[6] = {};
    std::ifstream in(pbr, std::ios::binary);
    in.read(id, 6);
    if (std::string(id, 6) != "RPBE01") {
      std::printf("error that is not a Pokemon Battle Revolution (USA) disc\n");
      return 2;
    }
  }
  std::vector<Model> models;
  const std::string source = Env("XD_STANDIN_SOURCE", "");
  const size_t count = static_cast<size_t>(std::atoi(Env("XD_STANDIN_COUNT", "12").c_str()));
  if (!source.empty()) {
    std::ifstream pairs(fs::path(source) / "pairs.tsv");
    for (std::string line; std::getline(pairs, line) && models.size() < count;) {
      std::vector<std::string> c;
      std::stringstream ss(line);
      for (std::string f; std::getline(ss, f, '\t');) c.push_back(f);
      if (c.size() < 4 || c[1].empty() || c[2].empty()) continue;
      const fs::path file = fs::path(source) / "out" / (c[1] + ".pkx");
      if (fs::is_regular_file(file, ec)) models.push_back({c[1], c[2], c[3], file});
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      models.push_back({"model" + std::to_string(i + 1), "pkx_model" + std::to_string(i + 1) + ".fsys", "0", {}});
    }
  }
  const fs::path tmp = fs::path(out + ".tmp");
  fs::remove_all(tmp, ec);
  fs::create_directories(tmp, ec);
  const std::string version = Env("XD_STANDIN_VERSION", "1.0.0");
  {
    std::ofstream json(tmp / "mod.json");
    json << "{\"id\": \"battle-revolution-models\", \"name\": \"Battle Revolution models\", \"version\": \"" << version
         << "\", \"author\": \"made on this PC by pbr_models\", \"description\": \"Pokemon Battle Revolution's models "
            "for the Pokemon in battle.\", \"game\": \"GXXE01\", \"applies\": \"restart\"}\n";
  }
  const int delay = std::atoi(Env("XD_STANDIN_DELAY_MS", "60").c_str());
  const std::string fail = Env("XD_STANDIN_FAIL", "");
  uint64_t bytes = 0;
  for (size_t i = 0; i < models.size(); i++) {
    std::printf("progress %zu %zu %s\n", i, models.size(), models[i].name.c_str());
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    if (!fail.empty() && i == models.size() / 2) {
      std::printf("error %s\n", fail.c_str());
      return 1;
    }
    const fs::path dir = tmp / "fsys" / models[i].archive;
    fs::create_directories(dir, ec);
    const fs::path file = dir / ("#" + models[i].entry);
    if (!models[i].source.empty()) {
      fs::copy_file(models[i].source, file, fs::copy_options::overwrite_existing, ec);
    } else {
      std::ofstream(file, std::ios::binary) << "stand-in model " << models[i].name;
    }
    bytes += fs::file_size(file, ec);
  }
  fs::remove_all(out, ec);
  fs::rename(tmp, out, ec);
  if (ec) {
    std::printf("error could not finish the folder: %s\n", ec.message().c_str());
    return 1;
  }
  std::printf("done %zu 0 %llu\n", models.size(), static_cast<unsigned long long>(bytes));
  return 0;
}

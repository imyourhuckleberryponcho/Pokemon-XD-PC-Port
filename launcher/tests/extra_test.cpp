



#include "launcher_extra.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
namespace ex = launcher::extra;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
  do {                                                                                                                 \
    const std::string _a = (a), _b = (b);                                                                              \
    if (_a != _b) {                                                                                                    \
      std::fprintf(stderr, "FAILED line %d: %s\n  got:      [%s]\n  expected: [%s]\n", __LINE__, #a, _a.c_str(),     \
                   _b.c_str());                                                                                        \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static void Write(const fs::path& file, const std::string& data) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary);
  out << data;
}

static void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

static launcher::GameInfo::ExtraSource PbrSource() {
  launcher::GameInfo::ExtraSource s;
  s.id = "pbr";
  s.name = "Pokemon Battle Revolution (USA)";
  s.gameIds = {"RPBE01"};
  s.manifestId = "battle-revolution-models";
  s.converterStem = "pbr_models";
  s.converterFolder = "pbr_models";
  s.converterOverrideEnv = "XD_TEST_CONVERTER_OVERRIDE";
  s.converterArguments = {"--pbr-disc", "{disc}", "--xd-disc", "{game_disc}", "--out", "{out}"};
  return s;
}

static void TestLines() {
  using K = ex::Line::Kind;
  ex::Line l = ex::ParseLine("progress 3 400 Pidgeot (flying)\r");
  CHECK(l.kind == K::Progress && l.done == 3 && l.total == 400 && l.name == "Pidgeot (flying)");
  l = ex::ParseLine("progress 0 12 absol");
  CHECK(l.kind == K::Progress && l.done == 0 && l.total == 12 && l.name == "absol");
  l = ex::ParseLine("progress 5 9");
  CHECK(l.kind == K::Progress && l.name.empty());
  l = ex::ParseLine("done 380 8 123456789012\n");
  CHECK(l.kind == K::Done && l.converted == 380 && l.keptOriginal == 8 && l.totalBytes == 123456789012ull);
  l = ex::ParseLine("error The disc is damaged: read failed at 0x1000 ");
  CHECK(l.kind == K::Error && l.message == "The disc is damaged: read failed at 0x1000");
  CHECK(ex::ParseLine("error").kind == K::Error && ex::ParseLine("error").message.empty());
  CHECK(ex::ParseLine("progress x 3 a").kind == K::None);
  CHECK(ex::ParseLine("progress 1").kind == K::None);
  CHECK(ex::ParseLine("done 1 2").kind == K::None);
  CHECK(ex::ParseLine("progress -1 3 a").kind == K::None);
  CHECK(ex::ParseLine("loading library...").kind == K::None);
  CHECK(ex::ParseLine("").kind == K::None && ex::ParseLine("   ").kind == K::None);
  CHECK(ex::ParseLine("  progress 1 2 b").kind == K::Progress);

  CHECK_EQ(ex::ParseVersionOutput("pbr_models 1.2.0\n"), "1.2.0");
  CHECK_EQ(ex::ParseVersionOutput("1.10\r\n"), "1.10");
  CHECK_EQ(ex::ParseVersionOutput("pbr_models v2.0.1 (build 7)"), "2.0.1");
  CHECK_EQ(ex::ParseVersionOutput("pbr_models (stand-in)"), "");
  CHECK_EQ(ex::ParseVersionOutput(""), "");
}

static void TestTable() {
  const launcher::GameInfo::ExtraSource s = PbrSource();
  std::vector<launcher::GameInfo::ExtraSource> table{s};
  CHECK(ex::FindSource(table, "pbr") == &table[0] && ex::FindSource(table, "PBR") == &table[0]);
  CHECK(ex::FindSource(table, "colosseum") == nullptr);
  CHECK(ex::AcceptsGameId(s, "RPBE01") && ex::AcceptsGameId(s, "rpbe01"));
  CHECK(!ex::AcceptsGameId(s, "GXXE01") && !ex::AcceptsGameId(s, "RPBP01") && !ex::AcceptsGameId(s, ""));
  const auto args = ex::ConverterArguments(s, "D:\\iso\\pbr.iso", "D:\\iso\\xd.iso", fs::path("C:\\data\\extra_assets\\pbr"));
  CHECK(args.size() == 6 && args[0] == "--pbr-disc" && args[1] == "D:\\iso\\pbr.iso" && args[2] == "--xd-disc" &&
        args[3] == "D:\\iso\\xd.iso" && args[4] == "--out" && args[5].find("pbr") != std::string::npos);
  SetEnv("XD_TEST_CONVERTER_OVERRIDE", "");
  const fs::path normal = ex::ConverterPath(s, fs::path("game"));
  CHECK(normal.parent_path().filename() == "pbr_models" && normal.stem() == "pbr_models");
  SetEnv("XD_TEST_CONVERTER_OVERRIDE", "somewhere/else/tool");
  CHECK(ex::ConverterPath(s, fs::path("game")) == fs::path("somewhere/else/tool"));
  SetEnv("XD_TEST_CONVERTER_OVERRIDE", "");
}

static std::string ModJson(const std::string& id, const std::string& version) {
  return "{\"id\": \"" + id + "\", \"name\": \"m\", \"version\": \"" + version + "\", \"game\": \"GXXE01\"}";
}

static void TestStatus(const fs::path& root) {
  const fs::path mod = root / "status" / "battle-revolution-models";
  using K = ex::Status::Kind;
  ex::Status st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.0");
  CHECK(st.kind == K::NotSetUp && st.text == "Not set up");
  st = ex::ComputeStatus(mod, "battle-revolution-models", false, "");
  CHECK(st.kind == K::ConverterMissing && st.text == "Converter missing" && !st.detail.empty());
  Write(mod / "mod.json", ModJson("battle-revolution-models", "1.0.0"));
  for (int i = 0; i < 3; i++) Write(mod / "fsys" / ("pkx_m" + std::to_string(i) + ".fsys") / "#0", "x");
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.0");
  CHECK(st.kind == K::Ready && st.models == 3 && st.text == "Ready: 3 models" && st.version == "1.0.0");
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "");
  CHECK(st.kind == K::Ready);
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.1");
  CHECK(st.kind == K::OutOfDate && st.text == "Out of date: make again");
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "0.9");
  CHECK(st.kind == K::Ready);
  st = ex::ComputeStatus(mod, "battle-revolution-models", false, "");
  CHECK(st.kind == K::Ready && !st.detail.empty());
  Write(mod / "mod.json", ModJson("battle-revolution-models", "1.10"));
  CHECK(ex::ComputeStatus(mod, "battle-revolution-models", true, "1.9").kind == K::Ready);
  Write(mod / "mod.json", ModJson("another-mod", "1.0.0"));
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.0");
  CHECK(st.kind == K::NotSetUp && st.detail.find("another-mod") != std::string::npos);
  Write(mod / "mod.json", "{ this is not json");
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.0");
  CHECK(st.kind == K::NotSetUp && !st.detail.empty());
  Write(mod / "mod.json", ModJson("battle-revolution-models", "1.0.0"));
  fs::remove_all(mod / "fsys");
  st = ex::ComputeStatus(mod, "battle-revolution-models", true, "1.0.0");
  CHECK(st.kind == K::Ready && st.models == 0 && st.text == "Ready: 0 models");
}

static void TestDiscCheck() {
  launcher::GameInfo::ExtraAssets assets;
  assets.identifyDisc = [](const std::string& path, std::string& id, std::string& title, std::string& error) {
    if (path == "wii.iso") return id = "RPBE01", title = "Pokemon Battle Revolution", true;
    if (path == "xd.iso") return id = "GXXE01", title = "Pokemon XD: Gale of Darkness", true;
    error = "not a disc image";
    return false;
  };
  const launcher::GameInfo::ExtraSource s = PbrSource();
  const auto wait = [](ex::DiscCheck& c) {
    for (int i = 0; i < 200 && c.state() == ex::DiscCheck::State::Checking; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  };
  ex::DiscCheck c;
  CHECK(c.state() == ex::DiscCheck::State::None);
  c.Start(s, "wii.iso", assets);
  wait(c);
  CHECK(c.state() == ex::DiscCheck::State::Good && c.forPath() == "wii.iso" && c.message().find("RPBE01") != std::string::npos);
  c.Start(s, "xd.iso", assets);
  wait(c);
  CHECK(c.state() == ex::DiscCheck::State::WrongGame);
  CHECK(c.message().find("Pokemon XD: Gale of Darkness") != std::string::npos && c.message().find("GXXE01") != std::string::npos &&
        c.message().find("Pokemon Battle Revolution (USA)") != std::string::npos);
  c.Start(s, "text.txt", assets);
  wait(c);
  CHECK(c.state() == ex::DiscCheck::State::Unreadable && c.message().find("not a disc image") != std::string::npos);
  c.Clear();
  CHECK(c.state() == ex::DiscCheck::State::None && c.forPath().empty());
}


static ex::MakeJob::Snapshot RunStandIn(const fs::path& standin, const fs::path& root, const std::string& pbrDisc,
                                        int cancelAfterMs, ex::MakeJob::Snapshot* midway = nullptr) {
  ex::MakeJob job;
  ex::MakeRequest request;
  request.converter = standin;
  request.outFolder = root / "extra_assets" / "pbr";
  request.logFile = root / "extra" / "pbr.log";
  request.arguments = {"--pbr-disc", pbrDisc, "--xd-disc", (root / "xd.iso").string(), "--out", request.outFolder.string()};
  std::string error;
  CHECK(job.Start(request, error));
  CHECK(job.Running());
  const auto began = std::chrono::steady_clock::now();
  bool cancelled = false;
  while (job.Running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
    if (midway != nullptr && midway->phase == ex::MakeJob::Phase::Idle && job.Get().done >= 2) *midway = job.Get();
    if (cancelAfterMs > 0 && !cancelled && ms >= cancelAfterMs) {
      job.Cancel();
      cancelled = true;
    }
    if (ms > 20000) {
      std::fprintf(stderr, "the stand-in did not finish\n");
      sFailures++;
      break;
    }
  }

  return job.Get();
}

static void TestMake(const fs::path& standin, const fs::path& root) {
  using P = ex::MakeJob::Phase;
  fs::create_directories(root / "extra_assets");
  Write(root / "xd.iso", "GXXE01 xd");
  Write(root / "pbr.iso", "RPBE01 a made-up header only");
  Write(root / "xd2.iso", "GXXE01 not pbr");
  SetEnv("XD_STANDIN_COUNT", "6");
  SetEnv("XD_STANDIN_DELAY_MS", "20");
  SetEnv("XD_STANDIN_VERSION", "2.3.4");
  const fs::path out = root / "extra_assets" / "pbr";


  Write(fs::path(out.string() + ".tmp") / "junk", "x");
  ex::MakeJob::Snapshot mid;
  ex::MakeJob::Snapshot s = RunStandIn(standin, root, (root / "pbr.iso").string(), 0, &mid);
  CHECK(s.phase == P::Done);
  CHECK(s.converted == 6 && s.keptOriginal == 0 && s.totalBytes > 0 && s.total == 6 && s.done == 5);
  CHECK(mid.done >= 2 && !mid.current.empty());
  CHECK(fs::is_regular_file(out / "mod.json") && fs::is_directory(out / "fsys" / "pkx_model1.fsys") &&
        !fs::exists(fs::path(out.string() + ".tmp")));
  ex::Status st = ex::ComputeStatus(out, "battle-revolution-models", true, ex::QueryVersion(standin, root / "extra" / "ver.log", 5000));
  CHECK(st.kind == ex::Status::Kind::Ready && st.models == 6 && st.version == "2.3.4");
  CHECK_EQ(ex::QueryVersion(standin, root / "extra" / "ver.log", 5000), "2.3.4");
  CHECK_EQ(ex::QueryVersion(root / "nothing-here.exe", root / "extra" / "ver.log", 1000), "");
  SetEnv("XD_STANDIN_VERSION", "3.0.0");
  st = ex::ComputeStatus(out, "battle-revolution-models", true, ex::QueryVersion(standin, root / "extra" / "ver.log", 5000));
  CHECK(st.kind == ex::Status::Kind::OutOfDate);
  SetEnv("XD_STANDIN_VERSION", "2.3.4");


  SetEnv("XD_STANDIN_COUNT", "3");
  s = RunStandIn(standin, root, (root / "pbr.iso").string(), 0);
  CHECK(s.phase == P::Done && s.converted == 3);
  int folders = 0;
  for (auto& e : fs::directory_iterator(out / "fsys")) folders += e.is_directory() ? 1 : 0;
  CHECK(folders == 3);


  s = RunStandIn(standin, root, (root / "xd2.iso").string(), 0);
  CHECK(s.phase == P::WrongDisc && s.message.find("Battle Revolution") != std::string::npos);
  CHECK(fs::is_regular_file(out / "mod.json") && !fs::exists(fs::path(out.string() + ".tmp")));


  SetEnv("XD_STANDIN_COUNT", "8");
  SetEnv("XD_STANDIN_FAIL", "the disc is damaged near the Pokemon models");
  s = RunStandIn(standin, root, (root / "pbr.iso").string(), 0);
  CHECK(s.phase == P::Failed && s.message == "the disc is damaged near the Pokemon models");
  CHECK(!fs::exists(fs::path(out.string() + ".tmp")) && fs::is_regular_file(out / "mod.json"));
  SetEnv("XD_STANDIN_FAIL", "");


  SetEnv("XD_STANDIN_COUNT", "40");
  SetEnv("XD_STANDIN_DELAY_MS", "100");
  const auto began = std::chrono::steady_clock::now();
  s = RunStandIn(standin, root, (root / "pbr.iso").string(), 500);
  const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
  CHECK(s.phase == P::Cancelled);
  CHECK(took < 3000);
  CHECK(!fs::exists(fs::path(out.string() + ".tmp")) && fs::is_regular_file(out / "mod.json"));

  fs::remove_all(out);
  s = RunStandIn(standin, root, (root / "pbr.iso").string(), 500);
  CHECK(s.phase == P::Cancelled && !fs::exists(out) && !fs::exists(fs::path(out.string() + ".tmp")));
  CHECK(!fs::exists(root / "extra" / "pbr.log"));


  ex::MakeJob job;
  ex::MakeRequest request;
  request.converter = root / "no-such-converter.exe";
  request.outFolder = out;
  request.logFile = root / "extra" / "pbr.log";
  std::string error;
  CHECK(!job.Start(request, error) && !error.empty() && !job.Running());


  Write(fs::path(out.string() + ".tmp") / "a", "1");
  Write(out / "keep", "2");
  ex::RemoveLeftovers(out);
  CHECK(!fs::exists(fs::path(out.string() + ".tmp")) && fs::exists(out / "keep"));
}

int main(int argc, char** argv) {
  const fs::path self = fs::absolute(argv[0]);
  const fs::path root = argc > 1 ? fs::absolute(argv[1]) : self.parent_path() / "launcher_extra_test_work";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  fs::path standin = self.parent_path() / "pbr_models_standin";
#ifdef _WIN32
  standin += ".exe";
#endif
  TestLines();
  TestTable();
  TestStatus(root);
  TestDiscCheck();
  if (fs::is_regular_file(standin, ec)) {
    TestMake(standin, root);
  } else {
    std::fprintf(stderr, "FAILED: %s is not next to this program\n", standin.string().c_str());
    sFailures++;
  }
  fs::remove_all(root, ec);
  std::printf(sFailures == 0 ? "launcher_extra_test: all passed\n" : "launcher_extra_test: %d FAILED\n", sFailures);
  return sFailures == 0 ? 0 : 1;
}

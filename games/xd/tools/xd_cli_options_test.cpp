#include "../src/cli_options.h"
#include "../src/xd_test_run.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

bool sOk = true;
void check(bool cond, const char* what) {
  std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
  sOk &= cond;
}

xd::cli::Options Parse(std::vector<const char*> args) {
  args.insert(args.begin(), "xd.exe");
  return xd::cli::Parse(static_cast<int>(args.size()), args.data());
}

}

int main() {
  using xd::cli::Options;
  {
    const Options o = Parse({});
    check(o.error.empty() && o.disc.empty() && !o.ChangesRun() && !o.help && !o.version && !o.noLauncher,
          "no arguments: nothing changes");
  }
  {
    const Options o = Parse({"--replay", "r.xdreplay", "--turn", "3", "a.iso"});
    check(o.error.empty() && o.replay == "r.xdreplay" && o.replayTurn == "3" && o.disc == "a.iso", "--replay and --turn are understood");
    bool seen = false;
    for (const auto& [n, v] : EnvFor(o, "")) seen = seen || (n == "XD_BATTLE_REPLAY" && v == "r.xdreplay");
    check(seen, "--replay becomes XD_BATTLE_REPLAY");
    check(!Parse({"--turn", "x"}).error.empty(), "--turn needs a number");
  }
  {
    const Options o = Parse({"C:\games\xd.iso"});
    check(o.error.empty() && o.disc == "C:\games\xd.iso" && !o.ChangesRun(), "the old way: a plain argument is the disc");
  }
  {
    const Options o = Parse({"--disc", "a.iso", "--fullscreen", "--preset", "Original", "--save", "My save",
                             "--data-folder", "D:\xd data"});
    check(o.error.empty(), "every option together parses");
    check(o.disc == "a.iso" && o.fullscreen == 1 && o.preset == "original" && o.save == "My save" &&
              o.dataFolder == "D:\xd data",
          "values are kept (preset is lower case, spaces survive)");
    check(o.ChangesRun(), "this run is changed");
  }
  {
    const Options o = Parse({"--disc=a b.iso", "--windowed", "--preset=enhanced"});
    check(o.error.empty() && o.disc == "a b.iso" && o.fullscreen == 0 && o.preset == "enhanced", "--name=value works");
  }
  check(Parse({"--help"}).help && Parse({"-h"}).help && Parse({"/?"}).help, "--help, -h, /?");
  check(Parse({"--version"}).version, "--version");
  check(Parse({"--no-launcher"}).noLauncher, "--no-launcher");
  check(Parse({"-psn_0_12345", "x.iso"}).disc == "x.iso", "the Finder's -psn_ is dropped");
  check(Parse({"--", "--odd.iso"}).disc == "--odd.iso", "after -- everything is a file name");
  check(Parse({"a.iso", "b.iso"}).notes.size() == 1 && Parse({"a.iso", "b.iso"}).disc == "a.iso", "a second plain argument is left out");
  check(Parse({""}).disc.empty() && Parse({""}).error.empty(), "an empty argument (the launcher with no disc) is nothing");

  check(!Parse({"--preset", "fancy"}).error.empty(), "a wrong preset is a mistake");
  check(!Parse({"--preset"}).error.empty(), "a missing value is a mistake");
  check(!Parse({"--data-folder", ""}).error.empty(), "an empty folder is a mistake");
  check(!Parse({"--fullscreen", "--windowed"}).error.empty(), "full screen and window together is a mistake");
  check(!Parse({"--bogus"}).error.empty(), "an unknown option is a mistake");
  check(!Parse({"--fullscreen=yes"}).error.empty(), "a value on a plain switch is a mistake");

  {
    const Options o = Parse({"--preset", "original", "--fullscreen", "--save", "s2", "--data-folder", "F:\d", "--disc",
                             "x.iso", "--no-launcher"});
    std::map<std::string, std::string> env;
    for (const auto& [k, v] : xd::cli::EnvFor(o, "")) env[k] = v;
    check(env["XD_PRESET"] == "original" && env["XD_SETTINGS"] == "video.fullscreen=on" && env["XD_SAVE_NAME"] == "s2" &&
              env["XD_DATA_FOLDER"] == "F:\d" && env.size() == 4,
          "the run-only switches (the disc and --no-launcher are not environment)");
    const auto get = [&](const char* name) -> const char* {
      const auto it = env.find(name);
      return it == env.end() ? nullptr : it->second.c_str();
    };
    check(xd::TestRunSwitchSet(get) == nullptr, "options never make a run a test run");
    check(!xd::PlayerFilesReadOnly(get), "a run with options may still write the player's settings");
  }
  {
    const Options o = Parse({"--windowed"});
    const auto env = xd::cli::EnvFor(o, "video.widescreen=on");
    check(env.size() == 1 && env[0].second == "video.widescreen=on;video.fullscreen=off",
          "--windowed is added after the XD_SETTINGS the run already has");
  }
  {
    const auto twice = xd::cli::EnvFor(Parse({"--fullscreen"}), "video.fullscreen=on");
    check(twice.size() == 1 && twice[0].second == "video.fullscreen=on", "the same option read twice is added once");
  }
  check(!xd::cli::Summary(Parse({"--preset", "original", "--fullscreen"})).empty(), "a summary line for the log");

  std::printf(sOk ? "PASS\n" : "FAILED\n");
  return sOk ? 0 : 1;
}

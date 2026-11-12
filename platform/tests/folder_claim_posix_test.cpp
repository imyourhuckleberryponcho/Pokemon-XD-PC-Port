






#include <port_folder_claim.h>

#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern char** environ;

static int sFailures = 0;
#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond);                                                   \
      sFailures++;                                                                                                     \
    }                                                                                                                  \
  } while (0)

static std::string sExe;

static double now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) / 1e9;
}


static pid_t Start(const char* mode, const std::string& folder, const char* ms = "0") {
  char* argv[] = {const_cast<char*>(sExe.c_str()), const_cast<char*>(mode), const_cast<char*>(folder.c_str()),
                  const_cast<char*>(ms), nullptr};
  pid_t pid = -1;
  if (posix_spawn(&pid, sExe.c_str(), nullptr, nullptr, argv, environ) != 0) return -1;
  return pid;
}


static int Wait(pid_t pid) {
  int status = 0;
  if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) return 99;
  return WEXITSTATUS(status);
}

static int RunChild(const std::string& folder) { return Wait(Start("child", folder)); }

int main(int argc, char** argv) {
  if (argc >= 3 && std::strcmp(argv[1], "child") == 0) {
    return PortFolderClaim_Take(argv[2]) ? 0 : 1;
  }
  if (argc >= 4 && std::strcmp(argv[1], "hold") == 0) {
    const bool got = PortFolderClaim_Take(argv[2]);
    std::printf("%s\n", got ? "held" : "refused");
    std::fflush(stdout);
    usleep(static_cast<useconds_t>(std::atoi(argv[3])) * 1000);
    return got ? 0 : 1;
  }
  char exe[PATH_MAX];
  if (realpath(argv[0], exe) == nullptr) return 2;
  sExe = exe;
  const char* tmp = std::getenv("TMPDIR");
  const std::string root = std::string((tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp") + "/folder_claim_test_" +
                           std::to_string(getpid());
  mkdir(root.c_str(), 0700);
  const std::string a = root + "/Card";
  const std::string b = root + "/Other";
  mkdir(a.c_str(), 0700);
  unsetenv("PORT_RESET_PARENT");

  CHECK(RunChild(a) == 0);
  CHECK(RunChild(a) == 0);
  CHECK(PortFolderClaim_Take(a.c_str()));
  CHECK(RunChild(a) == 1);
  CHECK(RunChild(a + "/") == 1);
  CHECK(RunChild(root + "/./Other/../Card") == 1);
  const std::string link = root + "/LinkToCard";
  if (symlink(a.c_str(), link.c_str()) == 0) CHECK(RunChild(link) == 1);
  CHECK(RunChild(b) == 0);
  CHECK(!PortFolderClaim_Take(a.c_str()));
  CHECK(PortFolderClaim_Take(nullptr) && PortFolderClaim_Take(""));


  {
    int pipeFd[2];
    CHECK(pipe(pipeFd) == 0);
    const int savedOut = dup(1);
    dup2(pipeFd[1], 1);
    const pid_t holder = Start("hold", b, "1500");
    dup2(savedOut, 1);
    close(savedOut);
    close(pipeFd[1]);
    char line[16] = {};
    CHECK(read(pipeFd[0], line, sizeof line - 1) > 0 && std::strncmp(line, "held", 4) == 0);
    close(pipeFd[0]);
    CHECK(RunChild(b) == 1);
    const std::string pid = std::to_string(holder);
    setenv("PORT_RESET_PARENT", pid.c_str(), 1);
    const double start = now();
    const pid_t restarted = Start("child", b);
    unsetenv("PORT_RESET_PARENT");
    CHECK(Wait(holder) == 0);
    CHECK(Wait(restarted) == 0);
    const double took = now() - start;
    CHECK(took > 0.8 && took < 10.0);
    std::printf("restarted copy got the folder after %.2f s (old copy held it for 1.5 s)\n", took);

    const pid_t holder2 = Start("hold", b, "600");
    usleep(300000);
    const std::string me = std::to_string(getpid());
    setenv("PORT_RESET_PARENT", me.c_str(), 1);
    CHECK(RunChild(b) == 0);
    unsetenv("PORT_RESET_PARENT");
    CHECK(Wait(holder2) == 0);
  }

  {
    const pid_t holder = Start("hold", b, "30000");
    usleep(300000);
    CHECK(RunChild(b) == 1);
    kill(holder, SIGKILL);
    int status = 0;
    waitpid(holder, &status, 0);
    CHECK(RunChild(b) == 0);
  }
  unlink(link.c_str());
  rmdir(a.c_str());
  rmdir(root.c_str());
  if (sFailures == 0) std::printf("%s: all passed\n", argv[0]);
  return sFailures == 0 ? 0 : 1;
}

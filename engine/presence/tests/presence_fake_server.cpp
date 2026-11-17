#include <ctime>
#include "fake_server.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: presence_fake_server <name> <log file> <seconds>\n");
    return 2;
  }
  FakeServer s;
  s.name = argv[1];
  s.Start();
  const std::string log = argv[2];
  const int secs = std::atoi(argv[3]);
  size_t written = 0;
  const auto t = Clock::now();
  while (std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t).count() < secs) {
    const auto f = s.Frames();
    if (f.size() > written) {
      FILE* fp = std::fopen(log.c_str(), "ab");
      if (fp != nullptr) {
        for (; written < f.size(); written++) {
          std::fprintf(fp, "%lld op=%u %s\n", static_cast<long long>(std::time(nullptr)), f[written].first,
                       f[written].second.c_str());
        }
        std::fclose(fp);
      }
    }
    if (FILE* st = std::fopen((log + ".stop").c_str(), "rb")) {
      std::fclose(st);
      break;
    }
    Sleep_ms(100);
  }
  s.Stop();
  return 0;
}



#include "presence.h"

#include "fake_server.h"
#include "presence.h"

using namespace engine::presence;

static int gFailed = 0;
#define CHECK(c)                                                                  \
  do {                                                                            \
    if (!(c)) {                                                                   \
      std::printf("FAILED line %d: %s\n", __LINE__, #c);                          \
      gFailed++;                                                                  \
    }                                                                             \
  } while (0)

static long long Since(Clock::time_point t) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}
template <class F>
static bool WaitFor(F f, int ms = 4000) {
  const auto t = Clock::now();
  while (Since(t) < ms) {
    if (f()) return true;
    Sleep_ms(10);
  }
  return f();
}

static Config FastConfig(const std::string& endpoint) {
  Config c;
  c.clientId = "1572149104006529045";
  c.endpointOverride = endpoint;
  c.minIntervalMs = 300;
  c.retryIntervalMs = 200;
  c.retryLongIntervalMs = 400;
  c.maxQuickRetries = 3;
  c.ioTimeoutMs = 400;
  c.stopWaitMs = 1500;
  return c;
}
static Activity Act(const std::string& d, const std::string& s = "state") {
  Activity a;
  a.details = d;
  a.state = s;
  a.largeImage = "logo";
  a.largeText = "Big \"text\"";
  a.startSeconds = 1700000000;
  return a;
}

static void TestBuilders() {
  CHECK(EscapeJson("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001");
  CHECK(BuildHandshake("42") == "{\"v\":1,\"client_id\":\"42\"}");
  const std::string j = BuildSetActivity(Act("Story mode", "Pokemon"), 77, 5);
  CHECK(j.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos);
  CHECK(j.find("\"pid\":77") != std::string::npos);
  CHECK(j.find("\"details\":\"Story mode\"") != std::string::npos);
  CHECK(j.find("\"timestamps\":{\"start\":1700000000}") != std::string::npos);
  CHECK(j.find("\"large_image\":\"logo\"") != std::string::npos);
  CHECK(j.find("\"large_text\":\"Big \\\"text\\\"\"") != std::string::npos);
  CHECK(j.find("small_image") == std::string::npos);
  const std::string c = BuildClearActivity(77, 6);
  CHECK(c.find("activity") == std::string::npos);
  CHECK(Clean(std::string("a\nb")) == "a b");
  CHECK(Clean(std::string(300, 'x')).size() == 120);
  std::string u;
  for (int i = 0; i < 100; i++) u += "\xC3\xA9";
  const std::string cu = Clean(u, 121);
  CHECK(cu.size() == 120);
}

static void TestBasic() {
  FakeServer s;
  s.Start();
  Client c;
  c.Start(FastConfig(s.name));
  c.Set(Act("Story mode"));
  CHECK(WaitFor([&] { return s.Count("Story mode") >= 1; }));
  const auto f = s.Frames();
  CHECK(!f.empty() && f[0].first == 0 && f[0].second.find("\"client_id\":\"1572149104006529045\"") != std::string::npos);
  CHECK(c.Connected());

  c.Set(Act("Story mode"));
  Sleep_ms(700);
  CHECK(s.Count("Story mode") == 1);
  c.Set(Act("In a battle"));
  CHECK(WaitFor([&] { return s.Count("In a battle") >= 1; }));
  const int before = static_cast<int>(s.Frames().size());
  const auto t = Clock::now();
  c.Stop();
  CHECK(Since(t) < 1500);

  const auto after = s.Frames();
  CHECK(static_cast<int>(after.size()) == before + 1);
  CHECK(after.back().second.find("SET_ACTIVITY") != std::string::npos && after.back().second.find("\"activity\"") == std::string::npos);
  s.Stop();
}

static void TestRateLimit() {
  FakeServer s;
  s.Start();
  Client c;
  Config cfg = FastConfig(s.name);
  cfg.minIntervalMs = 800;
  c.Start(cfg);
  c.Set(Act("one"));
  CHECK(WaitFor([&] { return s.Count("\"one\"") >= 1; }));
  const auto t = Clock::now();
  c.Set(Act("two"));
  c.Set(Act("three"));
  CHECK(WaitFor([&] { return s.Count("three") >= 1; }, 3000));
  CHECK(Since(t) >= 600);
  CHECK(s.Count("two") == 0);
  c.Clear();
  CHECK(WaitFor([&] { return s.Frames().back().second.find("\"activity\"") == std::string::npos; }, 3000));
  c.Stop();
  s.Stop();
}

static void TestReconnect() {
  FakeServer s(Mode::DropAfterFirstSet);
  s.Start();
  Client c;
  c.Start(FastConfig(s.name));
  c.Set(Act("again"));
  CHECK(WaitFor([&] { return s.connections >= 2 && s.Count("again") >= 2; }, 6000));
  s.mode = Mode::Normal;
  c.Stop();
  s.Stop();
}

static void TestLateStart() {
  FakeServer s;
  Client c;
  c.Start(FastConfig(s.name));
  c.Set(Act("early"));
  Sleep_ms(500);
  CHECK(!c.Connected());
  s.Start();
  CHECK(WaitFor([&] { return s.Count("early") >= 1; }, 6000));
  c.Stop();
  s.Stop();
}

static void TestNoDiscord() {
  FakeServer s;
  Client c;
  const auto t = Clock::now();
  c.Start(FastConfig(s.name));
  c.Set(Act("nobody"));
  c.Clear();
  Sleep_ms(1500);
  CHECK(!c.Connected());
  c.Stop();
  CHECK(Since(t) < 3500);
  Client never;
  never.Set(Act("x"));
  never.Clear();
  never.Stop();
  CHECK(!never.Running());
}

static void TestStalled() {
  FakeServer s(Mode::Stall);
  s.Start();
  Client c;
  Config cfg = FastConfig(s.name);
  c.Start(cfg);
  const auto t = Clock::now();
  c.Set(Act("stuck"));
  CHECK(Since(t) < 100);
  Sleep_ms(1200);
  CHECK(!c.Connected());
  const auto t2 = Clock::now();
  c.Stop();
  CHECK(Since(t2) < cfg.stopWaitMs + 500);
  s.Stop();
}

int main() {
  TestBuilders();
  TestBasic();
  TestRateLimit();
  TestReconnect();
  TestLateStart();
  TestNoDiscord();
  TestStalled();
  if (gFailed == 0) std::printf("presence_test: all OK\n");
  return gFailed == 0 ? 0 : 1;
}

#ifndef ENGINE_PRESENCE_H
#define ENGINE_PRESENCE_H

#include <cstdint>
#include <memory>
#include <string>

namespace engine::presence {

struct Activity {
  std::string details;
  std::string state;
  std::string largeImage;
  std::string largeText;
  std::string smallImage;
  std::string smallText;
  int64_t startSeconds = 0;
  bool operator!=(const Activity& o) const { return !(*this == o); }
  bool operator==(const Activity& o) const {
    return details == o.details && state == o.state && largeImage == o.largeImage && largeText == o.largeText &&
           smallImage == o.smallImage && smallText == o.smallText && startSeconds == o.startSeconds;
  }
};

struct Config {
  std::string clientId;
  std::string endpointOverride;
  int minIntervalMs = 15000;
  int retryIntervalMs = 30000;
  int retryLongIntervalMs = 300000;
  int maxQuickRetries = 10;
  int ioTimeoutMs = 2000;
  int stopWaitMs = 3000;
  bool log = false;
};

class Client {
 public:
  Client();
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void Start(const Config& config);
  void Set(const Activity& activity);
  void Clear();
  void Stop();
  bool Running() const;
  bool Connected() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

std::string EscapeJson(const std::string& s);
std::string BuildHandshake(const std::string& clientId);
std::string BuildSetActivity(const Activity& a, uint32_t pid, uint32_t nonce);
std::string BuildClearActivity(uint32_t pid, uint32_t nonce);
std::string Clean(const std::string& s, size_t maxBytes = 120);

}

#endif

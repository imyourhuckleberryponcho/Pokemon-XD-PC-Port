#ifndef XD_SAVE_EXT_H
#define XD_SAVE_EXT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xd::saveext {

struct Handler {
  const char* name = "";
  uint16_t version = 1;
  bool required = false;
  std::vector<uint8_t> (*save)() = nullptr;
  bool (*load)(const uint8_t* data, size_t size, uint16_t version) = nullptr;
  void (*reset)() = nullptr;
};

void Register(const Handler& handler);

const std::string& LastState();
std::vector<std::string> RequiredNow();

}

extern "C" {
int PortXD_SaveExtBeforeWrite(const uint8_t* image, size_t size);
void PortXD_SaveExtAfterWrite(int ok);
int PortXD_SaveExtOnRead(const uint8_t* image, size_t size, size_t length, char* text, size_t textSize);
}

#endif

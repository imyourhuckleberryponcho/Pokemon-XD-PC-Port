#ifndef XD_SAVE_NAMES_H
#define XD_SAVE_NAMES_H

#include <cstdint>
#include <memory>
#include <string>

namespace xd::save {

class GameNames {
public:
  GameNames();
  ~GameNames();
  bool Load(const std::string& discPath, std::string& error);
  bool Loaded() const;
  std::string Species(uint16_t id) const;
  std::string Item(uint16_t id) const;
  std::string Move(uint16_t id) const;
  std::string Nature(uint8_t id) const;
  std::string Message(uint32_t key) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif

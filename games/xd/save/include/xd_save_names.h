#ifndef XD_SAVE_NAMES_H
#define XD_SAVE_NAMES_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xd::save {

class GameNames {
public:
  GameNames();
  ~GameNames();
  bool Load(const std::string& discPath, std::string& error);
  bool LoadFromRunningGame(const void* tables, std::string& error);
  bool LoadFromRunningGame(std::string& error);
  bool Loaded() const;
  void SetLiveText(std::function<std::string(uint32_t)> fn);
  std::string Species(uint16_t id) const;
  std::string Item(uint16_t id) const;
  std::string Move(uint16_t id) const;
  std::string Nature(uint8_t id) const;
  std::string Ability(uint16_t id) const;
  std::string AbilityText(uint16_t id) const;
  std::string Message(uint32_t key) const;

private:
  bool LoadImpl(const std::string& discPath, bool running, std::string& error);
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::function<std::string(uint32_t)> live_;
};

}

#endif

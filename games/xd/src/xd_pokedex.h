#ifndef XD_POKEDEX_H
#define XD_POKEDEX_H

#include <pokedex.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xd::pokedex {

bool Enabled();

const std::vector<engine::pokedex::Species>& All();
const engine::pokedex::Species* Find(uint16_t index);
void Invalidate();

engine::pokedex::PlayerState& Player();
engine::pokedex::Settings DexSettings();
bool RefreshPlayer();
std::vector<uint16_t> MetOrder();

std::string CaughtText();
std::string ShinyText();
void SetLists(const std::string& caught, const std::string& shiny, bool kept);
bool ListsKept();
unsigned ListsChanged();

const engine::pokedex::Where& WhereOf(uint16_t index);
void AddTrainerPlaces(std::map<uint16_t, engine::pokedex::Where>& where);

std::string Utf8(const uint16_t* chars);
bool EntriesAvailable();
bool Imperial();

struct LookSource {
  std::string id, name;
  bool onPc = false;
};
std::vector<LookSource> LookSources();
std::string LookPin(int national);
void SetLookPin(int national, const std::string& id);
std::map<int, std::string> LookPins();
void SetLookPins(const std::map<int, std::string>& pins);
bool LookPinsChanged();
void ClearLookPinsChanged();

std::string TypeNameOf(uint8_t id);

struct MoveInfo {
  bool ok = false;
  int power = 0, accuracy = 0;
  uint8_t type = 0;
  std::string typeName, text;
};
MoveInfo MoveInfoOf(uint16_t move);

}

#endif

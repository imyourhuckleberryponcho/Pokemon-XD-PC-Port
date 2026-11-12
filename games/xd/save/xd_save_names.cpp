#include <xd_save_names.h>

#include "common_rel.h"
#include "dol_tables.h"

#include <port_disc.h>

#include <game/pxdvs/app/item/itemData.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/pokemon/pokemonSeikaku.h>
#include <game/pxdvs/app/waza/wazaData.h>

#include <xd_save.h>

#include <cstring>
#include <vector>

namespace xd::save {

namespace {

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

size_t CtrlArgBytes(uint8_t ctrl) {
  switch (ctrl) {
  case 7: case 9: case 56: case 82: case 83: case 84: case 91: case 92:
    return 1;
  case 8:
    return 4;
  }
  return 0;
}

}

struct GameNames::Impl {
  XDCommonTables tables{};
  std::vector<const uint8_t*> banks;

  void* Global(const char* name) const {
    for (uint32_t i = 0; i < tables.numGlobals; i++) {
      if (tables.globals[i].name != nullptr && std::strcmp(tables.globals[i].name, name) == 0 &&
          tables.globals[i].converted) {
        return tables.globals[i].value;
      }
    }
    return nullptr;
  }
  uint32_t Count(const char* name) const {
    const void* v = Global(name);
    return v != nullptr ? *static_cast<const uint32_t*>(v) : 0;
  }

  const uint8_t* Find(uint32_t key) const {
    if (key == 0) {
      return nullptr;
    }
    const uint32_t group = key >> 20, sub = key & 0xFFFFF;
    for (const uint8_t* bank : banks) {
      if (be16(bank) != group) {
        continue;
      }
      uint32_t lo = 0, hi = be16(bank + 4);
      while (lo < hi) {
        const uint32_t mid = (lo + hi) >> 1;
        const uint32_t k = be32(bank + 0x10 + mid * 8);
        if (k == sub) {
          return bank + be32(bank + 0x10 + mid * 8 + 4);
        }
        if (k < sub) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
    }
    return nullptr;
  }
};

GameNames::GameNames() = default;
GameNames::~GameNames() = default;

bool GameNames::Load(const std::string& discPath, std::string& error) {
  impl_.reset();
  auto impl = std::make_unique<Impl>();
  char err[512] = {};
  if (!PortDisc_Open(discPath.c_str(), err, sizeof(err))) {
    error = err;
    return false;
  }
  size_t dolSize = 0;
  const uint8_t* dol = PortDisc_Dol(&dolSize);
  if (dol == nullptr || !PortXD_LoadDolTables(dol, dolSize, err, sizeof(err))) {
    error = std::string("main.dol: ") + err;
    return false;
  }
  if (!PortXD_LoadCommonRel(&impl->tables, err, sizeof(err))) {
    error = std::string("common_rel: ") + err;
    return false;
  }
  if (const uint8_t* t = PortXD_GetDolTable("common_tbl")) {
    impl->banks.push_back(t);
  }
  if (void** res = static_cast<void**>(impl->Global("table_res"))) {
    if (impl->Count("table_res_number") > 1 && res[1] != nullptr) {
      impl->banks.push_back(static_cast<const uint8_t*>(res[1]));
    }
  }
  if (impl->banks.empty()) {
    error = "no message banks found";
    return false;
  }
  impl_ = std::move(impl);
  return true;
}

bool GameNames::Loaded() const { return impl_ != nullptr; }

std::string GameNames::Message(uint32_t key) const {
  if (!impl_) {
    return "";
  }
  const uint8_t* t = impl_->Find(key);
  if (t == nullptr) {
    return "";
  }
  std::u16string s;
  for (int n = 0; n < 256; n++) {
    const uint16_t c = be16(t);
    t += 2;
    if (c == 0) {
      break;
    }
    if (c == 0xFFFF) {
      const uint8_t ctrl = *t++;
      t += CtrlArgBytes(ctrl);
      continue;
    }
    s.push_back(static_cast<char16_t>(c));
  }
  return ToUtf8(s);
}

std::string GameNames::Species(uint16_t id) const {
  if (!impl_ || impl_->tables.pokemon_data == nullptr || id >= impl_->tables.pokemon_data_number) {
    return "";
  }
  return Message(static_cast<uint32_t>((uintptr_t)impl_->tables.pokemon_data[id].name));
}

std::string GameNames::Item(uint16_t id) const {
  if (!impl_) {
    return "";
  }
  const auto* index = static_cast<const uint16_t*>(impl_->Global("item_data_index"));
  const auto* items = static_cast<const ItemDataBiosRef*>(impl_->Global("item_data_prime"));
  if (index == nullptr || items == nullptr || id >= impl_->Count("item_data_index_number")) {
    return "";
  }
  const uint16_t slot = index[id];
  if (slot >= impl_->Count("item_data_prime_number")) {
    return "";
  }
  return Message(items[slot].name);
}

std::string GameNames::Move(uint16_t id) const {
  if (!impl_) {
    return "";
  }
  const auto* moves = static_cast<const WazaData*>(impl_->Global("waza_data"));
  if (moves == nullptr || id >= impl_->Count("waza_data_number")) {
    return "";
  }
  return Message(moves[id].name);
}

std::string GameNames::Nature(uint8_t id) const {
  if (!impl_) {
    return "";
  }
  const auto* natures = static_cast<const PokemonSeikakuDataBios*>(impl_->Global("pokemon_seikaku_data"));
  if (natures == nullptr || id >= impl_->Count("pokemon_seikaku_data_number")) {
    return "";
  }
  return Message(natures[id].name);
}

}

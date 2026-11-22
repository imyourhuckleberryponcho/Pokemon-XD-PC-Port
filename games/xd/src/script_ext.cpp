#include "script_ext.h"

#include "i18n.h"
#include "xd_challenges.h"
#include "xd_ui.h"

#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/waza/wazaData.h>

#include <imgui.h>
#include <port_menu.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

extern "C" {
uint32_t PortVI_GetFlipCount(void);
uint16_t PortInput_DeliveredButtons(int port);
void GSthreadSwitch(void);
void PortInput_LatchHeld(void);
void msgctrlSetValue(int32_t id, uintptr_t value);
void PortVideo_Notice(const char* text, double seconds);
uint32_t GSflagGet(uint32_t id);
int32_t heroItemAddItemDataId(void* hero, uint32_t item, uint32_t num, int32_t slot);
uint32_t heroItemCheckHaveItemDataId(void* hero, uint32_t item);
void* itemDataBiosGetPtr(uint16_t item);
int32_t itemDataBiosGetName(void* data);
uint16_t* GSmsgGetGSchar(uint32_t key);
extern uint32_t* pokemon_data_number;
extern uint32_t* waza_data_number;
extern uint32_t* item_data_prime_number;
extern uint32_t* flag_data_number;
WazaData* wazaDataBiosGetPtr(uint16_t id);
PokemonData* pokemonDataBiosGetPtr(uint16_t id);
uint8_t fightIsFight(void);
int32_t PortXD_SaveForced(void);
int32_t pokecoloGetRomVersion(void);
uint32_t memcardTaskCall(int32_t a, int32_t b, int32_t c);
void* savedataGetStatus(void* save, int32_t kind);
uint32_t gamedatasaveGetStatus(void* save, int32_t kind);
uintptr_t heroGetStatus(void* hero, uint32_t selector, uint16_t index);
void* memcardMemAllocForSavedata(void);
void* memcardMemFreeForSavedata(void* p);
}

namespace xd::scriptext {

namespace {

using engine::i18n::Tr;
using engine::script::Value;

engine::mods::ModValues sValues;
std::map<std::string, std::string> sVersions;
std::map<std::string, std::string> sSaved;
StoryCaller sCaller = nullptr;
bool sInStory = false;
std::thread::id sStoryThread;
bool sArming = false;
bool InStory() { return sInStory && sStoryThread == std::this_thread::get_id(); }
int sNumbers[2] = {0, 0};
std::string sTexts[2];

const Value* Field(const Value& table, const char* name) {
  if (table.kind != Value::Kind::Table) return nullptr;
  for (size_t i = 0; i < table.fieldNames.size(); i++) {
    if (table.fieldNames[i] == name) return &table.fieldValues[i];
  }
  return nullptr;
}
bool Text(const Value& t, const char* name, std::string& out, size_t maxLen) {
  const Value* v = Field(t, name);
  if (v == nullptr || v->kind != Value::Kind::String) return false;
  out = v->text.substr(0, maxLen);
  return true;
}
std::string Utf8Of(const uint16_t* t) {
  std::string out;
  if (t == nullptr) return out;
  for (int n = 0; n < 60; n++) {
    const uint16_t c = t[n];
    if (c == 0 || c >= 0xFF00 || c < 0x20) break;
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) out += static_cast<char>(0xC0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3F));
    else out += static_cast<char>(0xE0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3F)), out += static_cast<char>(0x80 | (c & 0x3F));
  }
  return out;
}
std::string ItemName(uint16_t item) {
  void* d = itemDataBiosGetPtr(item);
  if (d == nullptr) return "item " + std::to_string(item);
  const std::string n = Utf8Of(GSmsgGetGSchar(static_cast<uint32_t>(itemDataBiosGetName(d))));
  return n.empty() ? "item " + std::to_string(item) : n;
}

std::string Fill(const std::string& mod, const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] != '{') {
      out += text[i];
      continue;
    }
    const size_t end = text.find('}', i);
    if (end == std::string::npos || end - i > 60) {
      out += text[i];
      continue;
    }
    const std::string key = text.substr(i + 1, end - i - 1);
    std::string rep;
    bool known = true;
    const size_t colon = key.find(':');
    const std::string head = colon == std::string::npos ? key : key.substr(0, colon), arg = colon == std::string::npos ? "" : key.substr(colon + 1);
    if (head == "value") rep = std::to_string(sValues.Get(mod, arg));
    else if (head == "currency") rep = std::to_string(sValues.Balance(mod, arg));
    else if (head == "currency_name") rep = sValues.Currency(mod, arg) != nullptr ? sValues.Currency(mod, arg)->name : arg;
    else if (key == "number1") rep = std::to_string(sNumbers[0]);
    else if (key == "number2") rep = std::to_string(sNumbers[1]);
    else if (key == "text1") rep = sTexts[0];
    else if (key == "text2") rep = sTexts[1];
    else known = false;
    if (known) out += rep, i = end;
    else out += text[i];
  }
  return out;
}

struct ShopItem {
  uint16_t item = 0;
  int64_t price = 0;
};
struct Page {
  enum class Kind { List, Pick, Shop } kind = Kind::List;
  std::string mod, title, text, callback, currency;
  std::vector<std::string> choices;
  std::vector<ShopItem> items;
  int pick = 0;
  bool cancel = true;
};
struct Runner {
  Page page;
  int cursor = 0, top = 0, purchases = 0;
  int msgPage = 0;
  uint16_t lastBought = 0;
  std::vector<bool> chosen;
  std::string note;
  bool done = false, cancelled = false;
  int rows() const {
    return page.kind == Page::Kind::Shop ? static_cast<int>(page.items.size()) + 1 : page.kind == Page::Kind::Pick ? static_cast<int>(page.choices.size()) + 1 : std::max<int>(1, static_cast<int>(page.choices.size()));
  }
  int picked() const { return static_cast<int>(std::count(chosen.begin(), chosen.end(), true)); }
};

constexpr uint16_t kUp = 0x08, kDown = 0x04, kA = 0x100, kB = 0x200, kStart = 0x1000;
std::vector<Page> sPending;
std::mutex sUiLock;
std::unique_ptr<Runner> sActive;

void Start(Runner& r, const Page& p) {
  r = Runner();
  r.page = p;
  r.chosen.assign(p.choices.size(), false);
  for (std::string& c : r.page.choices) c = Fill(p.mod, c);
  r.page.title = Fill(p.mod, p.title);
  r.page.text = Fill(p.mod, p.text);
}

void Step(Runner& r, uint16_t pressed, uint16_t repeat) {
  const int n = r.rows();
  if (repeat & kUp) r.cursor = (r.cursor + n - 1) % n;
  if (repeat & kDown) r.cursor = (r.cursor + 1) % n;
  const bool list = r.page.kind == Page::Kind::List;
  const int visible = list ? xd::ui::ChoiceRowsFit() : 7;
  if (list) {
    if (r.msgPage + 1 < xd::ui::NpcPages(r.page.title, r.page.text)) {
      if (pressed & kA) r.msgPage++;
      else if ((pressed & kB) && r.page.cancel) r.done = r.cancelled = true;
      return;
    }
  }
  if (r.cursor < r.top) r.top = r.cursor;
  if (r.cursor >= r.top + visible) r.top = r.cursor - visible + 1;
  if ((pressed & kB) && r.page.cancel) {
    r.done = r.cancelled = true;
    return;
  }
  if (!(pressed & (kA | kStart))) return;
  switch (r.page.kind) {
  case Page::Kind::List:
    if (r.page.choices.empty() || (pressed & kA)) r.done = true;
    break;
  case Page::Kind::Pick:
    if (pressed & kStart) {
      if (r.picked() == r.page.pick) r.done = true;
    } else if (r.cursor < static_cast<int>(r.page.choices.size())) {
      const bool was = r.chosen[static_cast<size_t>(r.cursor)];
      if (was) r.chosen[static_cast<size_t>(r.cursor)] = false;
      else if (r.picked() < r.page.pick) r.chosen[static_cast<size_t>(r.cursor)] = true;
    } else if (r.picked() == r.page.pick) {
      r.done = true;
    }
    break;
  case Page::Kind::Shop:
    if (r.cursor >= static_cast<int>(r.page.items.size())) {
      if (pressed & kA) r.done = true;
      break;
    }
    if (pressed & kA) {
      const ShopItem& it = r.page.items[static_cast<size_t>(r.cursor)];
      std::string why;
      if (sValues.Balance(r.page.mod, r.page.currency) < it.price) {
        const engine::mods::CurrencyInfo* c = sValues.Currency(r.page.mod, r.page.currency);
        char line[96];
        std::snprintf(line, sizeof line, Tr("Not enough %s."), (c != nullptr ? c->name : r.page.currency).c_str());
        r.note = line;
      } else {
        const int32_t left = heroItemAddItemDataId(nullptr, it.item, 1, -1);
        if (left != 0) {
          r.note = Tr("The bag is full.");
          std::fprintf(stderr, "[script %s] shop: item %u not sold: the bag has no room (nothing was charged)\n", r.page.mod.c_str(), it.item);
        } else if (!sValues.Spend(r.page.mod, r.page.currency, it.price, why)) {
          r.note = why;
        } else {
          r.purchases++, r.lastBought = it.item;
          r.note = Tr("Thank you!");
          std::fprintf(stderr, "[script %s] shop: bought item %u for %lld, %lld left\n", r.page.mod.c_str(), it.item, static_cast<long long>(it.price),
                       static_cast<long long>(sValues.Balance(r.page.mod, r.page.currency)));
        }
      }
    }
    break;
  }
}

void DrawRunner(const Runner& r) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (r.page.kind == Page::Kind::List) {
    const bool last = xd::ui::NpcMessage(dl, r.page.title, r.page.text, r.msgPage);
    if (last && !r.page.choices.empty()) xd::ui::ChoiceBox(dl, r.page.choices, r.cursor, r.top);
    return;
  }
  xd::ui::Dim(dl, 150);
  const float x0 = 90.f, x1 = 550.f, y0 = 60.f;
  const int n = r.rows(), visible = 7;
  const int textLines = r.page.text.empty() ? 0 : std::min(3, std::max(1, static_cast<int>(std::ceil(xd::ui::Width(r.page.text, 0.9f) / (x1 - x0 - 40.f)))));
  const float y1 = y0 + 34.f + (textLines > 0 ? 22.f * static_cast<float>(textLines) + 6.f : 0.f) + (r.page.kind == Page::Kind::Shop ? 26.f : 0.f) + 26.f * static_cast<float>(std::min(n, visible)) + 70.f;
  xd::ui::TitledWindow(dl, x0, y0, x1, y1, r.page.title.empty() ? std::string(" ") : r.page.title, true);
  float y = y0 + 34.f;
  if (!r.page.text.empty()) y += 22.f * static_cast<float>(xd::ui::Para(dl, x0 + 20.f, y, x1 - x0 - 40.f, r.page.text, xd::ui::kSoft, 0.9f, 22.f, 3)) + 6.f;
  if (r.page.kind == Page::Kind::Shop) {
    const engine::mods::CurrencyInfo* c = sValues.Currency(r.page.mod, r.page.currency);
    xd::ui::Text(dl, x0 + 20.f, y, (c != nullptr ? c->name : r.page.currency) + ": " + std::to_string(sValues.Balance(r.page.mod, r.page.currency)), xd::ui::kGold, 0.9f);
    y += 26.f;
  }
  for (int i = r.top; i < std::min(n, r.top + visible); i++) {
    const float ry = y + static_cast<float>(i - r.top) * 26.f;
    const bool sel = i == r.cursor;
    if (sel) xd::ui::RowSel(dl, x0 + 12.f, x1 - 12.f, ry, 24.f);
    std::string label, price;
    const uint8_t* col = sel ? xd::ui::kText : xd::ui::kSoft;
    if (r.page.kind == Page::Kind::Shop) {
      if (i < static_cast<int>(r.page.items.size())) {
        label = ItemName(r.page.items[static_cast<size_t>(i)].item);
        price = std::to_string(r.page.items[static_cast<size_t>(i)].price);
      } else {
        label = Tr("Leave");
      }
    } else if (r.page.kind == Page::Kind::Pick) {
      if (i < static_cast<int>(r.page.choices.size())) label = std::string(r.chosen[static_cast<size_t>(i)] ? "[x] " : "[ ] ") + r.page.choices[static_cast<size_t>(i)];
      else label = std::string(Tr("Done")) + " (" + std::to_string(r.picked()) + "/" + std::to_string(r.page.pick) + ")";
    } else {
      label = r.page.choices.empty() ? std::string(Tr("OK")) : r.page.choices[static_cast<size_t>(i)];
    }
    xd::ui::Text(dl, x0 + 34.f, ry + 2.f, label, col, 0.9f, price.empty() ? x1 - x0 - 70.f : x1 - x0 - 150.f);
    if (!price.empty()) xd::ui::Text(dl, x1 - 110.f, ry + 2.f, price, xd::ui::kGold, 0.9f);
  }
  if (!r.note.empty()) xd::ui::Text(dl, x0 + 20.f, y1 - 38.f, r.note, xd::ui::kGood, 0.9f);
  xd::ui::HintBar(dl, r.page.kind == Page::Kind::Pick ? Tr("A: choose   START: done   B: back") : r.page.cancel ? Tr("A: choose   B: back") : Tr("A: choose"));
}

void Deliver(const Runner& r) {
  if (r.page.callback.empty() || sCaller == nullptr) return;
  std::vector<Value> args;
  if (r.page.kind == Page::Kind::List) {
    args.push_back(Value::Int(r.cancelled ? 0 : r.cursor + 1));
  } else if (r.page.kind == Page::Kind::Pick) {
    Value list = Value::List();
    if (!r.cancelled) {
      for (size_t i = 0; i < r.chosen.size(); i++) {
        if (r.chosen[i]) list.Add(Value::Int(static_cast<int64_t>(i) + 1));
      }
    }
    args.push_back(std::move(list));
  } else {
    args.push_back(Value::Int(r.purchases));
    args.push_back(Value::Int(r.lastBought));
  }
  if (!sCaller(r.page.mod, r.page.callback, args)) {
    std::fprintf(stderr, "[script %s] page: the callback \"%s\" is not a story_function of this mod (or it failed)\n", r.page.mod.c_str(), r.page.callback.c_str());
  }
}

std::atomic<bool> sShowing{false};
void ShowBlocking(const Page& page) {
  if (sShowing.exchange(true)) {
    std::fprintf(stderr, "[script %s] page \"%s\" refused: another page is up\n", page.mod.c_str(), page.title.c_str());
    return;
  }
  struct Free { ~Free() { sShowing = false; } } free_;
  Runner local;
  Start(local, page);
  {
    std::lock_guard<std::mutex> lock(sUiLock);
    sActive = std::make_unique<Runner>(local);
  }
  uint32_t last = PortVI_GetFlipCount();
  uint16_t prev = PortInput_DeliveredButtons(0);
  int heldUp = 0, heldDown = 0;
  std::fprintf(stderr, "[script %s] page \"%s\" up (%zu choices)\n", page.mod.c_str(), local.page.title.c_str(), local.page.choices.size());
  while (!local.done) {
    GSthreadSwitch();
    const uint32_t flip = PortVI_GetFlipCount();
    if (flip == last) continue;
    last = flip;
    const uint16_t held = PortInput_DeliveredButtons(0);
    static const bool trace = std::getenv("XD_MODPAGE_LOG") != nullptr;
    if (trace && (held != prev || flip % 60 == 0)) std::fprintf(stderr, "[script %s] page trace: game frame %u held 0x%04x\n", page.mod.c_str(), flip, static_cast<unsigned>(held));
    const uint16_t pressed = static_cast<uint16_t>(held & ~prev);
    uint16_t repeat = pressed & (kUp | kDown);
    heldUp = (held & kUp) ? heldUp + 1 : 0;
    heldDown = (held & kDown) ? heldDown + 1 : 0;
    if (heldUp > 15 && (heldUp - 15) % 5 == 0) repeat |= kUp;
    if (heldDown > 15 && (heldDown - 15) % 5 == 0) repeat |= kDown;
    prev = held;
    if (pressed != 0) std::fprintf(stderr, "[script %s] page: buttons 0x%04x, cursor %d\n", page.mod.c_str(), static_cast<unsigned>(pressed), local.cursor);
    Step(local, pressed, repeat);
    std::lock_guard<std::mutex> lock(sUiLock);
    sActive = std::make_unique<Runner>(local);
  }
  {
    std::lock_guard<std::mutex> lock(sUiLock);
    sActive.reset();
  }
  for (int tries = 0; tries < 300 && (PortInput_DeliveredButtons(0) & (kA | kB | kStart | kUp | kDown)) != 0; tries++) {
    const uint32_t before = PortVI_GetFlipCount();
    while (PortVI_GetFlipCount() == before) GSthreadSwitch();
  }
  std::fprintf(stderr, "[script %s] page \"%s\" closed: %s\n", page.mod.c_str(), local.page.title.c_str(), local.cancelled ? "cancelled" : "answered");
  Deliver(local);
}

struct MenuEntry {
  std::string mod, id, label, run;
};
std::vector<MenuEntry> sEntries;

struct MoveChange {
  uint16_t id = 0;
  int power = -1, accuracy = -1, priority = -999, type = -1;
};
struct SpeciesChange {
  uint16_t id = 0;
  int types[2] = {-1, -1}, abilities[2] = {-1, -1};
};
struct BattleArm {
  bool armed = false;
  std::string mod;
  bool hasBag = false, bag = true, hasGen4 = false, gen4 = false;
  std::vector<MoveChange> moves;
  std::vector<SpeciesChange> species;
};
BattleArm sArm;
bool sApplied = false, sAppliedRules = false;
std::vector<std::string> sSavedRules;
struct MoveBackup { uint16_t id; int16_t power; uint8_t accuracy, type; uint8_t priority; };
struct SpeciesBackup { uint16_t id; uint8_t types[2], abilities[2]; };
std::vector<MoveBackup> sMoveBackup;
std::vector<SpeciesBackup> sSpeciesBackup;

std::string One(const std::vector<Value>& a, size_t n, const char* usage) { return a.size() == n ? std::string() : std::string(usage); }

Value ApiValueGet(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "value_get(\"name\")"; return Value(); }
  return Value::Int(sValues.Get(mod, args[0].text));
}
Value ApiValueSet(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::Int) { error = "value_set(\"name\", whole number)"; return Value(); }
  std::string why;
  if (!sValues.Set(mod, args[0].text, args[1].integer, why)) {
    std::fprintf(stderr, "[script %s] value_set(\"%s\") refused: %s\n", mod.c_str(), args[0].text.c_str(), why.c_str());
    return Value::Bool(false);
  }
  return Value::Bool(true);
}
Value ApiValueAdd(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::Int) { error = "value_add(\"name\", whole number)"; return Value(); }
  std::string why;
  int64_t result = 0;
  if (!sValues.Add(mod, args[0].text, args[1].integer, result, why)) {
    std::fprintf(stderr, "[script %s] value_add(\"%s\") refused: %s\n", mod.c_str(), args[0].text.c_str(), why.c_str());
    return Value();
  }
  return Value::Int(result);
}
Value ApiSavedVersion(const std::string& mod, const std::vector<Value>&, std::string&) {
  const auto it = sSaved.find(mod);
  return it == sSaved.end() || it->second.empty() ? Value() : Value::Text(it->second);
}
Value ApiDefineCurrency(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Table) { error = "define_currency{id = \"bp\", name = \"Battle Points\", max = 9999}"; return Value(); }
  std::string id, name, why;
  const Value* max = Field(args[0], "max");
  if (!Text(args[0], "id", id, 40) || !Text(args[0], "name", name, 40) || (max != nullptr && max->kind != Value::Kind::Int)) { error = "define_currency{id = \"bp\", name = \"Battle Points\", max = 9999}"; return Value(); }
  if (!sValues.Define(mod, id, name, max != nullptr ? max->integer : 999999, why)) {
    std::fprintf(stderr, "[script %s] define_currency(\"%s\") refused: %s\n", mod.c_str(), id.c_str(), why.c_str());
    return Value::Bool(false);
  }
  return Value::Bool(true);
}
Value ApiCurrencyGet(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "currency_get(\"id\")"; return Value(); }
  return Value::Int(sValues.Balance(mod, args[0].text));
}
Value ApiCurrencyAdd(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::Int) { error = "currency_add(\"id\", amount)"; return Value(); }
  std::string why;
  int64_t result = 0;
  if (!sValues.Earn(mod, args[0].text, args[1].integer, result, why)) {
    std::fprintf(stderr, "[script %s] currency_add(\"%s\") refused: %s\n", mod.c_str(), args[0].text.c_str(), why.c_str());
    return Value();
  }
  return Value::Int(result);
}
Value ApiCurrencySpend(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::String || args[1].kind != Value::Kind::Int) { error = "currency_spend(\"id\", amount)"; return Value(); }
  std::string why;
  if (!sValues.Spend(mod, args[0].text, args[1].integer, why)) {
    std::fprintf(stderr, "[script %s] currency_spend(\"%s\", %lld): %s\n", mod.c_str(), args[0].text.c_str(), static_cast<long long>(args[1].integer), why.c_str());
    return Value::Bool(false);
  }
  return Value::Bool(true);
}

Value ApiMessageNumber(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::Int) { error = "message_number(1 or 2, number)"; return Value(); }
  if (args[0].integer < 1 || args[0].integer > 2 || args[1].integer < 0 || args[1].integer > 999999999) {
    std::fprintf(stderr, "[script %s] message_number refused: the slot is 1 or 2, the number 0 to 999999999\n", mod.c_str());
    return Value::Bool(false);
  }
  sNumbers[args[0].integer - 1] = static_cast<int>(args[1].integer);
  msgctrlSetValue(args[0].integer == 1 ? 0x2F : 0x30, static_cast<uintptr_t>(args[1].integer));
  return Value::Bool(true);
}
Value ApiMessageText(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  static uint16_t buffers[2][48];
  if (args.size() != 2 || args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::String) { error = "message_text(1 or 2, \"text\")"; return Value(); }
  if (args[0].integer < 1 || args[0].integer > 2) {
    std::fprintf(stderr, "[script %s] message_text refused: the slot is 1 or 2\n", mod.c_str());
    return Value::Bool(false);
  }
  uint16_t* b = buffers[args[0].integer - 1];
  size_t n = 0;
  const std::string& s = args[1].text;
  for (size_t i = 0; i < s.size() && n < 46;) {
    const uint8_t c = static_cast<uint8_t>(s[i]);
    uint32_t cp = c;
    size_t len = 1;
    if (c >= 0xE0 && i + 2 < s.size()) cp = (c & 0x0Fu) << 12 | (static_cast<uint8_t>(s[i + 1]) & 0x3Fu) << 6 | (static_cast<uint8_t>(s[i + 2]) & 0x3Fu), len = 3;
    else if (c >= 0xC0 && i + 1 < s.size()) cp = (c & 0x1Fu) << 6 | (static_cast<uint8_t>(s[i + 1]) & 0x3Fu), len = 2;
    b[n++] = static_cast<uint16_t>(cp >= 0x20 && cp < 0xFF00 ? cp : '?');
    i += len;
  }
  b[n] = 0;
  sTexts[args[0].integer - 1] = s.substr(0, 46);
  msgctrlSetValue(args[0].integer == 1 ? 0x4D : 0x57, reinterpret_cast<uintptr_t>(b));
  return Value::Bool(true);
}
Value ApiMessageSpecies(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "message_species(species)"; return Value(); }
  if (pokemon_data_number == nullptr || args[0].integer < 1 || args[0].integer >= static_cast<int64_t>(*pokemon_data_number)) {
    std::fprintf(stderr, "[script %s] message_species refused: no such species\n", mod.c_str());
    return Value::Bool(false);
  }
  msgctrlSetValue(0x4E, static_cast<uintptr_t>(args[0].integer));
  return Value::Bool(true);
}
Value ApiMessageItem(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 2 || args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::Int) { error = "message_item(1 or 2, item)"; return Value(); }
  if (args[0].integer < 1 || args[0].integer > 2 || item_data_prime_number == nullptr || args[1].integer < 1 || args[1].integer >= static_cast<int64_t>(*item_data_prime_number)) {
    std::fprintf(stderr, "[script %s] message_item refused: the slot is 1 or 2 and the item must exist\n", mod.c_str());
    return Value::Bool(false);
  }
  msgctrlSetValue(args[0].integer == 1 ? 0x2D : 0x2E, static_cast<uintptr_t>(args[1].integer));
  return Value::Bool(true);
}
Value ApiMessageMove(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::Int) { error = "message_move(move)"; return Value(); }
  if (waza_data_number == nullptr || args[0].integer < 1 || args[0].integer >= static_cast<int64_t>(*waza_data_number)) {
    std::fprintf(stderr, "[script %s] message_move refused: no such move\n", mod.c_str());
    return Value::Bool(false);
  }
  msgctrlSetValue(0x39, static_cast<uintptr_t>(args[0].integer));
  return Value::Bool(true);
}
Value ApiFill(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (args.size() != 1 || args[0].kind != Value::Kind::String) { error = "fill(\"text with {value:streak}\")"; return Value(); }
  return Value::Text(Fill(mod, args[0].text));
}

bool ReadPage(const std::string& mod, const Value& t, Page::Kind kind, Page& page, std::string& why) {
  if (t.kind != Value::Kind::Table) { why = "a table is needed: {title = ..., text = ..., choices = {...}, on_choice = \"story function\"}"; return false; }
  page = Page();
  page.kind = kind;
  page.mod = mod;
  Text(t, "title", page.title, 40);
  Text(t, "text", page.text, 300);
  Text(t, "on_choice", page.callback, 60);
  if (const Value* c = Field(t, "cancel"); c != nullptr && c->kind == Value::Kind::Bool) page.cancel = c->boolean;
  if (const Value* ch = Field(t, "choices"); ch != nullptr && ch->kind != Value::Kind::Nil) {
    if (ch->kind != Value::Kind::List || ch->fieldValues.size() > 64) { why = "choices is a list of up to 64 texts"; return false; }
    for (const Value& e : ch->fieldValues) {
      if (e.kind != Value::Kind::String) { why = "every choice is a text"; return false; }
      page.choices.push_back(e.text.substr(0, 60));
    }
  }
  if (kind == Page::Kind::Pick) {
    const Value* p = Field(t, "pick");
    if (p == nullptr || p->kind != Value::Kind::Int || p->integer < 1 || p->integer > static_cast<int64_t>(page.choices.size())) { why = "pick is how many to choose (1 up to the number of choices)"; return false; }
    page.pick = static_cast<int>(p->integer);
  }
  if (kind == Page::Kind::List && page.choices.empty() && page.text.empty() && page.title.empty()) { why = "a page with nothing on it"; return false; }
  return true;
}
Value Queue(const std::string& mod, Page::Kind kind, const std::vector<Value>& args, std::string& error, const char* usage) {
  if (args.size() != 1) { error = usage; return Value(); }
  Page page;
  std::string why;
  if (!ReadPage(mod, args[0], kind, page, why)) { error = std::string(usage) + ": " + why; return Value(); }
  if (sPending.size() >= 8) {
    std::fprintf(stderr, "[script %s] page refused: too many pages waiting\n", mod.c_str());
    return Value::Bool(false);
  }
  sPending.push_back(std::move(page));
  return Value::Bool(true);
}
Value ApiModPage(const std::string& mod, const std::vector<Value>& a, std::string& e) { return Queue(mod, Page::Kind::List, a, e, "mod_page{title, text, choices, on_choice}"); }
Value ApiModPick(const std::string& mod, const std::vector<Value>& a, std::string& e) { return Queue(mod, Page::Kind::Pick, a, e, "mod_pick{title, text, choices, pick, on_choice}"); }
Value ApiModShop(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  const char* usage = "mod_shop{currency = \"bp\", title = \"Prize counter\", items = {{item = ITEM.LEFTOVERS, price = 48}, ...}, on_choice = \"story function\"}";
  Page page;
  std::string why;
  if (args.size() != 1 || !ReadPage(mod, args[0], Page::Kind::Shop, page, why)) { error = std::string(usage) + (why.empty() ? "" : ": " + why); return Value(); }
  if (!Text(args[0], "currency", page.currency, 40) || sValues.Currency(mod, page.currency) == nullptr) {
    error = "mod_shop: the currency must be declared first with define_currency";
    return Value();
  }
  const Value* items = Field(args[0], "items");
  if (items == nullptr || items->kind != Value::Kind::List || items->fieldValues.empty() || items->fieldValues.size() > 40) { error = "mod_shop: items is a list of 1 to 40 {item, price}"; return Value(); }
  for (const Value& e : items->fieldValues) {
    const Value* it = Field(e, "item");
    const Value* pr = Field(e, "price");
    if (it == nullptr || pr == nullptr || it->kind != Value::Kind::Int || pr->kind != Value::Kind::Int || it->integer < 1 || pr->integer < 0 || pr->integer > 999999999) {
      error = "mod_shop: every item is {item = ITEM.X, price = whole number 0 or more}";
      return Value();
    }
    if (item_data_prime_number != nullptr && it->integer >= static_cast<int64_t>(*item_data_prime_number)) { error = "mod_shop: no such item " + std::to_string(it->integer); return Value(); }
    page.items.push_back({static_cast<uint16_t>(it->integer), pr->integer});
  }
  if (sPending.size() >= 8) return Value::Bool(false);
  sPending.push_back(std::move(page));
  return Value::Bool(true);
}

Value ApiAddMenuEntry(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  const char* usage = "add_menu_entry{id = \"tower\", label = \"BATTLE TOWER\", run = \"story function\"}";
  if (args.size() != 1 || args[0].kind != Value::Kind::Table) { error = usage; return Value(); }
  MenuEntry e;
  e.mod = mod;
  if (!Text(args[0], "id", e.id, 40) || !Text(args[0], "label", e.label, 30) || !Text(args[0], "run", e.run, 60) || e.label.empty()) { error = usage; return Value(); }
  if (const Value* where = Field(args[0], "where"); where != nullptr && !(where->kind == Value::Kind::String && where->text == "field")) {
    std::fprintf(stderr, "[script %s] add_menu_entry(\"%s\") refused: only where = \"field\" (the field menu) exists; the PC and PDA screens are the game's own\n", mod.c_str(), e.id.c_str());
    return Value::Bool(false);
  }
  for (const MenuEntry& o : sEntries) {
    if (o.mod == mod && o.id == e.id) {
      sEntries.erase(std::remove_if(sEntries.begin(), sEntries.end(), [&](const MenuEntry& m) { return m.mod == mod && m.id == e.id; }), sEntries.end());
      break;
    }
  }
  if (sEntries.size() >= 8) {
    std::fprintf(stderr, "[script %s] add_menu_entry refused: at most 8 entries in all\n", mod.c_str());
    return Value::Bool(false);
  }
  sEntries.push_back(e);
  std::fprintf(stderr, "[script %s] menu entry \"%s\" (%s) -> %s\n", mod.c_str(), e.label.c_str(), e.id.c_str(), e.run.c_str());
  return Value::Bool(true);
}

Value ApiSaveGame(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  if (!args.empty()) { error = "save_game()"; return Value(); }
  Value out = Value::Table();
  const auto say = [&](const char* status, const char* line) {
    std::fprintf(stderr, "[script %s] save_game: %s (%s)\n", mod.c_str(), status, line);
    out.Set("status", Value::Text(status));
    return out;
  };
  if (!InStory()) return say("not_allowed", "only from a story function: a hook cannot wait for the memory card");
  if (fightIsFight() != 0) return say("in_battle", "no saving in a battle");
  if (pokecoloGetRomVersion() == 1 || GSflagGet(0x3DE) == 0) return say("not_here", "the game does not allow saving at this point of the story");
  void* backup = memcardMemAllocForSavedata();
  std::memcpy(backup, savedataGetStatus(nullptr, 0), 0x27FD0);
  bool other = false;
  if (memcardTaskCall(3, 2, 0) == 3 && gamedatasaveGetStatus(nullptr, 4) != 0) {
    const uintptr_t mine = heroGetStatus(savedataGetStatus(backup, 2), 2, 0), card = heroGetStatus(nullptr, 2, 0);
    other = mine != card && gamedatasaveGetStatus(backup, 4) != 0;
  }
  std::memcpy(savedataGetStatus(nullptr, 0), backup, 0x27FD0);
  memcardMemFreeForSavedata(backup);
  if (other) return say("other_save", "the memory card holds another trainer's save: not written over");
  const int32_t r = PortXD_SaveForced();
  if (r != 4) {
    char line[80];
    std::snprintf(line, sizeof line, "the memory card job ended with %d", r);
    return say("card_error", line);
  }
  return say("saved", "written");
}

Value ApiBattleRules(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  const char* usage = "battle_rules{bag = false, gen4 = true}";
  if (args.size() != 1 || args[0].kind != Value::Kind::Table) { error = usage; return Value(); }
  if (fightIsFight() != 0 && !sArming) {
    std::fprintf(stderr, "[script %s] battle_rules refused: a battle is running (rules are for the next one)\n", mod.c_str());
    return Value::Bool(false);
  }
  for (const std::string& k : args[0].fieldNames) {
    if (k == "level") {
      std::fprintf(stderr, "[script %s] battle_rules refused: \"level\" (all Pokemon at one level) is not built; lend_team / set_trainer_team set their members' levels\n", mod.c_str());
      return Value::Bool(false);
    }
    if (k != "bag" && k != "gen4") { error = std::string(usage) + ": the game does not know \"" + k + "\" (bag, gen4)"; return Value(); }
  }
  BattleArm next = sArm;
  next.armed = true;
  next.mod = mod;
  if (const Value* b = Field(args[0], "bag")) { if (b->kind != Value::Kind::Bool) { error = "bag is true or false"; return Value(); } next.hasBag = true, next.bag = b->boolean; }
  if (const Value* g = Field(args[0], "gen4")) {
    if (g->kind != Value::Kind::Bool) { error = "gen4 is true or false"; return Value(); }
    bool has = false;
    for (const xd::challenges::Rule& r : xd::challenges::Rules()) has = has || std::strcmp(r.id, "gen4_rules") == 0;
    if (!has) {
      std::fprintf(stderr, "[script %s] battle_rules refused: the Gen 4 rules are not in this game\n", mod.c_str());
      return Value::Bool(false);
    }
    next.hasGen4 = true, next.gen4 = g->boolean;
  }
  sArm = next;
  std::fprintf(stderr, "[script %s] battle_rules: armed for the next battle%s%s\n", mod.c_str(), sArm.hasBag ? (sArm.bag ? " bag on" : " bag off") : "", sArm.hasGen4 ? (sArm.gen4 ? " gen4 on" : " gen4 off") : "");
  return Value::Bool(true);
}

Value ApiBattleTables(const std::string& mod, const std::vector<Value>& args, std::string& error) {
  const char* usage = "battle_tables{moves = {{move = MOVE.X, power = 90}}, species = {{species = SPECIES.X, types = {a, b}}}}";
  if (args.size() != 1 || args[0].kind != Value::Kind::Table) { error = usage; return Value(); }
  if (fightIsFight() != 0 && !sArming) {
    std::fprintf(stderr, "[script %s] battle_tables refused: a battle is running (the change is for the next one)\n", mod.c_str());
    return Value::Bool(false);
  }
  BattleArm next = sArm;
  next.moves.clear(), next.species.clear();
  for (size_t i = 0; i < args[0].fieldNames.size(); i++) {
    const std::string& group = args[0].fieldNames[i];
    const Value& rows = args[0].fieldValues[i];
    if (group != "moves" && group != "species") { error = std::string(usage) + ": the game does not know \"" + group + "\" (moves, species)"; return Value(); }
    if (rows.kind != Value::Kind::List) { error = std::string(usage) + ": " + group + " is a list of rows"; return Value(); }
    for (size_t j = 0; j < rows.fieldValues.size(); j++) {
      const bool moves = group == "moves";
      const Value& row = rows.fieldValues[j];
      const Value* idv = Field(row, moves ? "move" : "species");
      if (idv == nullptr || idv->kind != Value::Kind::Int || idv->integer < 1) { error = std::string("battle_tables: every row of ") + group + " names its " + (moves ? "move" : "species") + " (" + (moves ? "move = MOVE.X" : "species = SPECIES.X") + ")"; return Value(); }
      const int64_t id = idv->integer;
      if ((moves && (waza_data_number == nullptr || id >= static_cast<int64_t>(*waza_data_number))) || (!moves && (pokemon_data_number == nullptr || id >= static_cast<int64_t>(*pokemon_data_number)))) {
        error = "battle_tables: no such " + std::string(moves ? "move " : "species ") + std::to_string(id);
        return Value();
      }
      MoveChange m;
      SpeciesChange s;
      m.id = s.id = static_cast<uint16_t>(id);
      for (size_t k = 0; k < row.fieldNames.size(); k++) {
        const std::string& f = row.fieldNames[k];
        const Value& v = row.fieldValues[k];
        if (f == "move" || f == "species") continue;
        const auto whole = [&](int64_t lo, int64_t hi) { return v.kind == Value::Kind::Int && v.integer >= lo && v.integer <= hi; };
        if (moves && f == "power" && whole(0, 255)) m.power = static_cast<int>(v.integer);
        else if (moves && f == "accuracy" && whole(0, 255)) m.accuracy = static_cast<int>(v.integer);
        else if (moves && f == "priority" && whole(-6, 6)) m.priority = static_cast<int>(v.integer);
        else if (moves && f == "type" && whole(0, 255)) m.type = static_cast<int>(v.integer);
        else if (!moves && (f == "types" || f == "abilities") && v.kind == Value::Kind::List && v.fieldValues.size() == 2 && v.fieldValues[0].kind == Value::Kind::Int && v.fieldValues[1].kind == Value::Kind::Int &&
                 v.fieldValues[0].integer >= 0 && v.fieldValues[0].integer <= 255 && v.fieldValues[1].integer >= 0 && v.fieldValues[1].integer <= 255) {
          int* dest = f == "types" ? s.types : s.abilities;
          dest[0] = static_cast<int>(v.fieldValues[0].integer), dest[1] = static_cast<int>(v.fieldValues[1].integer);
        } else if (f == "hp" || f == "attack" || f == "defense" || f == "sp_attack" || f == "sp_defense" || f == "speed" || f == "pp") {
          std::fprintf(stderr, "[script %s] battle_tables refused: \"%s\" is kept in the save (a Pokemon's stats and PP are written from it); use a data file (change-data.md) for that\n", mod.c_str(), f.c_str());
          return Value::Bool(false);
        } else {
          error = "battle_tables: \"" + f + "\" is not a field the game can change for one battle (moves: power, accuracy, priority, type; species: types, abilities) or its value is out of range";
          return Value();
        }
      }
      (moves ? next.moves.push_back(m) : next.species.push_back(s));
    }
  }
  if (next.moves.size() + next.species.size() > 40) { error = "battle_tables: at most 40 rows"; return Value(); }
  next.armed = true;
  next.mod = mod;
  sArm = next;
  std::fprintf(stderr, "[script %s] battle_tables: armed for the next battle (%zu move(s), %zu species)\n", mod.c_str(), sArm.moves.size(), sArm.species.size());
  return Value::Bool(true);
}

}

namespace {

bool TakePending(Page& out) {
  if (sPending.empty()) return false;
  out = sPending.front();
  sPending.erase(sPending.begin());
  return true;
}

struct Hub {
  std::unique_ptr<Runner> runner;
};
Hub sHub;

void HubNext() {
  Page p;
  if (TakePending(p)) {
    sHub.runner = std::make_unique<Runner>();
    Start(*sHub.runner, p);
  }
}

std::vector<std::string> ActiveRules() {
  std::vector<std::string> ids;
  for (const xd::challenges::Rule& r : xd::challenges::Rules()) {
    if (xd::challenges::Active(r.id)) ids.push_back(r.id);
  }
  return ids;
}

void PutTablesBack() {
  for (const MoveBackup& b : sMoveBackup) {
    if (WazaData* w = wazaDataBiosGetPtr(b.id)) w->iryoku = b.power, w->avg = b.accuracy, w->zokuseiDataId = b.type, w->pri = b.priority;
  }
  for (const SpeciesBackup& b : sSpeciesBackup) {
    if (PokemonData* p = pokemonDataBiosGetPtr(b.id)) {
      p->zokuseiDataId[0] = b.types[0], p->zokuseiDataId[1] = b.types[1], p->tokuseiDataId[0] = b.abilities[0], p->tokuseiDataId[1] = b.abilities[1];
    }
  }
  if (!sMoveBackup.empty() || !sSpeciesBackup.empty()) std::fprintf(stderr, "[script] the game's tables are as the game has them again (%zu move(s), %zu species put back)\n", sMoveBackup.size(), sSpeciesBackup.size());
  sMoveBackup.clear(), sSpeciesBackup.clear();
}

}

engine::mods::ModValues& Values() { return sValues; }
void SetVersions(const std::map<std::string, std::string>& versionOf) { sVersions = versionOf; }
void LoadValues(const std::vector<engine::mods::ModValueSet>& sets, const char* why) {
  size_t dropped = 0;
  sValues.Load(sets, &dropped);
  sSaved.clear();
  size_t n = 0;
  for (const engine::mods::ModValueSet& s : sets) sSaved[s.mod] = s.version, n += s.values.size();
  std::fprintf(stderr, "[script] saved numbers read (%s): %zu number(s) of %zu mod(s)%s\n", why, n, sets.size(), dropped ? " (some did not follow the rules and were dropped)" : "");
}
void ClearValues(const char* why) {
  const bool had = !sValues.Empty();
  sValues.Load({}, nullptr);
  sSaved.clear();
  if (had) std::fprintf(stderr, "[script] saved numbers cleared (%s)\n", why);
}
std::vector<engine::mods::ModValueSet> ExportValues() { return sValues.Export(sVersions); }
bool ValuesChanged(const std::vector<engine::mods::ModValueSet>& kept) { return ExportValues() != kept; }

void Reset() {
  sPending.clear();
  sEntries.clear();
  sValues.ClearCurrencies();
  sArm = BattleArm();
  if (sApplied) BattleEnded();
  PutTablesBack();
  std::lock_guard<std::mutex> lock(sUiLock);
  sActive.reset();
}

Value BattleRulesTable(const std::string& mod, const std::vector<Value>& args, std::string& error) { return ApiBattleRules(mod, args, error); }

void RegisterApis(engine::script::Host& host) {
  host.SetApi("value_get", ApiValueGet);
  host.SetApi("value_set", ApiValueSet);
  host.SetApi("value_add", ApiValueAdd);
  host.SetApi("saved_version", ApiSavedVersion);
  host.SetApi("define_currency", ApiDefineCurrency);
  host.SetApi("currency_get", ApiCurrencyGet);
  host.SetApi("currency_add", ApiCurrencyAdd);
  host.SetApi("currency_spend", ApiCurrencySpend);
  host.SetApi("message_number", ApiMessageNumber);
  host.SetApi("message_text", ApiMessageText);
  host.SetApi("message_species", ApiMessageSpecies);
  host.SetApi("message_item", ApiMessageItem);
  host.SetApi("message_move", ApiMessageMove);
  host.SetApi("fill", ApiFill);
  host.SetApi("mod_page", ApiModPage);
  host.SetApi("mod_pick", ApiModPick);
  host.SetApi("mod_shop", ApiModShop);
  host.SetApi("add_menu_entry", ApiAddMenuEntry);
  host.SetApi("save_game", ApiSaveGame);
  host.SetApi("battle_tables", ApiBattleTables);
}

void SetStoryCaller(StoryCaller caller) { sCaller = caller; }
void SetInStoryCall(bool on) { sInStory = on, sStoryThread = std::this_thread::get_id(); }
void SetArming(bool on) { sArming = on; }

void RunPending() {
  struct Done { ~Done() { sInStory = false; } } done;
  for (int guard = 0; guard < 32; guard++) {
    Page p;
    if (!TakePending(p)) return;
    ShowBlocking(p);
  }
  sPending.clear();
  std::fprintf(stderr, "[script] pages: more than 32 in a row from one call: the rest is dropped\n");
}

void DrawPage() {
  std::unique_ptr<Runner> copy;
  {
    std::lock_guard<std::mutex> lock(sUiLock);
    if (!sActive) return;
    copy = std::make_unique<Runner>(*sActive);
  }
  DrawRunner(*copy);
}
bool PageUp() {
  std::lock_guard<std::mutex> lock(sUiLock);
  return sActive != nullptr;
}

bool HasMenuEntries() { return !sEntries.empty(); }
size_t MenuEntryCount() { return sEntries.size(); }
const char* MenuEntryLabel(size_t i) { return i < sEntries.size() ? sEntries[i].label.c_str() : ""; }
const char* MenuEntryMod(size_t i) { return i < sEntries.size() ? sEntries[i].mod.c_str() : ""; }
void MenuEntryRun(size_t i) {
  if (i >= sEntries.size()) return;
  const MenuEntry e = sEntries[i];
  std::fprintf(stderr, "[script %s] menu entry \"%s\" chosen: running %s\n", e.mod.c_str(), e.id.c_str(), e.run.c_str());
  sInStory = false;
  sPending.clear();
  if (sCaller == nullptr || !sCaller(e.mod, e.run, {})) std::fprintf(stderr, "[script %s] menu entry \"%s\": the story function \"%s\" is not there (or it failed)\n", e.mod.c_str(), e.id.c_str(), e.run.c_str());
  HubNext();
}
bool EntryPageUp() { return sHub.runner != nullptr; }
void EntryPageReset() { sHub.runner.reset(), sPending.clear(); }
void EntryPageFrame(const PortMenuInput* in) {
  if (!sHub.runner) return;
  Step(*sHub.runner, in->pressed, in->repeat);
  DrawRunner(*sHub.runner);
  if (sHub.runner->done) {
    const Runner finished = *sHub.runner;
    sHub.runner.reset();
    Deliver(finished);
    HubNext();
  }
}

void BattleStarted() {
  if (sApplied) BattleEnded();
  if (!sArm.armed) return;
  const BattleArm arm = sArm;
  sArm = BattleArm();
  for (const MoveChange& m : arm.moves) {
    WazaData* w = wazaDataBiosGetPtr(m.id);
    if (w == nullptr) continue;
    sMoveBackup.push_back({m.id, w->iryoku, w->avg, w->zokuseiDataId, w->pri});
    if (m.power >= 0) w->iryoku = static_cast<int16_t>(m.power);
    if (m.accuracy >= 0) w->avg = static_cast<uint8_t>(m.accuracy);
    if (m.type >= 0) w->zokuseiDataId = static_cast<uint8_t>(m.type);
    if (m.priority != -999) w->pri = static_cast<uint8_t>(static_cast<int8_t>(m.priority));
  }
  for (const SpeciesChange& s : arm.species) {
    PokemonData* p = pokemonDataBiosGetPtr(s.id);
    if (p == nullptr) continue;
    sSpeciesBackup.push_back({s.id, {p->zokuseiDataId[0], p->zokuseiDataId[1]}, {p->tokuseiDataId[0], p->tokuseiDataId[1]}});
    for (int i = 0; i < 2; i++) {
      if (s.types[i] >= 0) p->zokuseiDataId[i] = static_cast<uint8_t>(s.types[i]);
      if (s.abilities[i] >= 0) p->tokuseiDataId[i] = static_cast<uint8_t>(s.abilities[i]);
    }
  }
  if (!arm.moves.empty() || !arm.species.empty()) std::fprintf(stderr, "[script %s] battle_tables in force for this battle (%zu move(s), %zu species)\n", arm.mod.c_str(), arm.moves.size(), arm.species.size());
  if (arm.hasBag || arm.hasGen4) {
    sSavedRules = ActiveRules();
    std::vector<std::string> now = sSavedRules;
    const auto drop = [&](const char* id) { now.erase(std::remove(now.begin(), now.end(), std::string(id)), now.end()); };
    if (arm.hasBag) { drop("no_battle_items"); if (!arm.bag) now.push_back("no_battle_items"); }
    if (arm.hasGen4) { drop("gen4_rules"); if (arm.gen4) now.push_back("gen4_rules"); }
    xd::challenges::SetActive(now, "a script's rules for one battle");
    std::fprintf(stderr, "[script %s] battle_rules in force for this battle\n", arm.mod.c_str());
  }
  sApplied = true;
  sAppliedRules = arm.hasBag || arm.hasGen4;
}

void BattleEnded() {
  if (!sApplied) return;
  sApplied = false;
  PutTablesBack();
  if (sAppliedRules) {
    xd::challenges::SetActive(sSavedRules, "the script's rules for one battle are over: the player's own rules again");
    std::fprintf(stderr, "[script] battle_rules over: the player's own rules are back\n");
  }
  sAppliedRules = false;
}

}

extern "C" void PortXD_ScriptRunPending(void) {
  xd::scriptext::RunPending();
}

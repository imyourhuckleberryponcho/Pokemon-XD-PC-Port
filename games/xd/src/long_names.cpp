#include <cstdint>
#include <cstring>
#include <map>
#include <string>

extern "C" {
void* pokemonDataBiosGetPtr(uint16_t species);
uint32_t pokemonDataBiosGetName(void* data);
char16_t* GSmsgGetGSchar(uint32_t key);
void PortXD_RealMsgctrlSetValue(int32_t id, uintptr_t value);
uint32_t PortXD_RealMsgctrlFont(void* ctrl);
char16_t* fightOutPokemonGetNicknamePtr(void* battler);
}

namespace {

constexpr size_t kGameLetters = 10;
constexpr size_t kMaxLetters = 15;

std::map<std::u16string, std::u16string> sLong;
bool sBuilt = false;

void Build() {
  void* first = pokemonDataBiosGetPtr(1);
  const char16_t* probe = first != nullptr ? GSmsgGetGSchar(pokemonDataBiosGetName(first)) : nullptr;
  if (probe == nullptr) return;
  sBuilt = true;
  std::map<std::u16string, int> seen;
  for (uint16_t sp = 1; sp < 1200; sp++) {
    void* data = pokemonDataBiosGetPtr(sp);
    if (data == nullptr) continue;
    const char16_t* name = GSmsgGetGSchar(pokemonDataBiosGetName(data));
    if (name == nullptr) continue;
    size_t n = 0;
    while (n < 40 && name[n] != 0) n++;
    if (n <= kGameLetters || n > kMaxLetters) continue;
    const std::u16string whole(name, n);
    const std::u16string head = whole.substr(0, kGameLetters);
    auto it = sLong.find(head);
    if (it != sLong.end() && it->second != whole) {
      seen[head] = 2;
      continue;
    }
    sLong[head] = whole;
  }
  for (const auto& kv : seen) sLong.erase(kv.first);
}

char16_t sPlace[0x80][kMaxLetters + 8];

bool IsNameValue(int32_t id) {
  switch (id) {
    case 0xD: case 0xE: case 0x41:
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
    case 0x32: case 0x33: case 0x36: case 0x37: case 0x51:
      return true;
  }
  return false;
}

bool IsNarrowWindow(int32_t id) { return id == 0x36 || id == 0x37 || id == 0x51; }

constexpr char16_t Ctrl(uint8_t code, uint8_t arg) { return static_cast<char16_t>(code | (arg << 8)); }

struct MsgCtrlView {
  uint8_t unk0, activeFlag, shadow, talkSE;
  float f04, f08, f0c, f10, charW;
  int16_t width, height;
  uint32_t key;
  int16_t font;
  uint8_t pad22, unk23;
  uint32_t color;
  uint8_t* pad28;
  uint8_t* saved;
  uint8_t* stream;
  uint8_t* stack[3];
  int8_t depth, indentOff, lineSpace, baseLineBias;
  uint8_t flags, pending, waitFlag, waitFlag2;
  uint8_t pad48[2];
  int16_t wait;
  uint8_t align, ruby;
  uint8_t pad4e[2];
  float f50, f54;
  uint8_t* rubyPtr;
  uint8_t r5c[4];
  float f60;
  float scaleX;
  float f68;
};

const char16_t* Whole(const char16_t* s, char16_t* out, bool narrow) {
  if (!sBuilt) Build();
  if (sLong.empty() || s == nullptr) return s;
  size_t n = 0;
  while (n <= kGameLetters && s[n] != 0) n++;
  if (n != kGameLetters) return s;
  auto it = sLong.find(std::u16string(s, n));
  if (it == sLong.end()) return s;
  const std::u16string& whole = it->second;
  size_t at = 0;
  if (narrow) {
    out[at++] = 0xFFFF;
    out[at++] = Ctrl(7, static_cast<uint8_t>(0x80 + kGameLetters * 100 / whole.size()));
  }
  for (char16_t c : whole) out[at++] = c;
  if (narrow) {
    out[at++] = 0xFFFF;
    out[at++] = Ctrl(7, 0x80 + 100);
  }
  out[at] = 0;
  return out;
}

}

extern "C" void msgctrlSetValue(int32_t id, uintptr_t value) {
  if (value > 0x10000 && id >= 0 && id < 0x80 && IsNameValue(id))
    value = reinterpret_cast<uintptr_t>(Whole(reinterpret_cast<const char16_t*>(value), sPlace[id], IsNarrowWindow(id)));
  PortXD_RealMsgctrlSetValue(id, value);
}

extern "C" char16_t* PortXD_FopNickname(void* battler) {
  static char16_t ring[4][kMaxLetters + 8];
  static unsigned next = 0;
  char16_t* s = fightOutPokemonGetNicknamePtr(battler);
  return const_cast<char16_t*>(Whole(s, ring[next++ & 3], false));
}

extern "C" uintptr_t msgctrlFont(void* ctrl) {
  MsgCtrlView* o = static_cast<MsgCtrlView*>(ctrl);
  if (o->stream != nullptr && *o->stream >= 0x80) {
    o->scaleX = static_cast<float>(*o->stream - 0x80) / 100.0f;
    o->stream++;
    return 0;
  }
  return PortXD_RealMsgctrlFont(ctrl);
}

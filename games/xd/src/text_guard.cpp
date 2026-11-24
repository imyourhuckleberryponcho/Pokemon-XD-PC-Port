#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

typedef uintptr_t GCuptr;
struct MsgCtrl;
extern "C" {
GCuptr msgctrlItem(MsgCtrl*);
GCuptr msgctrlItem2(MsgCtrl*);
GCuptr msgctrlMsgID(MsgCtrl*);
GCuptr msgctrlWaza(MsgCtrl*);
GCuptr msgctrlPokemonID(MsgCtrl*);
GCuptr msgctrlMenuMsgID(MsgCtrl*);
GCuptr msgctrlMenuMsgID2(MsgCtrl*);
GCuptr msgctrlTribe(MsgCtrl*);
GCuptr msgctrlNpc(MsgCtrl*);
uint16_t* GSmsgGetGSchar(uint32_t key);
}

namespace {
constexpr uint32_t kUnknownText = 0x3AD2;

std::set<uint32_t> sReported;

bool ForcedMissing(const char* handler) {
  static const char* want = std::getenv("XD_TEXT_GUARD_TEST");
  return want != nullptr && (std::strcmp(want, handler) == 0 || std::strcmp(want, "all") == 0);
}

GCuptr Guard(GCuptr key, const char* handler) {
  if (key == 0 || key > 0xFFFFFFFFu) return key;
  const uint32_t id = static_cast<uint32_t>(key);
  if (GSmsgGetGSchar(id) != nullptr && !ForcedMissing(handler)) return key;
  if (sReported.insert(id).second) std::fprintf(stderr, "[text] missing text id %u (0x%X): shown as \"????\"\n", id, id);
  return GSmsgGetGSchar(kUnknownText) != nullptr ? static_cast<GCuptr>(kUnknownText) : 0;
}
}

extern "C" {
GCuptr PortXD_GuardItem(MsgCtrl* c) { return Guard(msgctrlItem(c), "item"); }
GCuptr PortXD_GuardItem2(MsgCtrl* c) { return Guard(msgctrlItem2(c), "item2"); }
GCuptr PortXD_GuardMsgID(MsgCtrl* c) { return Guard(msgctrlMsgID(c), "msgid"); }
GCuptr PortXD_GuardWaza(MsgCtrl* c) { return Guard(msgctrlWaza(c), "waza"); }
GCuptr PortXD_GuardPokemonID(MsgCtrl* c) { return Guard(msgctrlPokemonID(c), "pokemon"); }
GCuptr PortXD_GuardMenuMsgID(MsgCtrl* c) { return Guard(msgctrlMenuMsgID(c), "menumsgid"); }
GCuptr PortXD_GuardMenuMsgID2(MsgCtrl* c) { return Guard(msgctrlMenuMsgID2(c), "menumsgid2"); }
GCuptr PortXD_GuardTribe(MsgCtrl* c) { return Guard(msgctrlTribe(c), "tribe"); }
GCuptr PortXD_GuardNpc(MsgCtrl* c) { return Guard(msgctrlNpc(c), "npc"); }
}

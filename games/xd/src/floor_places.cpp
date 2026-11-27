#include <cstdint>
#include <cstdio>
#include <cstring>

struct AppScript;
struct GSthread;
class FloorStack {
public:
  void makeScriptModule();
  uint32_t m_groupID;
  uint32_t m_floorID;
  void* m_pushData;
  uint32_t m_pushSize;
  uint8_t m_pushFlag;
  uint8_t m_popFlag;
  AppScript* m_script;
  uint32_t m_unk18;
  GSthread* m_thread;
  uint32_t m_resources[64][2];
  uint32_t m_unk220;
};

namespace {
struct StackList { FloorStack* stacks; int32_t depth; uint32_t num; };
constexpr uint32_t kGamePlaces = 4, kPlaces = 6;
constexpr uint32_t kListGc = 12, kStacksGc = 548 * kGamePlaces + 16;
alignas(16) unsigned char sPlaces[sizeof(FloorStack) * kPlaces];
StackList* sList = nullptr;
bool sReady = false, sUsed = false;
uint32_t sListPc = 0, sStacksPc = 0;
void* sListPtr = nullptr;
void* sStacksPtr = nullptr;
void* sCandidates[8];
int sCandidateCount = 0;
uint32_t sBootHeap = 0;

void Noted(uint32_t pc, uint32_t gc, void* p) {
  if (gc == kListGc && pc == sizeof(StackList) && sCandidateCount < 8) sCandidates[sCandidateCount++] = p;
  if (gc == kStacksGc) sStacksPtr = p, sStacksPc = pc;
}
}

extern "C" {
void PortXD_FloorPlacesRange(unsigned char** begin, size_t* size) {
  *begin = sPlaces;
  *size = sizeof sPlaces;
}
extern void (*PortXD_NotedAlloc)(uint32_t pc, uint32_t gc, void* p);
void GSfloorInit(uint32_t places, uint32_t b, uint32_t c, uint32_t d);
uint8_t GSfloorPush(uint32_t id);
uint32_t GSmemGetCurrentHeap(void);
uint32_t GSmemSetCurrentHeap(uint32_t heap);

void PortXD_FloorInit(uint32_t places, uint32_t b, uint32_t c, uint32_t d) {
  const bool again = sUsed;
  sUsed = true;
  sReady = false;
  sListPtr = sStacksPtr = nullptr;
  sCandidateCount = 0;
  PortXD_NotedAlloc = &Noted;
  GSfloorInit(places, b, c, d);
  PortXD_NotedAlloc = nullptr;
  for (int i = 0; i < sCandidateCount; i++)
    if (sStacksPtr != nullptr && static_cast<StackList*>(sCandidates[i])->stacks == sStacksPtr) sListPtr = sCandidates[i];
  sList = static_cast<StackList*>(sListPtr);
  if (again || places != kGamePlaces || sList == nullptr || sStacksPtr == nullptr || sList->stacks != sStacksPtr || sList->num != kGamePlaces || sList->depth != 0 ||
      sStacksPc != sizeof(FloorStack) * kGamePlaces) {
    std::fprintf(stderr, "[floor places] the game's floor stack is not as expected (list %p, places %p of %u bytes): left as it is\n", sListPtr, sStacksPtr, sStacksPc);
    return;
  }
  std::memset(sPlaces, 0, sizeof sPlaces);
  std::memcpy(sPlaces, sList->stacks, sizeof(FloorStack) * kGamePlaces);
  sList->stacks = reinterpret_cast<FloorStack*>(sPlaces);
  sBootHeap = GSmemGetCurrentHeap();
  sReady = true;
}

uint8_t PortXD_FloorPushRoom(uint32_t id) {
  if (!sReady || sList == nullptr) return GSfloorPush(id);
  for (uint32_t i = kGamePlaces; i < kPlaces; i++) {
    if (sList->stacks[i].m_script != nullptr) continue;
    const uint32_t heap = GSmemSetCurrentHeap(sBootHeap);
    sList->stacks[i].makeScriptModule();
    GSmemSetCurrentHeap(heap);
    if (sList->stacks[i].m_script == nullptr) return GSfloorPush(id);
  }
  sList->num = kPlaces;
  const uint8_t ok = GSfloorPush(id);
  sList->num = kGamePlaces;
  return ok;
}
int32_t PortXD_FloorDepth(void) { return sList != nullptr ? sList->depth : -1; }
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
extern void* activeParticle[16];
uint32_t PortVI_GetFlipCount(void);
}

namespace {
bool sOn = false;
bool sReported = false;
uint32_t sLastFrame = 0;

void* Next(void* p) { return *static_cast<void**>(p); }
}

extern "C" void PortXD_ParticleCheckInit(void) {
  const char* v = std::getenv("XD_PARTICLE_CHECK");
  sOn = v != nullptr && v[0] == '1';
  if (sOn) {
    std::fprintf(stderr, "[particle check] on: looking for a loop in the active particle lists every frame\n");
  }
}

extern "C" void PortXD_ParticleCheckFrame(void) {
  if (!sOn || sReported) {
    return;
  }
  const uint32_t frame = PortVI_GetFlipCount();
  for (int link = 0; link < 16; link++) {
    void* slow = activeParticle[link];
    void* fast = activeParticle[link];
    bool ring = false;
    while (fast != nullptr && Next(fast) != nullptr) {
      slow = Next(slow);
      fast = Next(Next(fast));
      if (slow == fast) {
        ring = true;
        break;
      }
    }
    if (!ring) {
      continue;
    }
    int before = 0;
    slow = activeParticle[link];
    while (slow != fast) {
      slow = Next(slow);
      fast = Next(fast);
      before++;
    }
    void* first = slow;
    void* last = first;
    int length = 1;
    while (Next(last) != first) {
      last = Next(last);
      length++;
    }
    std::fprintf(stderr,
                 "[particle check] game frame %u (last clean frame %u): activeParticle[%d] is a ring: %d particle(s) "
                 "lead in, the ring has %d; it starts at %p; the particle closing it is %p (its next -> %p). "
                 "Rerun with XD_WATCH=%llx\n",
                 frame, sLastFrame, link, before, length, first, last, Next(last),
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(last)));
    void* p = activeParticle[link];
    for (int i = 0; i < before + length && i < 64; i++, p = Next(p)) {
      std::fprintf(stderr, "[particle check]   #%d %p kind %08x\n", i, p,
                   *reinterpret_cast<uint32_t*>(static_cast<char*>(p) + sizeof(void*)));
    }
    std::fflush(stderr);
    sReported = true;
    return;
  }
  sLastFrame = frame;
}

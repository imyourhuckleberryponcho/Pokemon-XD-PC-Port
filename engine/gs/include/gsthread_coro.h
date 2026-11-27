#ifndef GSTHREAD_CORO_H
#define GSTHREAD_CORO_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gscoro {

bool UseCoroutines(void* (*alloc)(size_t size, size_t align));
bool On();

struct Piece {
  uint8_t* address = nullptr;
  std::vector<uint8_t> bytes;
};

struct Snapshot {
  std::vector<uint8_t> core;
  std::vector<Piece> stacks;
  size_t tasks = 0;
  bool inTask = false;
};

int Take(Snapshot& snapshot);

[[noreturn]] void Restore(const Snapshot& snapshot);

size_t LiveTasks();

void RunOnMainStack(void (*fn)(void*), void* arg);
void StackBlock(uint8_t** begin, size_t* size);

void ToBytes(const Snapshot& snapshot, uintptr_t cookie, std::vector<uint8_t>& out);
bool FromBytes(const uint8_t* data, size_t size, uintptr_t currentCookie, Snapshot& snapshot, size_t* rewritten,
               uintptr_t* fileCookie, const char** why);

uintptr_t StackGuardGet();
void StackGuardSet(uintptr_t value);

void Create(void* thread, void* entry, void (*exitFunc)(uint32_t), uint32_t stackSize);
void SetArg(void* thread, int index, uintptr_t value);
void Run(void* thread);
void SwitchToScheduler(void (*hook)());
void Destroy(void* thread);

}

#endif

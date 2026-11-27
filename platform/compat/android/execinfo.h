#ifndef PORT_ANDROID_EXECINFO_H
#define PORT_ANDROID_EXECINFO_H
#include <unwind.h>

#ifdef __cplusplus
extern "C" {
#endif

struct port_bt_state {
  void** buffer;
  int count, size;
};
static inline _Unwind_Reason_Code port_bt_step(struct _Unwind_Context* context, void* arg) {
  struct port_bt_state* s = (struct port_bt_state*)arg;
  const uintptr_t pc = _Unwind_GetIP(context);
  if (pc == 0) return _URC_END_OF_STACK;
  if (s->count >= s->size) return _URC_END_OF_STACK;
  s->buffer[s->count++] = (void*)pc;
  return _URC_NO_REASON;
}
static inline int backtrace(void** buffer, int size) {
  struct port_bt_state s = {buffer, 0, size};
  _Unwind_Backtrace(port_bt_step, &s);
  return s.count;
}

#ifdef __cplusplus
}
#endif
#endif

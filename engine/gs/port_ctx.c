#include "port_ctx.h"

#include <stdint.h>
#include <string.h>

#ifdef __APPLE__
#define CTX_SYM "_PortCtx_Swap"
#define CTX_TYPE(kind)
#define CTX_SIZE
#else
#define CTX_SYM "PortCtx_Swap"
#define CTX_TYPE(kind) ".type PortCtx_Swap," kind "\n"
#define CTX_SIZE ".size PortCtx_Swap,.-PortCtx_Swap\n"
#endif

#if defined(__x86_64__)
__asm__(".text\n"
        ".globl " CTX_SYM "\n" CTX_TYPE("@function") CTX_SYM ":\n"
        "  pushq %rbp\n"
        "  pushq %rbx\n"
        "  pushq %r12\n"
        "  pushq %r13\n"
        "  pushq %r14\n"
        "  pushq %r15\n"
        "  movq %rsp, (%rdi)\n"
        "  movq %rsi, %rsp\n"
        "  popq %r15\n"
        "  popq %r14\n"
        "  popq %r13\n"
        "  popq %r12\n"
        "  popq %rbx\n"
        "  popq %rbp\n"
        "  ret\n" CTX_SIZE);

PortCtx PortCtx_Make(void* stack, size_t size, void (*entry)(void)) {
  uintptr_t top = ((uintptr_t)stack + size) & ~(uintptr_t)15;
  uint64_t* sp = (uint64_t*)(top - 64);
  memset(sp, 0, 64);
  sp[6] = (uint64_t)(uintptr_t)entry;
  return (PortCtx)sp;
}

#elif defined(__aarch64__)
__asm__(".text\n"
        ".globl " CTX_SYM "\n" CTX_TYPE("%function") CTX_SYM ":\n"
        "  sub sp, sp, #176\n"
        "  stp x19, x20, [sp, #0]\n"
        "  stp x21, x22, [sp, #16]\n"
        "  stp x23, x24, [sp, #32]\n"
        "  stp x25, x26, [sp, #48]\n"
        "  stp x27, x28, [sp, #64]\n"
        "  stp x29, x30, [sp, #80]\n"
        "  stp d8, d9, [sp, #96]\n"
        "  stp d10, d11, [sp, #112]\n"
        "  stp d12, d13, [sp, #128]\n"
        "  stp d14, d15, [sp, #144]\n"
        "  mov x2, sp\n"
        "  str x2, [x0]\n"
        "  mov sp, x1\n"
        "  ldp x19, x20, [sp, #0]\n"
        "  ldp x21, x22, [sp, #16]\n"
        "  ldp x23, x24, [sp, #32]\n"
        "  ldp x25, x26, [sp, #48]\n"
        "  ldp x27, x28, [sp, #64]\n"
        "  ldp x29, x30, [sp, #80]\n"
        "  ldp d8, d9, [sp, #96]\n"
        "  ldp d10, d11, [sp, #112]\n"
        "  ldp d12, d13, [sp, #128]\n"
        "  ldp d14, d15, [sp, #144]\n"
        "  add sp, sp, #176\n"
        "  ret\n" CTX_SIZE);

PortCtx PortCtx_Make(void* stack, size_t size, void (*entry)(void)) {
  uintptr_t top = ((uintptr_t)stack + size) & ~(uintptr_t)15;
  uint64_t* sp = (uint64_t*)(top - 176);
  memset(sp, 0, 176);
  sp[11] = (uint64_t)(uintptr_t)entry;
  return (PortCtx)sp;
}
#else
#error "port_ctx: only x86_64 and arm64 are written"
#endif

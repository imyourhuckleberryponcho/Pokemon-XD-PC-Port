#ifndef PORT_CTX_H
#define PORT_CTX_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void* PortCtx;

PortCtx PortCtx_Make(void* stack, size_t size, void (*entry)(void));
void PortCtx_Swap(PortCtx* save, PortCtx to);

#ifdef __cplusplus
}
#endif
#endif

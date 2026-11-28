#ifndef PORT_WIN_MUSYX_H
#define PORT_WIN_MUSYX_H
#include <stdint.h>
#include "port_addr_window.h"
#ifdef XD_ADDR_WINDOW
#define MUSY_PC_PTR(v) PortWin_Ptr((uint32_t)(v))
#define MUSY_ADDR32(p) PortWin_Off((const void*)(p))
#define MUSY_MALLOC(n) PortWin_Malloc(n)
#define MUSY_FREE(p) PortWin_Free(p)
#endif
#endif

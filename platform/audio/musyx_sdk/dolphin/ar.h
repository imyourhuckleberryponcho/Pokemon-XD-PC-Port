#ifndef PORT_MUSYX_SDK_AR_H
#define PORT_MUSYX_SDK_AR_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARAM_DIR_MRAM_TO_ARAM 0x00
#define ARAM_DIR_ARAM_TO_MRAM 0x01

u32 ARGetBaseAddress(void);
u32 ARGetSize(void);
u32 ARAlloc(u32 length);

#ifdef __cplusplus
}
#endif

#endif

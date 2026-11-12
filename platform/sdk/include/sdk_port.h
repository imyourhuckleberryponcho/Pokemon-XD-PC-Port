#ifndef PLATFORM_SDK_PORT_H
#define PLATFORM_SDK_PORT_H

#define GXInit PortGX_Init
#define GXInitFifoBase PortGX_InitFifoBase
#define GXInitFifoPtrs PortGX_InitFifoPtrs
#define GXGetFifoPtrs PortGX_GetFifoPtrs
#define GXGetCPUFifo PortGX_GetCPUFifo
#define GXGetGPFifo PortGX_GetGPFifo
#define GXSetCPUFifo PortGX_SetCPUFifo
#define GXSetGPFifo PortGX_SetGPFifo
#define GXSaveCPUFifo PortGX_SaveCPUFifo
#define GXEnableBreakPt PortGX_EnableBreakPt
#define GXDisableBreakPt PortGX_DisableBreakPt
#define GXGetGPStatus PortGX_GetGPStatus
#define GXFlush PortGX_Flush
#define GXSetDrawSync PortGX_SetDrawSync
#define GXReadDrawSync PortGX_ReadDrawSync
#define GXSetDrawDone PortGX_SetDrawDone
#define GXDrawDone PortGX_DrawDone
#define GXSetDrawDoneCallback PortGX_SetDrawDoneCallback
#define GXBeginDisplayList PortGX_BeginDisplayList
#define GXEndDisplayList PortGX_EndDisplayList
#define GXCallDisplayList PortGX_CallDisplayList
#define GXCopyDisp PortGX_CopyDisp
#define GXCopyTex PortGX_CopyTex

#include <stddef.h>
#include <stdint.h>
#include <dolphin/types.h>
#include <dolphin/gx/GXVert.h>

#ifdef __cplusplus
extern "C" {
#endif
void PortGX_PipeU8(uint8_t v);
void PortGX_PipeU16(uint16_t v);
void PortGX_PipeU32(uint32_t v);
void PortGX_PipeU64(uint64_t v);
void PortGX_PipeF32(float v);
void PortGX_PipeF64(double v);

void PortGX_SetArrayUnsized(int attr, const void* data, uint8_t stride);

typedef void (*GXBreakPtCallback)(void);
typedef void (*PADSamplingCallback)(void);

void GXSaveGPFifo(void* fifo);
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb);
u32 PortGX_ReadCPUFifoWriteReg(void);
void PortGX_EndOfFrame(void);
void PortGX_TraceOrigins(void (*describe)(const void* address, char* out, size_t outSize));
void* PortMem_LowAlloc(size_t size, size_t align);
uint8_t* PortMem_MEM1(void);
void PortMem_InitOS(uint32_t arenaLo);
void SISetSamplingRate(u32 msec);
PADSamplingCallback PADSetSamplingCallback(PADSamplingCallback callback);
#ifdef __cplusplus
}
#endif

#define DVDReadAsyncPrio PortDVD_ReadAsyncPrio
#define DVDSeekAsyncPrio PortDVD_SeekAsyncPrio
#define DVDGetCommandBlockStatus PortDVD_GetCommandBlockStatus
#define DVDGetDriveStatus PortDVD_GetDriveStatus
#define DVDReadPrio PortDVD_ReadPrio
#define DVDOpen PortDVD_Open
#define ARQPostRequest PortARQ_PostRequest
#define CARDMountAsync PortCARD_MountAsync
#define CARDCheckAsync PortCARD_CheckAsync
#define CARDFormatAsync PortCARD_FormatAsync
#define CARDCreateAsync PortCARD_CreateAsync
#define CARDDeleteAsync PortCARD_DeleteAsync
#define CARDSetAttributesAsync PortCARD_SetAttributesAsync
#define CARDReadAsync PortCARD_ReadAsync
#define CARDSetStatusAsync PortCARD_SetStatusAsync
#define CARDWriteAsync PortCARD_WriteAsync
#define CARDGetResultCode PortCARD_GetResultCode
#define CARDGetSerialNo PortCARD_GetSerialNo
#define CARDProbeEx PortCARD_ProbeEx
#define CARDProbe PortCARD_Probe

#ifdef __cplusplus
extern "C" {
#endif
void PortCARD_Init(void);
#ifdef __cplusplus
}
#endif
#define CARDInit(...) PortCARD_Init()

#define PORTGX_SETARRAY_PICK(_1, _2, _3, _4, _5, NAME, ...) NAME
#define PORTGX_EXPAND(x) x
#define GXSetArray(...) \
  PORTGX_EXPAND(PORTGX_SETARRAY_PICK(__VA_ARGS__, GXSetArray, GXSetArray_4args_unsupported, \
                                     PortGX_SetArrayUnsized, , )(__VA_ARGS__))

#ifdef __cplusplus
namespace port_gx {
struct WGPipe {
  struct P8 { void operator=(uint8_t v) const { PortGX_PipeU8(v); } };
  struct P16 { void operator=(uint16_t v) const { PortGX_PipeU16(v); } };
  struct P32 { void operator=(uint32_t v) const { PortGX_PipeU32(v); } };
  struct P64 { void operator=(uint64_t v) const { PortGX_PipeU64(v); } };
  struct PF32 { void operator=(float v) const { PortGX_PipeF32(v); } };
  struct PF64 { void operator=(double v) const { PortGX_PipeF64(v); } };
  P8 u8, s8;
  P16 u16, s16;
  P32 u32, s32;
  P64 u64, s64;
  PF32 f32;
  PF64 f64;
};
inline WGPipe wgpipe;
}

#undef GXWGFifo
#define GXWGFifo (::port_gx::wgpipe)
#endif

#endif

#ifndef GC_DSP_H
#define GC_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DSP_EXT_NONE,
  DSP_EXT_7,
  DSP_EXT_8,
} DspExtKind;

typedef struct {
  const char* name;
  uint16_t value;
  uint16_t mask;
  uint8_t size;
  DspExtKind ext;
} DspOpInfo;

const DspOpInfo* GcDsp_MainOps(int* count);
const DspOpInfo* GcDsp_ExtOps(int* count);

int GcDsp_DecodeMain(uint16_t word);
int GcDsp_DecodeExt(uint16_t word);
int GcDsp_Size(uint16_t word);

int GcDsp_Disassemble(const uint16_t* words, size_t available, uint16_t address, char* out, size_t outSize);

typedef struct GcDsp GcDsp;

typedef struct {
  void* user;
  uint8_t* (*mainMemory)(void* user, uint32_t address, uint32_t size);
  uint8_t* aram;
  uint32_t aramSize;
  bool dramWordsHostOrder;
  void (*interruptCpu)(void* user);
  void (*log)(void* user, const char* message);
} GcDspHost;

typedef enum {
  GCDSP_ROM_RETURN,
  GCDSP_ROM_WAIT,
  GCDSP_ROM_JUMPED,
} GcDspRomResult;
typedef GcDspRomResult (*GcDspRomRoutine)(GcDsp* dsp, void* user);

void GcDsp_InstallPortRom(GcDsp* dsp);
void GcDsp_PortCoefTable(uint16_t* words);

int GcDsp_ParseCoefDump(const uint8_t* bytes, size_t size, uint16_t* words);

GcDsp* GcDsp_Create(const GcDspHost* host);
void GcDsp_Destroy(GcDsp* dsp);
GcDsp* GcDsp_Clone(const GcDsp* dsp);
void GcDsp_SetHost(GcDsp* dsp, const GcDspHost* host);

void GcDsp_LoadIrom(GcDsp* dsp, const uint16_t* words);
void GcDsp_LoadCoef(GcDsp* dsp, const uint16_t* words);
void GcDsp_SetRomRoutine(GcDsp* dsp, uint16_t address, GcDspRomRoutine routine, void* user);

int GcDsp_Run(GcDsp* dsp, int steps);
int GcDsp_RunUntilIdle(GcDsp* dsp, int maxSteps);

void GcDsp_WriteControl(GcDsp* dsp, uint16_t value);
uint16_t GcDsp_ReadControl(const GcDsp* dsp);
void GcDsp_CpuSendMail(GcDsp* dsp, uint32_t mail);
uint32_t GcDsp_CpuMailToDsp(const GcDsp* dsp);
uint32_t GcDsp_CpuPeekMail(const GcDsp* dsp);
uint32_t GcDsp_CpuTakeMail(GcDsp* dsp);
bool GcDsp_IsHalted(const GcDsp* dsp);

uint16_t GcDsp_ReadReg(GcDsp* dsp, int reg);
void GcDsp_WriteReg(GcDsp* dsp, int reg, uint16_t value);
uint16_t GcDsp_ReadDmem(GcDsp* dsp, uint16_t address);
void GcDsp_WriteDmem(GcDsp* dsp, uint16_t address, uint16_t value);
uint16_t GcDsp_Pc(const GcDsp* dsp);
void GcDsp_SetPc(GcDsp* dsp, uint16_t pc);
int64_t GcDsp_Acc(const GcDsp* dsp, int n);
void GcDsp_SetAcc(GcDsp* dsp, int n, int64_t value);
uint16_t GcDsp_Sr(const GcDsp* dsp);
uint16_t* GcDsp_Iram(GcDsp* dsp);
uint16_t* GcDsp_Dram(GcDsp* dsp);
uint32_t* GcDsp_PortRomState(GcDsp* dsp);
void GcDsp_Log(GcDsp* dsp, const char* message);
size_t GcDsp_SaveState(const GcDsp* dsp, uint8_t* out, size_t size, bool memories);

#ifdef __cplusplus
}
#endif

#endif

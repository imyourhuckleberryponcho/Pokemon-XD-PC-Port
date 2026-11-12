#ifndef DSP_REFERENCE_H
#define DSP_REFERENCE_H
#include <gc_dsp.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RefDsp RefDsp;
RefDsp* RefDsp_Create(const GcDspHost* host);
void RefDsp_Destroy(RefDsp* dsp);
RefDsp* RefDsp_Clone(const RefDsp* dsp);
void RefDsp_LoadIrom(RefDsp* dsp, const uint16_t* words);
void RefDsp_LoadCoef(RefDsp* dsp, const uint16_t* words);
void RefDsp_InstallPortRom(RefDsp* dsp);
int RefDsp_Run(RefDsp* dsp, int steps);
int RefDsp_RunUntilIdle(RefDsp* dsp, int maxSteps);
void RefDsp_WriteControl(RefDsp* dsp, uint16_t value);
uint16_t RefDsp_ReadControl(const RefDsp* dsp);
void RefDsp_CpuSendMail(RefDsp* dsp, uint32_t mail);
uint32_t RefDsp_CpuMailToDsp(const RefDsp* dsp);
uint32_t RefDsp_CpuPeekMail(const RefDsp* dsp);
uint32_t RefDsp_CpuTakeMail(RefDsp* dsp);
bool RefDsp_IsHalted(const RefDsp* dsp);
uint16_t RefDsp_ReadReg(RefDsp* dsp, int reg);
void RefDsp_WriteReg(RefDsp* dsp, int reg, uint16_t value);
uint16_t RefDsp_ReadDmem(RefDsp* dsp, uint16_t address);
void RefDsp_WriteDmem(RefDsp* dsp, uint16_t address, uint16_t value);
uint16_t RefDsp_Pc(const RefDsp* dsp);
void RefDsp_SetPc(RefDsp* dsp, uint16_t pc);
int64_t RefDsp_Acc(const RefDsp* dsp, int n);
void RefDsp_SetAcc(RefDsp* dsp, int n, int64_t value);
uint16_t RefDsp_Sr(const RefDsp* dsp);
uint16_t* RefDsp_Iram(RefDsp* dsp);
uint16_t* RefDsp_Dram(RefDsp* dsp);
size_t RefDsp_SaveState(const RefDsp* dsp, uint8_t* out, size_t size, bool memories);
#ifdef __cplusplus
}
#endif
#endif

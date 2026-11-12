#ifndef PORT_MUSYX_SDK_DSP_H
#define PORT_MUSYX_SDK_DSP_H

#include <dolphin/os.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*DSPCallback)(void* task);
typedef struct STRUCT_DSP_TASK DSPTaskInfo;

typedef struct STRUCT_DSP_TASK {
  volatile u32 state;
  volatile u32 priority;
  volatile u32 flags;
  u16* iram_mmem_addr;
  u32 iram_length;
  u32 iram_addr;
  u16* dram_mmem_addr;
  u32 dram_length;
  u32 dram_addr;
  u16 dsp_init_vector;
  u16 dsp_resume_vector;
  DSPCallback init_cb;
  DSPCallback res_cb;
  DSPCallback done_cb;
  DSPCallback req_cb;
  DSPTaskInfo* next;
  DSPTaskInfo* prev;
  OSTime t_context;
  OSTime t_task;
} DSPTaskInfo;

u32 DSPCheckMailToDSP(void);
u32 DSPCheckMailFromDSP(void);
u32 DSPReadMailFromDSP(void);
void DSPSendMailToDSP(u32 mail);
void DSPInit(void);
BOOL DSPCheckInit(void);
void DSPReset(void);
void DSPHalt(void);
u32 DSPGetDMAStatus(void);
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task);
DSPTaskInfo* DSPCancelTask(DSPTaskInfo* task);

#ifdef __cplusplus
}
#endif

#endif

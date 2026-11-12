#include "dol_tables.h"

#include <string.h>

#define DSP_SLAVE_SIZE 0x1EC0

__declspec(align(32)) char dspSlave[DSP_SLAVE_SIZE];
unsigned short dspSlaveLength = DSP_SLAVE_SIZE;

bool PortXD_LoadDspProgram(void) {
  const uint8_t* program = PortXD_GetDolTable("dspSlave");
  if (program == NULL) {
    return false;
  }
  memcpy(dspSlave, program, DSP_SLAVE_SIZE);
  return true;
}

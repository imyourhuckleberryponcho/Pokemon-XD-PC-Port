#include <cstdint>

extern "C" {

int PortXD_MemoAisyouRow(int32_t spriteId, uint16_t* type) {
  if (spriteId != 2000) return 0;
  *type = 18;
  return 1;
}

int PortXD_ExpAllOn(void) { return 0; }
unsigned PortXD_ExpAllBench(unsigned base, int metCount) {
  (void)base;
  (void)metCount;
  return 0;
}

}

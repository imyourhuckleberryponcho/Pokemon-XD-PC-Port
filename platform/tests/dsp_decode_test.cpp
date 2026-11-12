#include <gc_dsp.h>

#include <cstdio>
#include <cstring>

int main() {
  int mainCount = 0, extCount = 0;
  const DspOpInfo* ops = GcDsp_MainOps(&mainCount);
  const DspOpInfo* ext = GcDsp_ExtOps(&extCount);
  int overlaps = 0, unknown = 0;
  for (int w = 0; w < 65536; w++) {
    int matches = 0;
    for (int i = 0; i < mainCount; i++) {
      matches += (w & ops[i].mask) == ops[i].value;
    }
    if (matches > 1) {
      if (overlaps < 10) {
        std::printf("word %04X matches %d main instructions:", w, matches);
        for (int i = 0; i < mainCount; i++) {
          if ((w & ops[i].mask) == ops[i].value) {
            std::printf(" %s", ops[i].name);
          }
        }
        std::printf("\n");
      }
      overlaps++;
    }
    unknown += matches == 0;
  }
  int extOverlaps = 0;
  for (int b = 0; b < 256; b++) {
    int matches = 0;
    bool ldaxSlot = false;
    for (int i = 0; i < extCount; i++) {
      if ((b & ext[i].mask) == ext[i].value) {
        matches++;
        ldaxSlot |= std::strncmp(ext[i].name, "'LDAX", 5) == 0;
      }
    }
    if (matches > 1 && !ldaxSlot) {
      std::printf("extension byte %02X matches %d operations\n", b, matches);
      extOverlaps++;
    }
  }
  std::printf("%d main instructions, %d extended operations; %d words are no listed instruction\n", mainCount,
              extCount, unknown);
  const bool ok = overlaps == 0 && extOverlaps == 0;
  std::printf("%s no word matches two instructions\n", ok ? "ok  " : "FAIL");
  return ok ? 0 : 1;
}

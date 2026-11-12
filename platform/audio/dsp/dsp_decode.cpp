#include "gc_dsp.h"

#include <cstdio>
#include <cstring>

namespace {

const DspOpInfo kMain[] = {
    {"NOP", 0x0000, 0xFFFF, 1, DSP_EXT_NONE},
    {"DAR", 0x0004, 0xFFFC, 1, DSP_EXT_NONE},
    {"IAR", 0x0008, 0xFFFC, 1, DSP_EXT_NONE},
    {"SUBARN", 0x000C, 0xFFFC, 1, DSP_EXT_NONE},
    {"ADDARN", 0x0010, 0xFFF0, 1, DSP_EXT_NONE},
    {"HALT", 0x0021, 0xFFFF, 1, DSP_EXT_NONE},
    {"LOOP", 0x0040, 0xFFE0, 1, DSP_EXT_NONE},
    {"BLOOP", 0x0060, 0xFFE0, 2, DSP_EXT_NONE},
    {"LRI", 0x0080, 0xFFE0, 2, DSP_EXT_NONE},
    {"LR", 0x00C0, 0xFFE0, 2, DSP_EXT_NONE},
    {"SR", 0x00E0, 0xFFE0, 2, DSP_EXT_NONE},
    {"IF", 0x0270, 0xFFF0, 1, DSP_EXT_NONE},
    {"J", 0x0290, 0xFFF0, 2, DSP_EXT_NONE},
    {"CALL", 0x02B0, 0xFFF0, 2, DSP_EXT_NONE},
    {"RET", 0x02D0, 0xFFF0, 1, DSP_EXT_NONE},
    {"RTI", 0x02F0, 0xFFF0, 1, DSP_EXT_NONE},
    {"LSRN", 0x02CA, 0xFFFF, 1, DSP_EXT_NONE},
    {"ASRN", 0x02CB, 0xFFFF, 1, DSP_EXT_NONE},
    {"ADDI", 0x0200, 0xFEFF, 2, DSP_EXT_NONE},
    {"XORI", 0x0220, 0xFEFF, 2, DSP_EXT_NONE},
    {"ANDI", 0x0240, 0xFEFF, 2, DSP_EXT_NONE},
    {"ORI", 0x0260, 0xFEFF, 2, DSP_EXT_NONE},
    {"CMPI", 0x0280, 0xFEFF, 2, DSP_EXT_NONE},
    {"ANDF", 0x02A0, 0xFEFF, 2, DSP_EXT_NONE},
    {"ANDCF", 0x02C0, 0xFEFF, 2, DSP_EXT_NONE},
    {"ILRR", 0x0210, 0xFEFC, 1, DSP_EXT_NONE},
    {"ILRRD", 0x0214, 0xFEFC, 1, DSP_EXT_NONE},
    {"ILRRI", 0x0218, 0xFEFC, 1, DSP_EXT_NONE},
    {"ILRRN", 0x021C, 0xFEFC, 1, DSP_EXT_NONE},
    {"ADDIS", 0x0400, 0xFE00, 1, DSP_EXT_NONE},
    {"CMPIS", 0x0600, 0xFE00, 1, DSP_EXT_NONE},
    {"LRIS", 0x0800, 0xF800, 1, DSP_EXT_NONE},
    {"LOOPI", 0x1000, 0xFF00, 1, DSP_EXT_NONE},
    {"BLOOPI", 0x1100, 0xFF00, 2, DSP_EXT_NONE},
    {"SBCLR", 0x1200, 0xFF00, 1, DSP_EXT_NONE},
    {"SBSET", 0x1300, 0xFF00, 1, DSP_EXT_NONE},
    {"LSL", 0x1400, 0xFEC0, 1, DSP_EXT_NONE},
    {"LSR", 0x1440, 0xFEC0, 1, DSP_EXT_NONE},
    {"ASL", 0x1480, 0xFEC0, 1, DSP_EXT_NONE},
    {"ASR", 0x14C0, 0xFEC0, 1, DSP_EXT_NONE},
    {"SI", 0x1600, 0xFF00, 2, DSP_EXT_NONE},
    {"JR", 0x1700, 0xFF10, 1, DSP_EXT_NONE},
    {"CALLR", 0x1710, 0xFF10, 1, DSP_EXT_NONE},
    {"LRR", 0x1800, 0xFF80, 1, DSP_EXT_NONE},
    {"LRRD", 0x1880, 0xFF80, 1, DSP_EXT_NONE},
    {"LRRI", 0x1900, 0xFF80, 1, DSP_EXT_NONE},
    {"LRRN", 0x1980, 0xFF80, 1, DSP_EXT_NONE},
    {"SRR", 0x1A00, 0xFF80, 1, DSP_EXT_NONE},
    {"SRRD", 0x1A80, 0xFF80, 1, DSP_EXT_NONE},
    {"SRRI", 0x1B00, 0xFF80, 1, DSP_EXT_NONE},
    {"SRRN", 0x1B80, 0xFF80, 1, DSP_EXT_NONE},
    {"MRR", 0x1C00, 0xFC00, 1, DSP_EXT_NONE},
    {"LRS", 0x2000, 0xF800, 1, DSP_EXT_NONE},
    {"SRSH", 0x2800, 0xFE00, 1, DSP_EXT_NONE},
    {"SRS", 0x2C00, 0xFC00, 1, DSP_EXT_NONE},
    {"XORR", 0x3000, 0xFC80, 1, DSP_EXT_7},
    {"ANDR", 0x3400, 0xFC80, 1, DSP_EXT_7},
    {"ORR", 0x3800, 0xFC80, 1, DSP_EXT_7},
    {"ANDC", 0x3C00, 0xFE80, 1, DSP_EXT_7},
    {"ORC", 0x3E00, 0xFE80, 1, DSP_EXT_7},
    {"XORC", 0x3080, 0xFE80, 1, DSP_EXT_7},
    {"NOT", 0x3280, 0xFE80, 1, DSP_EXT_7},
    {"LSRNRX", 0x3480, 0xFC80, 1, DSP_EXT_7},
    {"ASRNRX", 0x3880, 0xFC80, 1, DSP_EXT_7},
    {"LSRNR", 0x3C80, 0xFE80, 1, DSP_EXT_7},
    {"ASRNR", 0x3E80, 0xFE80, 1, DSP_EXT_7},
    {"ADDR", 0x4000, 0xF800, 1, DSP_EXT_8},
    {"ADDAX", 0x4800, 0xFC00, 1, DSP_EXT_8},
    {"ADD", 0x4C00, 0xFE00, 1, DSP_EXT_8},
    {"ADDP", 0x4E00, 0xFE00, 1, DSP_EXT_8},
    {"SUBR", 0x5000, 0xF800, 1, DSP_EXT_8},
    {"SUBAX", 0x5800, 0xFC00, 1, DSP_EXT_8},
    {"SUB", 0x5C00, 0xFE00, 1, DSP_EXT_8},
    {"SUBP", 0x5E00, 0xFE00, 1, DSP_EXT_8},
    {"MOVR", 0x6000, 0xF800, 1, DSP_EXT_8},
    {"MOVAX", 0x6800, 0xFC00, 1, DSP_EXT_8},
    {"MOV", 0x6C00, 0xFE00, 1, DSP_EXT_8},
    {"MOVP", 0x6E00, 0xFE00, 1, DSP_EXT_8},
    {"ADDAXL", 0x7000, 0xFC00, 1, DSP_EXT_8},
    {"INCM", 0x7400, 0xFE00, 1, DSP_EXT_8},
    {"INC", 0x7600, 0xFE00, 1, DSP_EXT_8},
    {"DECM", 0x7800, 0xFE00, 1, DSP_EXT_8},
    {"DEC", 0x7A00, 0xFE00, 1, DSP_EXT_8},
    {"NEG", 0x7C00, 0xFE00, 1, DSP_EXT_8},
    {"MOVNP", 0x7E00, 0xFE00, 1, DSP_EXT_8},
    {"NX", 0x8000, 0xF700, 1, DSP_EXT_8},
    {"CLR", 0x8100, 0xF700, 1, DSP_EXT_8},
    {"CMP", 0x8200, 0xFF00, 1, DSP_EXT_8},
    {"MULAXH", 0x8300, 0xFF00, 1, DSP_EXT_8},
    {"CLRP", 0x8400, 0xFF00, 1, DSP_EXT_8},
    {"TSTPROD", 0x8500, 0xFF00, 1, DSP_EXT_8},
    {"TSTAXH", 0x8600, 0xFE00, 1, DSP_EXT_8},
    {"M2", 0x8A00, 0xFF00, 1, DSP_EXT_8},
    {"M0", 0x8B00, 0xFF00, 1, DSP_EXT_8},
    {"CLR15", 0x8C00, 0xFF00, 1, DSP_EXT_8},
    {"SET15", 0x8D00, 0xFF00, 1, DSP_EXT_8},
    {"SET16", 0x8E00, 0xFF00, 1, DSP_EXT_8},
    {"SET40", 0x8F00, 0xFF00, 1, DSP_EXT_8},
    {"MUL", 0x9000, 0xF700, 1, DSP_EXT_8},
    {"ASR16", 0x9100, 0xF700, 1, DSP_EXT_8},
    {"MULMVZ", 0x9200, 0xF600, 1, DSP_EXT_8},
    {"MULAC", 0x9400, 0xF600, 1, DSP_EXT_8},
    {"MULMV", 0x9600, 0xF600, 1, DSP_EXT_8},
    {"MULX", 0xA000, 0xE700, 1, DSP_EXT_8},
    {"ABS", 0xA100, 0xF700, 1, DSP_EXT_8},
    {"TST", 0xB100, 0xF700, 1, DSP_EXT_8},
    {"MULXMVZ", 0xA200, 0xE600, 1, DSP_EXT_8},
    {"MULXAC", 0xA400, 0xE600, 1, DSP_EXT_8},
    {"MULXMV", 0xA600, 0xE600, 1, DSP_EXT_8},
    {"MULC", 0xC000, 0xE700, 1, DSP_EXT_8},
    {"CMPAXH", 0xC100, 0xE700, 1, DSP_EXT_8},
    {"MULCMVZ", 0xC200, 0xE600, 1, DSP_EXT_8},
    {"MULCAC", 0xC400, 0xE600, 1, DSP_EXT_8},
    {"MULCMV", 0xC600, 0xE600, 1, DSP_EXT_8},
    {"MADDX", 0xE000, 0xFC00, 1, DSP_EXT_8},
    {"MSUBX", 0xE400, 0xFC00, 1, DSP_EXT_8},
    {"MADDC", 0xE800, 0xFC00, 1, DSP_EXT_8},
    {"MSUBC", 0xEC00, 0xFC00, 1, DSP_EXT_8},
    {"LSL16", 0xF000, 0xFE00, 1, DSP_EXT_8},
    {"MADD", 0xF200, 0xFE00, 1, DSP_EXT_8},
    {"LSR16", 0xF400, 0xFE00, 1, DSP_EXT_8},
    {"MSUB", 0xF600, 0xFE00, 1, DSP_EXT_8},
    {"ADDPAXZ", 0xF800, 0xFC00, 1, DSP_EXT_8},
    {"CLRL", 0xFC00, 0xFE00, 1, DSP_EXT_8},
    {"MOVPZ", 0xFE00, 0xFE00, 1, DSP_EXT_8},
};

const DspOpInfo kExt[] = {
    {"'NOP", 0x00, 0xFC, 0, DSP_EXT_NONE},
    {"'DR", 0x04, 0xFC, 0, DSP_EXT_NONE},
    {"'IR", 0x08, 0xFC, 0, DSP_EXT_NONE},
    {"'NR", 0x0C, 0xFC, 0, DSP_EXT_NONE},
    {"'MV", 0x10, 0xF0, 0, DSP_EXT_NONE},
    {"'S", 0x20, 0xE4, 0, DSP_EXT_NONE},
    {"'SN", 0x24, 0xE4, 0, DSP_EXT_NONE},
    {"'L", 0x40, 0xC4, 0, DSP_EXT_NONE},
    {"'LN", 0x44, 0xC4, 0, DSP_EXT_NONE},
    {"'LS", 0x80, 0xCE, 0, DSP_EXT_NONE},
    {"'SL", 0x82, 0xCE, 0, DSP_EXT_NONE},
    {"'LSN", 0x84, 0xCE, 0, DSP_EXT_NONE},
    {"'SLN", 0x86, 0xCE, 0, DSP_EXT_NONE},
    {"'LSM", 0x88, 0xCE, 0, DSP_EXT_NONE},
    {"'SLM", 0x8A, 0xCE, 0, DSP_EXT_NONE},
    {"'LSNM", 0x8C, 0xCE, 0, DSP_EXT_NONE},
    {"'SLNM", 0x8E, 0xCE, 0, DSP_EXT_NONE},
    {"'LDAX", 0xC3, 0xCF, 0, DSP_EXT_NONE},
    {"'LDAXN", 0xC7, 0xCF, 0, DSP_EXT_NONE},
    {"'LDAXM", 0xCB, 0xCF, 0, DSP_EXT_NONE},
    {"'LDAXNM", 0xCF, 0xCF, 0, DSP_EXT_NONE},
    {"'LD", 0xC0, 0xCC, 0, DSP_EXT_NONE},
    {"'LDN", 0xC4, 0xCC, 0, DSP_EXT_NONE},
    {"'LDM", 0xC8, 0xCC, 0, DSP_EXT_NONE},
    {"'LDNM", 0xCC, 0xCC, 0, DSP_EXT_NONE},
};

constexpr int kMainCount = sizeof(kMain) / sizeof(kMain[0]);
constexpr int kExtCount = sizeof(kExt) / sizeof(kExt[0]);

struct Tables {
  short main[65536];
  short ext[256];
  Tables() {
    for (int w = 0; w < 65536; w++) {
      main[w] = -1;
      for (int i = 0; i < kMainCount; i++) {
        if ((w & kMain[i].mask) == kMain[i].value) {
          main[w] = static_cast<short>(i);
          break;
        }
      }
    }
    for (int b = 0; b < 256; b++) {
      ext[b] = -1;
      for (int i = 0; i < kExtCount; i++) {
        if ((b & kExt[i].mask) == kExt[i].value) {
          ext[b] = static_cast<short>(i);
          break;
        }
      }
    }
  }
};

const Tables& tables() {
  static const Tables t;
  return t;
}

const char* const kRegNames[32] = {"ar0", "ar1", "ar2", "ar3", "ix0", "ix1", "ix2", "ix3", "wr0", "wr1", "wr2",
                                   "wr3", "st0", "st1", "st2", "st3", "ac0.h", "ac1.h", "config", "sr", "prod.l",
                                   "prod.m1", "prod.h", "prod.m2", "ax0.l", "ax1.l", "ax0.h", "ax1.h", "ac0.l",
                                   "ac1.l", "ac0.m", "ac1.m"};
const char* const kCond[16] = {"GE", "L", "G", "LE", "NZ", "Z", "NC", "C", "x8", "x9", "xA", "xB", "LNZ", "LZ", "O", ""};

}

extern "C" {

const DspOpInfo* GcDsp_MainOps(int* count) {
  *count = kMainCount;
  return kMain;
}

const DspOpInfo* GcDsp_ExtOps(int* count) {
  *count = kExtCount;
  return kExt;
}

int GcDsp_DecodeMain(uint16_t word) { return tables().main[word]; }

int GcDsp_DecodeExt(uint16_t word) {
  const int op = tables().main[word];
  if (op < 0 || kMain[op].ext == DSP_EXT_NONE) {
    return -1;
  }
  const uint8_t bits = kMain[op].ext == DSP_EXT_7 ? (word & 0x7F) : (word & 0xFF);
  return tables().ext[bits];
}

int GcDsp_Size(uint16_t word) {
  const int op = tables().main[word];
  return op < 0 ? 1 : kMain[op].size;
}

int GcDsp_Disassemble(const uint16_t* words, size_t available, uint16_t address, char* out, size_t outSize) {
  (void)address;
  const uint16_t w = words[0];
  const int op = GcDsp_DecodeMain(w);
  if (op < 0) {
    std::snprintf(out, outSize, "??? %04X", w);
    return 1;
  }
  const DspOpInfo& info = kMain[op];
  const uint16_t w2 = info.size == 2 && available > 1 ? words[1] : 0;
  const char* name = info.name;
  char args[64] = "";
  if (std::strcmp(name, "J") == 0 || std::strcmp(name, "CALL") == 0) {
    std::snprintf(out, outSize, "%s%s 0x%04X", name, kCond[w & 15], w2);
    return info.size;
  }
  if (std::strcmp(name, "IF") == 0 || std::strcmp(name, "RET") == 0 || std::strcmp(name, "RTI") == 0) {
    std::snprintf(out, outSize, "%s%s", name, kCond[w & 15]);
    return info.size;
  }
  if (std::strcmp(name, "LRI") == 0 || std::strcmp(name, "LR") == 0) {
    std::snprintf(args, sizeof(args), " $%s, 0x%04X", kRegNames[w & 31], w2);
  } else if (std::strcmp(name, "SR") == 0) {
    std::snprintf(args, sizeof(args), " @0x%04X, $%s", w2, kRegNames[w & 31]);
  } else if (std::strcmp(name, "BLOOP") == 0 || std::strcmp(name, "LOOP") == 0) {
    std::snprintf(args, sizeof(args), " $%s%s", kRegNames[w & 31], info.size == 2 ? "" : "");
    if (info.size == 2) {
      std::snprintf(args + std::strlen(args), sizeof(args) - std::strlen(args), ", 0x%04X", w2);
    }
  } else if (std::strcmp(name, "BLOOPI") == 0) {
    std::snprintf(args, sizeof(args), " %u, 0x%04X", w & 0xFF, w2);
  } else if (std::strcmp(name, "SI") == 0) {
    std::snprintf(args, sizeof(args), " @0x%04X, 0x%04X", 0xFF00 | (w & 0xFF), w2);
  } else if (std::strcmp(name, "MRR") == 0) {
    std::snprintf(args, sizeof(args), " $%s, $%s", kRegNames[(w >> 5) & 31], kRegNames[w & 31]);
  } else if (std::strncmp(name, "LRR", 3) == 0) {
    std::snprintf(args, sizeof(args), " $%s, @$ar%d", kRegNames[w & 31], (w >> 5) & 3);
  } else if (std::strncmp(name, "SRR", 3) == 0) {
    std::snprintf(args, sizeof(args), " @$ar%d, $%s", (w >> 5) & 3, kRegNames[w & 31]);
  } else if (std::strcmp(name, "ILRR") == 0 || std::strcmp(name, "ILRRD") == 0 || std::strcmp(name, "ILRRI") == 0 ||
             std::strcmp(name, "ILRRN") == 0) {
    std::snprintf(args, sizeof(args), " $ac%d.m, @$ar%d", (w >> 8) & 1, w & 3);
  } else if (std::strcmp(name, "LRS") == 0) {
    std::snprintf(args, sizeof(args), " $%s, @0x%02X", kRegNames[0x18 + ((w >> 8) & 7)], w & 0xFF);
  } else if (std::strcmp(name, "SRS") == 0) {
    std::snprintf(args, sizeof(args), " @0x%02X, $%s", w & 0xFF, kRegNames[0x1C + ((w >> 8) & 3)]);
  } else if (std::strcmp(name, "SRSH") == 0) {
    std::snprintf(args, sizeof(args), " @0x%02X, $ac%d.h", w & 0xFF, (w >> 8) & 1);
  } else if (std::strcmp(name, "LRIS") == 0) {
    std::snprintf(args, sizeof(args), " $%s, %d", kRegNames[0x18 + ((w >> 8) & 7)], static_cast<int8_t>(w & 0xFF));
  } else if (std::strcmp(name, "ADDIS") == 0 || std::strcmp(name, "CMPIS") == 0) {
    std::snprintf(args, sizeof(args), " $ac%d, %d", (w >> 8) & 1, static_cast<int8_t>(w & 0xFF));
  } else if (std::strcmp(name, "LSL") == 0 || std::strcmp(name, "ASL") == 0) {
    std::snprintf(args, sizeof(args), " $ac%d, %d", (w >> 8) & 1, w & 0x3F);
  } else if (std::strcmp(name, "LSR") == 0 || std::strcmp(name, "ASR") == 0) {
    std::snprintf(args, sizeof(args), " $ac%d, %d", (w >> 8) & 1, (w & 0x3F) == 0 ? 0 : 64 - (w & 0x3F));
  } else if (std::strcmp(name, "SBSET") == 0 || std::strcmp(name, "SBCLR") == 0) {
    std::snprintf(args, sizeof(args), " SR bit %d", (w & 7) + 6);
  } else if (std::strcmp(name, "LOOPI") == 0) {
    std::snprintf(args, sizeof(args), " %u", w & 0xFF);
  } else if (std::strcmp(name, "JR") == 0 || std::strcmp(name, "CALLR") == 0) {
    std::snprintf(out, outSize, "%s%s $%s", name, kCond[w & 15], kRegNames[(w >> 5) & 7]);
    return info.size;
  } else if (std::strcmp(name, "DAR") == 0 || std::strcmp(name, "IAR") == 0) {
    std::snprintf(args, sizeof(args), " $ar%d", w & 3);
  } else if (std::strcmp(name, "ADDARN") == 0) {
    std::snprintf(args, sizeof(args), " $ar%d, $ix%d", w & 3, (w >> 2) & 3);
  } else if (std::strcmp(name, "SUBARN") == 0) {
    std::snprintf(args, sizeof(args), " $ar%d", w & 3);
  } else if (info.size == 2) {
    std::snprintf(args, sizeof(args), " 0x%04X", w2);
  }
  const int ext = GcDsp_DecodeExt(w);
  if (ext >= 0 && ext != 0) {
    std::snprintf(out, outSize, "%s%s%s : %04X", name, kExt[ext].name, args, w);
  } else {
    std::snprintf(out, outSize, "%s%s : %04X", name, args, w);
  }
  return info.size;
}

}

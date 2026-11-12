#include <dolphin/gx.h>

#define SAMPLE_1X { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }
#define SAMPLE_AA { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }
#define VFILTER_OFF { 0, 0, 21, 22, 21, 0, 0 }
#define VFILTER_DF { 8, 8, 10, 12, 10, 8, 8 }
#define VFILTER_AA { 4, 8, 12, 16, 12, 8, 4 }

GXRenderModeObj GXNtsc240Ds = {1, 640, 240, 240, 40, 0, 640, 480, 0, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXNtsc240DsAa = {1, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXNtsc240Int = {0, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXNtsc240IntAa = {0, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXNtsc480IntAa = {0, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, SAMPLE_AA, VFILTER_AA};
GXRenderModeObj GXNtsc480Prog = {2, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXNtsc480ProgSoft = {2, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0, SAMPLE_1X, VFILTER_DF};
GXRenderModeObj GXNtsc480ProgAa = {2, 640, 242, 480, 40, 0, 640, 480, 0, 0, 1, SAMPLE_AA, VFILTER_AA};
GXRenderModeObj GXMpal240Ds = {9, 640, 240, 240, 40, 0, 640, 480, 0, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXMpal240DsAa = {9, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXMpal240Int = {8, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXMpal240IntAa = {8, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXMpal480Int = {8, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXMpal480IntAa = {8, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, SAMPLE_AA, VFILTER_AA};
GXRenderModeObj GXPal264Ds = {5, 640, 264, 264, 40, 11, 640, 528, 0, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXPal264DsAa = {5, 640, 264, 264, 40, 11, 640, 528, 0, 0, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXPal264Int = {4, 640, 264, 264, 40, 23, 640, 528, 0, 1, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXPal264IntAa = {4, 640, 264, 264, 40, 23, 640, 528, 0, 1, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXPal528Int = {4, 640, 528, 528, 40, 23, 640, 528, 1, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXPal524IntAa = {4, 640, 264, 524, 40, 23, 640, 524, 1, 0, 1, SAMPLE_AA, VFILTER_AA};
GXRenderModeObj GXEurgb60Hz240Ds = {21, 640, 240, 240, 40, 0, 640, 480, 0, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXEurgb60Hz240DsAa = {21, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXEurgb60Hz240Int = {20, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXEurgb60Hz240IntAa = {20, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, SAMPLE_AA, VFILTER_OFF};
GXRenderModeObj GXEurgb60Hz480IntDf = {20, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, SAMPLE_1X, VFILTER_DF};
GXRenderModeObj GXEurgb60Hz480Int = {20, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, SAMPLE_1X, VFILTER_OFF};
GXRenderModeObj GXEurgb60Hz480IntAa = {20, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, SAMPLE_AA, VFILTER_AA};

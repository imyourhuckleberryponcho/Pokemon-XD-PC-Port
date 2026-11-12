#ifndef GX_TEXTURE_DECODE_H
#define GX_TEXTURE_DECODE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool PortGX_DecodeTexture(const void* data, uint32_t width, uint32_t height, uint32_t format, const void* tlut,
                          uint32_t tlutFormat, uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif

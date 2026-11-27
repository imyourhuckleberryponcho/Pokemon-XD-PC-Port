#ifndef ENGINE_ROOM_C_API_H
#define ENGINE_ROOM_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XDR_API_VERSION 1
int xdr_api_version(void);

void xdr_free(void* p);

char* xdr_recipe_check(const char* recipe_json);

char* xdr_map_parts(const uint8_t* map, size_t map_size, char** error);

uint8_t* xdr_collision_png(const uint8_t* collision, size_t collision_size, int pixels, size_t* png_size);

typedef int (*xdr_source_map_fn)(const char* archive, uint8_t** data, size_t* size, void* user);

typedef struct xdr_result {
  uint8_t* map;        size_t map_size;
  uint8_t* collision;  size_t collision_size;
  char* report_json;
} xdr_result;

int xdr_build(const char* recipe_json, const uint8_t* base_map, size_t base_map_size, const uint8_t* base_collision, size_t base_collision_size,
              xdr_source_map_fn source, void* user, uint32_t max_archive_bytes, uint32_t base_archive_bytes, xdr_result* out);
void xdr_result_free(xdr_result* r);

#ifdef __cplusplus
}
#endif
#endif

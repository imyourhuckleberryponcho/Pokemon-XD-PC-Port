#ifndef PORT_MAC_MALLOC_H
#define PORT_MAC_MALLOC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* block, size_t size);
void free(void* block);
#ifdef __cplusplus
}
#endif
#define M_MMAP_THRESHOLD 1
#define M_ARENA_MAX 2
#define M_TOP_PAD 3
#define mallopt(what, value) ((void)(what), (void)(value), 1)
#endif

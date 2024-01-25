#ifndef NATURE_FIXALLOC_H
#define NATURE_FIXALLOC_H

#include <stdint.h>
#include <stdlib.h>

#define FIXALLOC_CHUNK_LIMIT 16384

typedef struct fixalloc_link_t {
    struct fixalloc_link_t* next;
} fixalloc_link_t;

typedef struct {
    uintptr_t size; // 单个元素的 size
    fixalloc_link_t* free_list;
    uintptr_t chunk_ptr;
    uint32_t chunk_rem;  // chunk 剩余可用的空间
    uint32_t chunk_size; // chunk size
    uintptr_t inuse;     // fixalloc alloc+free size 汇总, 主要用于 stat
} fixalloc_t;

void fixalloc_init(fixalloc_t* f, uintptr_t fix_size);

void* fixalloc_alloc(fixalloc_t* f);

void fixalloc_free(fixalloc_t* f, void* p);

#endif // NATURE_FIXALLOC_H

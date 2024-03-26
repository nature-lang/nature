#ifndef NATURE_FIXALLOC_H
#define NATURE_FIXALLOC_H

#include <stdint.h>
#include <stdlib.h>

#define FIXALLOC_CHUNK_LIMIT 16384// 16KB

typedef struct fixalloc_link_t {
    struct fixalloc_link_t *next;
} fixalloc_link_t;

typedef struct {
    uintptr_t size;            // 单个元素的 size
    fixalloc_link_t *free_list;// 释放节点都 push 在 free_list 中， 其 item 大小需要大于一个指针的大小
    uintptr_t chunk_ptr;
    uint32_t chunk_rem;         // chunk 剩余可用的空间
    uint32_t chunk_size;        // chunk size
    uintptr_t inuse;            // fixalloc alloc+free size 汇总, 主要用于 stat
    fixalloc_link_t *chunk_list;// 每一个 chunk 的前 8byte 是链表 next 位置
} fixalloc_t;

void fixalloc_init(fixalloc_t *f, uintptr_t fix_size);

void *fixalloc_alloc(fixalloc_t *f);

void fixalloc_free(fixalloc_t *f, void *p);

#endif// NATURE_FIXALLOC_H

//
// Created by weiwenhao on 2024/1/25.
//

#include "fixalloc.h"

#include <assert.h>
#include <string.h>

#include "utils/helper.h"

void fixalloc_init(fixalloc_t *f, uintptr_t fix_size) {
    assert(fix_size <= FIXALLOC_CHUNK_LIMIT && "fixalloc_init: fix_size too large");

    if (fix_size < sizeof(fixalloc_link_t)) {
        fix_size = sizeof(fixalloc_link_t);
    }

    f->size = fix_size;
    f->chunk_ptr = 0;
    f->chunk_rem = 0;
    f->chunk_size = (uint32_t)(FIXALLOC_CHUNK_LIMIT / fix_size * fix_size); // 向下取证避免尾部浪费
    f->inuse = 0;
}

void *fixalloc_alloc(fixalloc_t *f) {
    assert(f->size > 0 && "fixalloc_alloc: uninitialized");

    // free list 主要是由 free 释放的 obj
    if (f->free_list != NULL) {
        void *v = f->free_list;
        f->free_list = f->free_list->next;
        f->inuse += f->size;
        memset(v, 0, f->size);
        return v;
    }

    if (f->chunk_rem < f->size) {
        // 申请新的 chunk
        f->chunk_ptr = (uintptr_t)sys_memory_map(NULL, f->chunk_size);
        f->chunk_rem = f->chunk_size;
    }

    void *v = (void *)f->chunk_ptr;
    f->chunk_ptr += f->size;
    f->chunk_rem -= f->size;
    f->inuse += f->size;

    memset(v, 0, f->size);
    return v;
}

void fixalloc_free(fixalloc_t *f, void *p) {
    f->inuse -= f->size;

    // 直接改变指针结构，将 v 存放在 free_list 头部
    fixalloc_link_t *v = p;
    v->next = f->free_list;
    f->free_list = v;
}

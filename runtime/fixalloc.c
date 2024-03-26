#include "fixalloc.h"

#include <assert.h>
#include <string.h>
#include <uv.h>

#include "utils/helper.h"

void fixalloc_init(fixalloc_t *f, uintptr_t fix_size) {
    assert(fix_size <= FIXALLOC_CHUNK_LIMIT && "fixalloc_init: fix_size too large");

    if (fix_size < sizeof(fixalloc_link_t)) {
        fix_size = sizeof(fixalloc_link_t);
    }

    f->size = fix_size;
    f->chunk_ptr = 0;
    f->chunk_rem = 0;
    f->chunk_size = (uint32_t) (FIXALLOC_CHUNK_LIMIT / fix_size * fix_size);// 向下取证避免尾部浪费
    f->free_list = NULL;
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

        DEBUGF("[fixalloc_alloc] f v=%p", v);
        return v;
    }

    if (f->chunk_rem < f->size) {
        // 申请新的 chunk
        f->chunk_ptr = (uintptr_t) sys_memory_map(NULL, f->chunk_size);
        f->chunk_rem = f->chunk_size;
    }

    void *v = (void *) f->chunk_ptr;
    f->chunk_ptr += f->size;
    f->chunk_rem -= f->size;
    f->inuse += f->size;

    memset(v, 0, f->size);
    DEBUGF("[fixalloc_alloc] n v=%p", v);
    return v;
}

void fixalloc_free(fixalloc_t *f, void *p) {
    DEBUGF("[fixalloc_free] f=%p,v=%p", f, p);
    f->inuse -= f->size;

    // TODO 清空 p 中的数据，避免存在错误引用而没有报错出来, 正式环境用不上
    //    memset(p, 0, f->size);

    // 直接改变指针结构，将 v 存放在 free_list 头部
    fixalloc_link_t *v = p;// sizeof(fixalloc_link_t) = ptr_size
    v->next = f->free_list;// 清空或者重新为 ptr size 赋值
    f->free_list = v;
}

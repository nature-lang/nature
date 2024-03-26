#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>

#include "aco/aco.h"
#include "runtime.h"
#include "sizeclass.h"
#include "utils/custom_links.h"
#include "utils/helper.h"

extern memory_t *memory;
extern uint64_t remove_total_bytes;    // 当前回收到物理内存中的总空间
extern uint64_t allocated_total_bytes; // 当前分配的总空间
extern int64_t allocated_bytes;       // 当前分配的内存空间
extern uint64_t next_gc_bytes;         // 下一次 gc 的内存量
extern bool gc_barrier;                // gc 屏障开启标识
extern uint8_t gc_stage;               // gc 阶段
extern mutex_t gc_stage_locker;

typedef enum {
    GC_STAGE_OFF, // 0 表示 gc 关闭, 这也是一个初始状态
    GC_STAGE_START,
    GC_STAGE_MARK,
    GC_STAGE_MARK_DONE, // mark 停止是一个过程
    GC_STAGE_SWEEP,
} gc_stage_t;

// 遍历

// 头进
#define RT_LIST_PUSH_HEAD(list, item) \
    do {                              \
        if (list == NULL) {           \
            list = item;              \
            break;                    \
        }                             \
                                      \
        item->next = list;            \
        list = item;                  \
    } while (0)

// 头出
#define RT_LIST_POP_HEAD(list, item) \
    do {                             \
        if (list == NULL) {          \
            break;                   \
        }                            \
                                     \
        *item = list;                \
        list = *item->next;          \
        *item->next = NULL;          \
    } while (0)

// radix tree 每一层级的 item 可以管理的 page 的数量
static uint64_t summary_page_count[PAGE_SUMMARY_LEVEL] = {
    CHUNK_BITS_COUNT * 64, CHUNK_BITS_COUNT * 32, CHUNK_BITS_COUNT * 16, CHUNK_BITS_COUNT * 8, CHUNK_BITS_COUNT,
};

static uint64_t summary_index_scale[PAGE_SUMMARY_LEVEL] = {64, 32, 16, 8, 0};

static inline bool gc_barrier_get() {
    return gc_barrier;
}

static inline void gc_barrier_start() {
    gc_barrier = true;
}

static inline void gc_barrier_stop() {
    gc_barrier = false;
}

/**
 * 最后一位如果为 1 表示 no ptr, 0 表示 has ptr
 * @param spanclass
 * @return
 */
static inline bool spanclass_has_ptr(uint8_t spanclass) {
    return (spanclass & 1) == 0;
}

static inline addr_t arena_base(uint64_t arena_index) {
    return arena_index * ARENA_SIZE + ARENA_BASE_OFFSET;
}

static inline uint64_t arena_index(uint64_t base) {
    return (base - ARENA_BASE_OFFSET) / ARENA_SIZE;
}

static inline bool in_heap(addr_t addr) {
    return addr >= ARENA_HINT_BASE && addr < memory->mheap->current_arena.end;
}

/**
 * 根据内存地址找到响应的 arena
 * @param addr
 * @return
 */
static inline arena_t *take_arena(addr_t addr) {
    if (!in_heap(addr)) {
        return NULL;
    }

    uint64_t index = arena_index(addr);
    arena_t *arena = memory->mheap->arenas[index];
    return arena;
}

static inline uint64_t arena_bits_index(arena_t *arena, addr_t addr) {
    uint64_t ptr_count = (addr - arena->base) / POINTER_SIZE;
    uint64_t bit_index = (ptr_count / 4) * 8 + (ptr_count % 4);
    return bit_index;
}

static inline addr_t safe_heap_addr(addr_t addr) {
    assert(addr >= ARENA_HINT_BASE && "addr overflow heap base");
    assert(addr < memory->mheap->current_arena.end && "addr overflow heap end");
    return addr;
}

static inline addr_t fetch_heap_addr(addr_t addr) {
    addr_t result = fetch_addr_value(addr);
    return safe_heap_addr(result);
}

static inline uint64_t fetch_int_value(addr_t addr, uint64_t size) {
    if (size == QWORD) {
        return *(uint64_t *)addr;
    }
    if (size == DWORD) {
        return *(uint32_t *)addr;
    }
    if (size == WORD) {
        return *(uint16_t *)addr;
    }
    if (size == BYTE) {
        return *(uint8_t *)addr;
    }
    assert(false && "cannot fetch int value");
    exit(1);
}

void memory_init();

/**
 * 基于 mmap
 * @param hint_addr
 * @return
 */
void *mheap_sys_alloc(mheap_t *mheap, uint64_t *size);

uint64_t arena_index(addr_t base);

addr_t arena_base(uint64_t arena_index);

mspan_t *span_of(addr_t addr);

bool spanclass_has_ptr(uint8_t spanclass);

void uncache_span(mcentral_t *mcentral, mspan_t *span);

void mheap_free_span(mheap_t *mheap, mspan_t *span);

addr_t mstack_new(uint64_t size);

void rtypes_deserialize();

void fndefs_deserialize();

void symdefs_deserialize();

rtype_t *rt_find_rtype(uint32_t rtype_hash);

uint64_t rt_rtype_out_size(uint32_t rtype_hash);

fndef_t *find_fn(addr_t addr);

/**
 * 强制 gc
 * @return
 */
void runtime_gc();

void runtime_eval_gc();

void runtime_force_gc();

void *gc_malloc(uint64_t reflect_hash);

/**
 * 不会进行 gc
 * @param size
 * @param rtype
 * @return
 */
void *rt_gc_malloc(uint64_t size, rtype_t *rtype);

uint64_t runtime_malloc_bytes();

mspan_t *mspan_new(addr_t base, uint64_t pages_count, uint8_t spanclass);

arena_hint_t *arena_hints_init();

void shade_obj_grey(void *obj);

void rt_shade_obj_with_barrier(void *obj);

void mark_ptr_black(void *value);

void mheap_grow(uint64_t pages_count);
addr_t page_alloc_find(uint64_t pages_count);

#endif // NATURE_MEMORY_H

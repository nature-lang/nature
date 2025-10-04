#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>

#include "aco/aco.h"
#include "rtype.h"
#include "runtime.h"
#include "sizeclass.h"
#include "utils/custom_links.h"
#include "utils/helper.h"

extern memory_t *memory;
extern uint64_t remove_total_bytes; // 当前回收到物理内存中的总空间
extern uint64_t allocated_total_bytes; // 当前分配的总空间
extern atomic_size_t allocated_bytes; // 当前分配的内存空间
extern uint64_t next_gc_bytes; // 下一次 gc 的内存量
extern bool gc_barrier; // gc 屏障开启标识
extern struct sc_map_sv const_str_pool;

extern uint8_t gc_stage; // gc 阶段
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

// radix tree 每一层级的 item 可以管理的 page 的数量, 用于判断当前 page 是否满载
static uint64_t summary_page_count[PAGE_SUMMARY_LEVEL] = {
        L0_MAX_PAGES,
        L1_MAX_PAGES,
        L2_MAX_PAGES,
        L3_MAX_PAGES,
        L4_MAX_PAGES,
};

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
    // 直接内联 in_heap 的逻辑，减少函数调用开销
    if (addr < ARENA_HINT_BASE || addr >= memory->mheap->current_arena.end) {
        return NULL;
    }

    uint64_t index = arena_index(addr);
    // 添加预取指令，提高缓存命中率
    __builtin_prefetch(&memory->mheap->arenas[index], 0, 3);
    arena_t *arena = memory->mheap->arenas[index];
    return arena;
}

static inline uint64_t arena_bits_index(arena_t *arena, addr_t addr) {
    // 最优化版本：完全使用位运算
    uint64_t ptr_count = (addr - arena->base) >> 3; // 假设POINTER_SIZE=8
    uint64_t result = ((ptr_count & ~3ULL) << 1) + (ptr_count & 3ULL);

    // Optimized: use bit shifts for maximum performance
    // Maps 4 consecutive pointers to 8 bits (2 bits per pointer)
    //    uint64_t _ptr_count = (addr - arena->base) / POINTER_SIZE;
    //    uint64_t bit_index = (_ptr_count / 4) * 8 + (_ptr_count % 4);
    //    return bit_index;
    //    assert(bit_index == result);
    return result;
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
        return *(uint64_t *) addr;
    }
    if (size == DWORD) {
        return *(uint32_t *) addr;
    }
    if (size == WORD) {
        return *(uint16_t *) addr;
    }
    if (size == BYTE) {
        return *(uint8_t *) addr;
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


static inline void ndata_deserialize() {
    rt_data_ptr = &rt_data;
}

static inline void nstrtable_deserialize() {
    rt_strtable_ptr = &rt_strtable_data;
}

void rtypes_deserialize();

void fndefs_deserialize();

void callers_deserialize();

void symdefs_deserialize();

void register_const_str_pool();

static inline uint64_t rt_rtype_stack_size(int64_t rhash) {
    assert(rhash > 0 && "rhash empty");

    rtype_t *rtype = rt_find_rtype(rhash);

    assert(rtype && "cannot find rtype by hash");
    return rtype->stack_size;
}

static inline uint8_t take_sizeclass(uint8_t spanclass) {
    return spanclass >> 1;
}

fndef_t *find_fn(addr_t addr, n_processor_t *p);

/**
 * 强制 gc
 * @return
 */
void runtime_gc();

void runtime_eval_gc();

void runtime_force_gc();

void *gc_malloc(uint64_t rhash);

void *gc_malloc_size(uint64_t size);

uint64_t runtime_malloc_bytes();

mspan_t *mspan_new(addr_t base, uint64_t pages_count, uint8_t spanclass);

arena_hint_t *arena_hints_init();

void shade_obj_grey(void *obj);

void rt_shade_obj_with_barrier(void *obj);

void mark_ptr_black(void *value);

void mheap_grow(uint64_t pages_count);

uint64_t page_alloc_find(uint64_t pages_count, bool must_find);

#endif // NATURE_MEMORY_H

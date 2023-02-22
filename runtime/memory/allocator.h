#ifndef NATURE_ALLOCATOR_H
#define NATURE_ALLOCATOR_H

#include "utils/slice.h"
#include "utils/list.h"
#include "utils/helper.h"
#include "utils/bitmap.h"
#include "utils/typedef.h"

/**
 * sizeclass 一共有 68 种(其中 sizeclass = 0 用于大内存对象分配，在定义中的一共是 1 - 67 种)
 * spanclass 相比于 sizeclass 多了 span 中的 obj 是否包含指针
 */
#define SIZECLASS_COUNT 68

#define ARENA_COUNT 4194304 // 64 位 linux 按照每 64MB 内存进行拆分，一共可以拆分这个多个 arena

#define PAGE_SIZE 8192 // 单位 byte

#define PAGE_MASK (PAGE_SIZE - 1) // 0b1111111111111

#define  ARENA_PAGES_COUNT 8192 // 64M / 8K = 8192 个page

#define ARENA_BITS_COUNT 2097152 // 1byte 可以索引 4个指针*8byte = 32byte 空间, 64MB 空间需要 64*1024*1024 / 32

#define PAGE_ALLOC_CHUNK_L1 8192
#define PAGE_ALLOC_CHUNK_L2 8192

#define PAGE_SUMMARY_LEVEL 5 // 5 层 radix tree

/**
 * 最后一位表示 sizeclass 是否包含指针 是 0 表示 scan 类型， 1 表示 noscan 类型
 */
#define SPANCLASS_COUNT 136

typedef struct mspan_t {
    struct mspan_t *next; // mspan 是双向链表
    struct mspan_t *prev;

    uint32_t sweepgen;
    void *base; // mspan 的内存起始地址
    uint8_t spanclass; // spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint pages_count; // page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages 是不固定的
    uint obj_count; // mspan 中 obj 的数量，也可以通过 sizeclass 直接确定,如果是分配大内存时，其固定为 1， 也就是一个 obj 占用一个 span

    // bitmap 结构, alloc_bits 标记 obj 是否被使用， 1 表示使用，0表示空闲
    bitmap_t *alloc_bits;
    bitmap_t *gcmark_bits; // gc 阶段标记，1 表示被使用(三色标记中的黑色),0表示空闲(三色标记中的白色)
} mspan_t;

/**
 * m_cache 是线程的缓存，大部分情况的内存申请都是通过 mcache 来进行的
 * 物理机有几个线程就注册几个 mcache
 */
typedef struct {
    mspan_t *alloc[SPANCLASS_COUNT]; // 136 种 mspan,每种类型的 span 只会持有一个
    uint32_t flush_gen; // sweepgen 缓存，避免重复进行 mache flush
} mcache_t;

typedef struct {
    uint8_t spanclass;

    list *partial_swept; // swept 表示是否被垃圾回收清扫
    list *partial_unswept;

    list *full_swept;
    list *full_unswept; // full 表示已经没有空闲的 sapn 了
} mcentral_t;

typedef struct {
    uint64_t blocks[8];
} page_chunk_t; // page_chunk 现在占用 512bit


typedef uint64_t page_summary_t; // page alloc chunk 的摘要数据，组成 [start,max,end]
/**
 * 由于一个 chunk 是 512bit，能表示 4MB 的空间
 * 48 位可用内存空间是 256T, 则需要 4GB 的 chunk 空间。
 * 如果直接初始话一个 4GB 内存空间的数组，这无疑是非常浪费的。
 * 数组元素的大小是 512bit, 所以如果是一维数组平铺需要 67108864 个元素
 * 分成二维数组则是 一维和二维都是 2^13 = 8192 个元素
 * page_alloc 是一个自增数据，所以数组的第二维度没有初始化时就是一个空指针数据
 */
typedef struct {
    // 最底层 level 的数量等于当前 chunk 的数量
    // 再往上每一级的数量等于下一级数量 / 8
    slice_t *summary[PAGE_SUMMARY_LEVEL];

    uint32_t chunk_count;
    // 核心位图，标记自启动以来所有 page 的使用情况
    page_chunk_t *chunks[PAGE_ALLOC_CHUNK_L1]; // 通过 chunks = {0} 初始化，可以确保第二维度为 null
} page_alloc_t;

// arena meta

typedef struct {
    // heapArena.bitmap? bitmap 用一个字节(8bit)标记 arena 中4个指针大小(8byte)的内存空间。
    // 8bit 低四位用于标记这四个内存空间的类型(0: 指针， 1: 标量)。这是 gc 遍历所有对象的关键
    // 高四位用于标记这四个内存空间是否需要被 gc 扫描？ (0: 终止，1: 扫描)
    uint8_t bits[ARENA_BITS_COUNT];

    // 可以通过 page_index 快速定位到 span, 每一个 pages 都会在这里有一个数据
    mspan_t *spans[ARENA_PAGES_COUNT];
} heap_arena;

typedef struct {
    page_alloc_t pages;
    heap_arena *arenas[ARENA_COUNT];
    mcentral_t centrals[SPANCLASS_COUNT];

    uint32_t sweepgen;
    slice_t *spans; // 所有分配的 span 都会在这里被引用

    struct {
        void *base;
        void *cursor;
    } current_arena;
} mheap_t;

void *runtime_malloc(uint size, typedef_t *type);

// TODO 根据 size 计算出 67 个类中的一个
static uint8_t calc_sizeclass(uint8_t size);


// 7位sizeclass + 1位是否包含指针
static uint8_t make_spanclass(uint8_t sizeclass, uint8_t has_ptr);

// 从 spanclass 对应的 span 中找到一个 free 的 obj 并返回
static void *mcache_alloc(uint8_t spanclass);

static mspan_t *mheap_alloc(uint pages_count, uint8_t spanclass);

static uint get_span_size(uint8_t sizeclass);

// 设置 meta heap_arena 的 bits
static void heap_arena_bits_set(void *addr, uint size, uint span_size, typedef_t *type);

static void mcentral_full_swept_push(uint8_t spanclass, mspan_t *span);

// 单位
static void *std_malloc(uint size, typedef_t *type) {
    bool has_ptr = type != NULL || type->last_ptr_count;
    uint8_t sizeclass = calc_sizeclass(size);
    uint8_t spanclass = make_spanclass(sizeclass, has_ptr);

    uint span_size = get_span_size(sizeclass);

    void *addr = mcache_alloc(spanclass);
    if (has_ptr) {
        // 对 heap arena bits 做标记
        heap_arena_bits_set(addr, size, span_size, type);
    }

    return addr;
}

struct statvoid *large_malloc(uint size, typedef_t *type) {
    bool has_ptr = type != NULL || type->last_ptr_count;
    uint8_t spanclass = make_spanclass(0, has_ptr);

    // 计算需要分配的 page count(向上取整)
    uint pages_count = size / PAGE_SIZE;
    if ((size & PAGE_MASK) != 0) {
        pages_count += 1;
    }

    // 直接从堆中分配 span
    mspan_t *s = mheap_alloc(pages_count, spanclass);
    assertf(s != NULL, "out of memory");

    // 将 span 推送到 full swept 中，这样才能进行垃圾回收
    mcentral_full_swept_push(spanclass, s);
    s->obj_count = 1;
    return s->base;
}

#endif //NATURE_ALLOCTOR_H

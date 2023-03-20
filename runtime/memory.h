#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include <stdlib.h>
#include <stdint.h>

#include "utils/links.h"
#include "utils/value.h"
#include "utils/slice.h"
#include "utils/linked.h"
#include "utils/bitmap.h"
#include "sizeclass.h"

#define ARENA_SIZE 67108864 // arena 的大小，单位 byte

#define ARENA_COUNT 4194304 // 64 位 linux 按照每 64MB 内存进行拆分，一共可以拆分这个多个 arena

#define PAGE_SIZE 8192 // 单位 byte

#define PAGE_MASK (PAGE_SIZE - 1) // 0b1111111111111

#define MSTACK_SIZE (8 * 1024 * 1024) // 由于目前还没有栈扩容机制，所以初始化栈可以大一点

#define  ARENA_PAGES_COUNT 8192 // 64M / 8K = 8192 个page

#define ARENA_BITS_COUNT 2097152 // 1byte = 8bit 可以索引 4*8byte = 32byte 空间, 64MB 空间需要 64*1024*1024 / 32

#define PAGE_ALLOC_CHUNK_SPLIT 8192 // 每组 chunks 中的元素的数量

#define ARENA_HINT_BASE  824633720832 // 0x00c0 << 32 // 单位字节，表示虚拟地址 offset addr = 0.75T
#define ARENA_HINT_SIZE  1099511627776 // 1 << 40
#define ARENA_HINT_COUNT 128 // 0.75T ~ 128T

#define ARENA_BASE_OFFSET ARENA_HINT_BASE

#define CHUNK_BITS_COUNT 512 // 单位 bit, 一个 chunk 的大小是 512bit


/**
 * 最后一位表示 sizeclass 是否包含指针 是 0 表示 scan 类型， 1 表示 noscan 类型
 */
#define SPANCLASS_COUNT 136

#define STD_MALLOC_LIMIT (32 * 1024) // 32Kb

#define PAGE_SUMMARY_LEVEL 5 // 5 层 radix tree
#define PAGE_SUMMARY_MERGE_COUNT 8 // 每个上级 summary 索引的数量
#define PAGE_SUMMARY_COUNT_L5  (128*1024*1024 / 4) // 一个 chunk 表示 4M 空间, 所以 l5 一共有 33554432 个 chunk(128T空间)
#define PAGE_SUMMARY_COUNT_L4  (PAGE_SUMMARY_COUNT_L5 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L3  (PAGE_SUMMARY_COUNT_L4 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L2  (PAGE_SUMMARY_COUNT_L3 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L1  (PAGE_SUMMARY_COUNT_L2 / PAGE_SUMMARY_MERGE_COUNT)

// radix tree 每一层级的 item 可以管理的 page 的数量
static uint64_t summary_page_count[PAGE_SUMMARY_LEVEL] = {
        CHUNK_BITS_COUNT * 64,
        CHUNK_BITS_COUNT * 32,
        CHUNK_BITS_COUNT * 16,
        CHUNK_BITS_COUNT * 8,
        CHUNK_BITS_COUNT,
};


static uint summary_index_scale[PAGE_SUMMARY_LEVEL] = {64, 32, 16, 8, 0};

/**
 * 参考 linux, 栈从上往下增长，所以在数学意义上 base > end
 */
typedef struct {
    addr_t base; // 虚拟起始地址
    addr_t end; // 栈结束地址
    uint64_t size; // 栈空间
    addr_t frame_base; // BP register value，指向 local values 和 previous rbp value 之间, *ptr 取的值是 (ptr ~ ptr+8)
    addr_t top; // SP register
} mstack_t;

typedef struct mspan_t {
    struct mspan_t *next; // mspan 是双向链表
    struct mspan_t *prev;

    uint32_t sweepgen; // 目前暂时是单线程模式，所以不需要并发垃圾回收
    addr_t base; // mspan 在 arena 中的起始位置
    addr_t end;
    uint8_t spanclass; // spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint pages_count; // page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages 是不固定的
    uint obj_count; // mspan 中 obj 的数量，也可以通过 sizeclass 直接确定,如果是分配大内存时，其固定为 1， 也就是一个 obj 占用一个 span
    uint obj_size; // obj_count * obj_size 不一定等于 pages_count * page_size, 虽然可以通过 sizeclass 获取，但是不兼容大对象
    uint alloc_count; // 已经用掉的 obj count

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

    linked_t *partial_swept; // swept 表示是否被垃圾回收清扫
    linked_t *full_swept;

//    list *partial_unswept; // TODO 开发多线程模式时再做支持
//    list *full_unswept; // full 表示已经没有空闲的 sapn 了
} mcentral_t;

typedef struct {
    uint64_t blocks[8];
} page_chunk_t; // page_chunk 现在占用 64 * 8 = 512bit


typedef struct {
    uint16_t start;
    uint16_t end;
    uint32_t max;
} page_summary_t; // page alloc chunk 的摘要数据，组成 [start,max,end]

/**
 * chunk 1bit 对应一个 page 是否被使用
 *
 * 由于一个 chunk 是 512bit * page size(8kb)，能表示 4MB 的空间
 * 48 位可用内存空间是 256T(34359738368 page),  则需要 4GB 的 chunk 空间。
 * 如果直接初始话一个 4GB 内存空间的数组，这无疑是非常浪费的。
 * 数组元素的大小是 512bit, 所以如果是一维数组平铺需要 67108864 个 chunk 元素
 * 分成二维数组则是 一维和二维都是 2^13 = 8192 个元素
 * page_alloc_find 是一个自增数据，所以数组的第二维度没有初始化时就是一个空指针数据
 * level = 5 时一个有 67108864 个 chunk * 8byte = 512M, 5 级结构也需要 600M 的空间(但这是没有映射的虚拟内存空间)
 * 这里的数据都是原数据，所以在初始化时就已经注册完成了
 */
typedef struct {
    // 最底层 level 的数量等于当前 chunk 的数量 64487424 * 64bit =
    // 再往上每一级的数量等于下一级数量的 1/8
    // 由于 arena 目前使用 128T, 所以 l5 chunk 的数量暂定为
    // l5 = 33554432 个chunk
    // l4 = 4194304
    // l3 = 524288
    // l2 = 65536
    // l1 = 8192
    page_summary_t *summary[PAGE_SUMMARY_LEVEL];

    uint64_t chunk_count;

    // 核心位图，标记自启动以来所有 page 的使用情况
    // 通过 chunks = {0} 初始化，可以确保第二维度为 null
    page_chunk_t *chunks[PAGE_ALLOC_CHUNK_SPLIT];
} page_alloc_t;

// arena meta

typedef struct {
    // heapArena.bitmap? bitmap 用一个字节(8bit)标记 arena 中4个指针大小(8byte)的内存空间。
    // 8bit 低四位用于标记这四个内存空间的类型(0: 标量 scalar， 1: 指针 pointer)。这是 gc 遍历所有对象的关键
    // 高四位用于标记这四个内存空间是否需要被 gc 扫描？ (0: dead，1: scan)
    // 高四位标记了 4 个指针，如果其是 1 表示其后面还有指针需要扫描，0 表示 no more pointers in this object
    // 当 obj 是一个大对象时这很有效
    uint8_t bits[ARENA_BITS_COUNT];

    // 可以通过 page_index 快速定位到 span, 每一个 pages 都会在这里有一个数据
    mspan_t *spans[ARENA_PAGES_COUNT]; // page = 8k, 所以 pages 的数量是固定的

    addr_t base;
} arena_t;

/**
 * 从 0.75T 开始 hint,知道 126.75T
 */
typedef struct arena_hint_t {
    addr_t addr; // mmap hint addr
    bool last;
    struct arena_hint_t *next;
} arena_hint_t;

/**
 * heap_arena 和 pages 是管理虚拟内存的两个视角
 */
typedef struct {
    // 全局只有这一个 page alloc，所以所有通过 arena 划分出来的 page 都将被该 page alloc 结构管理
    page_alloc_t page_alloc;
    // arenas 在空间上是不连续的，尤其是前面部分都是 null, 为了能够快速遍历，需要一个可遍历的空间
    slice_t *arena_indexes; // arena index 列表
    arena_t *arenas[ARENA_COUNT];
    mcentral_t centrals[SPANCLASS_COUNT];
    uint32_t sweepgen;
    slice_t *spans; // 所有分配的 span 都会在这里被引用
    arena_hint_t *arena_hints;
    // cursor ~ end 可能会跨越多个连续的 arena
    struct {
        addr_t cursor; // 指向未被使用的地址
        addr_t end; // 指向本次申请的终点
    } current_arena;
} mheap_t;


/**
 * linux 线程由自己的系统栈，多个线程就有多个 system stack
 * 由于进入到 runtime 的大多数情况中都需要切换栈区，所以必须知道当前线程的栈区
 * 如果 processor 是第一个线程，那么其 system_stack 和 linux main stack 相同,
 * copy linux stack 时并不需要精准的范围，base 地址直接使用当前 rsp, end 地址使用自定义大小比如 64kb 就行了
 * 关键是切换到 user stack 时记录下 rsp 和 rbp pointer
 */
typedef struct processor_t {
    mstack_t user_stack;
    mstack_t system_stack;
    mcache_t mcache;
} processor_t;

typedef struct {
    mheap_t *mheap;
    linked_t *grey_list;
    uint32_t sweepgen; // collector 中的 grep list 每一次使用前都需要清空
} memory_t;

memory_t *memory;

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

void uncache_span(mcentral_t mcentral, mspan_t *span);

void mheap_free_span(mheap_t *mheap, mspan_t *span);

addr_t mstack_new(uint64_t size);

void rtypes_deserialize();

void fndefs_deserialize();

rtype_t *rt_find_rtype(uint64_t index);

#endif //NATURE_MEMORY_H

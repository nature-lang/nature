#ifndef NATURE_ALLOCATOR_H
#define NATURE_ALLOCATOR_H

#include "utils/slice.h"
#include "utils/helper.h"
#include "utils/bitmap.h"

/**
 * sizeclass 一共有 68 种(其中 sizeclass = 0 用于大内存对象分配，在定义中的一共是 1 - 67 种)
 * spanclass 相比于 sizeclass 多了 span 中的 obj 是否包含指针
 */
#define SIZECLASS_COUNT 68


/**
 * 最后一位表示 sizeclass 是否包含指针 是 0 表示 scan 类型， 1 表示 noscan 类型
 */
#define SPANCLASS_COUNT 136

typedef struct mspan_t {
    struct mspan_t *next; // mspan 是双向链表
    struct mspan_t *prev;

    void *start_addr; // mspan 的内存起始地址
    uint8_t spanclass; // spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint pages_count; // page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages 是不固定的
    uint obj_count; // mspan 中 obj 的数量，也可以通过 sizeclass 直接确定

    // bitmap 结构
    bitmap_t *alloc_bits; // 单位大小是 uint8
    bitmap_t *gcmark_bits
} mspan_t;

/**
 * m_cache 是线程的缓存，大部分情况的内存申请都是通过 mcache 来进行的
 * 物理机有几个线程就注册几个 mcache
 */
typedef struct {
    mspan_t *alloc[SPANCLASS_COUNT]; // 136 种 mspan
} mcache_t;

typedef struct {

} mcentral_t;


typedef struct {

} page_alloc_t;

typedef struct {

} mheap_t;

#endif //NATURE_ALLOCTOR_H

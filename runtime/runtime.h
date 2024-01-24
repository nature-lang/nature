#ifndef NATURE_RUNTIME_RUNTIME_H
#define NATURE_RUNTIME_RUNTIME_H

#include <pthread.h>
#include <uv.h>

#include "aco/aco.h"
#include "utils/bitmap.h"
#include "utils/linked.h"
#include "utils/mutex.h"
#include "utils/type.h"

#define GC_WORKLIST_LIMIT 1024 // 每处理 1024 个 ptr 就 yield

#define ARENA_SIZE 67108864 // arena 的大小，单位 byte

#define ARENA_COUNT 4194304 // 64 位 linux 按照每 64MB 内存进行拆分，一共可以拆分这个多个 arena

#define ALLOC_PAGE_SIZE 8192 // 单位 byte

#define PAGE_MASK (ALLOC_PAGE_SIZE - 1) // 0b1111111111111

#define MSTACK_SIZE (8 * 1024 * 1024) // 8M 由于目前还没有栈扩容机制，所以初始化栈可以大一点

#define ARENA_PAGES_COUNT 8192 // 64M / 8K = 8192 个 page

#define ARENA_BITS_COUNT 2097152 // 1byte = 8bit 可以索引 4*8byte = 32byte 空间, 64MB 空间需要 64*1024*1024 / 32

#define PAGE_ALLOC_CHUNK_SPLIT 8192 // 每组 chunks 中的元素的数量

#define ARENA_HINT_BASE 824633720832  // 0x00c0 << 32 // 单位字节，表示虚拟地址 offset addr = 0.75T
#define ARENA_HINT_SIZE 1099511627776 // 1 << 40
#define ARENA_HINT_COUNT 128          // 0.75T ~ 128T

#define ARENA_BASE_OFFSET ARENA_HINT_BASE

#define CHUNK_BITS_COUNT 512 // 单位 bit, 一个 chunk 的大小是 512bit

/**
 * 最后一位表示 sizeclass 是否包含指针 是 0 表示 scan 类型， 1 表示 noscan 类型
 */
#define SPANCLASS_COUNT 136

#define STD_MALLOC_LIMIT (32 * 1024) // 32Kb

#define PAGE_SUMMARY_LEVEL 5                          // 5 层 radix tree
#define PAGE_SUMMARY_MERGE_COUNT 8                    // 每个上级 summary 索引的数量
#define PAGE_SUMMARY_COUNT_L5 (128 * 1024 * 1024 / 4) // 一个 chunk 表示 4M 空间, 所以 l5 一共有 33554432 个 chunk(128T空间)
#define PAGE_SUMMARY_COUNT_L4 (PAGE_SUMMARY_COUNT_L5 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L3 (PAGE_SUMMARY_COUNT_L4 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L2 (PAGE_SUMMARY_COUNT_L3 / PAGE_SUMMARY_MERGE_COUNT)
#define PAGE_SUMMARY_COUNT_L1 (PAGE_SUMMARY_COUNT_L2 / PAGE_SUMMARY_MERGE_COUNT)

#define DEFAULT_NEXT_GC_BYTES (100 * 1024) // 100KB
#define NEXT_GC_FACTOR 2

#define WAIT_SHORT_TIME 10 // ms
#define WAIT_MID_TIME 50   // ms
#define WAIT_LONG_TIME 100 // ms

#define SAFE_NEW(type) safe_mallocz(sizeof(type))

#define MANUAL_NEW(type) manual_malloc(sizeof(type))

#define PREEMPT_LOCK()                         \
    processor_t *_p = processor_get();         \
    if (_p) {                                  \
        mutex_lock(_p->thread_preempt_locker); \
    }

#define PREEMPT_UNLOCK()                         \
    if (_p) {                                    \
        mutex_unlock(_p->thread_preempt_locker); \
    }

#ifdef DEBUG
#define SAFE_DEBUGF(format, ...)                                                                                                        \
    do {                                                                                                                                \
        PREEMPT_LOCK();                                                                                                                 \
        fprintf(stderr, "[%lu] USER_CO DEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t)uv_thread_self(), ##__VA_ARGS__); \
        fflush(stderr);                                                                                                                 \
        PREEMPT_UNLOCK();                                                                                                               \
    } while (0)

#endif

typedef void (*void_fn_t)(void);

/**
 * 参考 linux, 栈从上往下增长，所以在数学意义上 base > end
 */
typedef struct {
    addr_t stack_base;   // 虚拟起始地址(按照内存申请的节奏来，这里依旧是低地址位置)
    uint64_t stack_size; // 栈空间
    ucontext_t ctx;
} mmode_t;

typedef struct mspan_t {
    struct mspan_t *next; // mspan 是双向链表
                          //    struct mspan_t *prev;

    uint32_t sweepgen; // 目前暂时是单线程模式，所以不需要并发垃圾回收
    addr_t base;       // mspan 在 arena 中的起始位置
    addr_t end;
    uint8_t spanclass; // spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint64_t pages_count; // page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages
    // 是不固定的
    uint64_t obj_count; // mspan 中 obj 的数量，也可以通过 sizeclass 直接确定,如果是分配大内存时，其固定为 1，
    // 也就是一个 obj 占用一个 span
    uint64_t obj_size; // obj_count * obj_size 不一定等于 pages_count * page_size, 虽然可以通过 sizeclass
    // 获取，但是不兼容大对象
    uint64_t alloc_count; // 已经用掉的 obj count

    // bitmap 结构, alloc_bits 标记 obj 是否被使用， 1 表示使用，0表示空闲
    bitmap_t *alloc_bits;
    bitmap_t *gcmark_bits; // gc 阶段标记，1 表示被使用(三色标记中的黑色),0表示空闲(三色标记中的白色)

    bitmap_t *manual_bits; // 手动分配的内存区域，gcmark_bits 初始状态, 手动 malloc 和 free 内存时需要同时更新 gcmark/manual

    mutex_t *gcmark_locker;
} mspan_t;

/**
 * m_cache 是线程的缓存，大部分情况的内存申请都是通过 mcache 来进行的
 * 物理机有几个线程就注册几个 mcache
 */
typedef struct {
    mspan_t *alloc[SPANCLASS_COUNT]; // 136 种 mspan,每种类型的 span 只会持有一个
    uint32_t flush_gen;              // sweepgen 缓存，避免重复进行 mache flush
} mcache_t;

typedef struct {
    uint8_t spanclass;

    mspan_t *partial_list; // 还有空闲 span obj 的链表
    mspan_t *full_list;
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

    //    uint64_t chunk_count;

    // 核心位图，标记自启动以来所有 page 的使用情况
    // 通过 chunks = {0} 初始化，可以确保第二维度为 null
    page_chunk_t *chunks[PAGE_ALLOC_CHUNK_SPLIT];
} page_alloc_t;

// arena meta

typedef struct {
    // heapArena.bitmap? bitmap 用一个字节(8bit)标记 arena 中4个指针大小(8byte)的内存空间。(也就是 2bit 标记一个指针)
    // 8bit 低四位用于标记这四个内存空间的类型(0: 标量 scalar， 1: 指针 pointer)。这是 gc 遍历所有对象的关键
    // 高四位用于标记这四个内存空间是否需要被 gc 扫描？ (0: dead，1: scan)
    // 高四位标记了 4 个指针，如果其是 1 表示其后面还有指针需要扫描，0 表示 no more pointers in this object
    // 当 obj 是一个大对象时这很有效
    uint8_t bits[ARENA_BITS_COUNT];

    // 每个 arena 包含 64M 内存，被划分成 8192个 page, 每个 page 8K
    // 可以通过 page_index 快速定位到 span, 每一个 pages 都会在这里有一个数据
    // 可以是一个 span 存在于多个 page_index 中, 一个 span 的最小内存是 8k, 所以一个 page 最多只能存储一个 span.
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
        addr_t end;    // 指向本次申请的终点
    } current_arena;
} mheap_t;

typedef struct {
    mheap_t *mheap; // 全局 heap, 访问时需要加锁
    mutex_t *locker;
    uint32_t sweepgen; // collector 中的 grep list 每一次使用前都需要清空
} memory_t;

typedef enum {
    CO_STATUS_RUNNABLE = 1, // 允许被调度
    CO_STATUS_RUNNING = 2,  // 正在运行
    CO_STATUS_SYSCALL = 3,  // 陷入系统调用
    CO_STATUS_WAITING = 4,  // 等待 IO 事件就绪
    CO_STATUS_DEAD = 5,     // 死亡状态
} co_status_t;

typedef struct processor_t processor_t;

typedef struct coroutine_t {
    bool main; // 是否是 main 函数
    bool solo; // 当前协程需要独享线程
    co_status_t status;
    aco_t *aco;
    void *fn;       // fn 指向
    processor_t *p; // 当前 coroutine 绑定的 p
    n_vec_t *args;
    void *result; // coroutine 如果存在返回值，相关的值会放在 result 中

    bool is_preempt; // 是否是被抢占出来的，如果是则需要进行特殊调度

    // 当前 coroutine stack 颜色是否为黑色, 黑色说明当前 goroutine stack 已经扫描完毕
    // gc stage 是 mark 时
    bool gc_black;

    bool gc_work; // 是否是用于 gc 的协程

    // 默认为 0， 只有当 coroutine 独占整个线程时才会存在 thread_id
    // 1. solo coroutine 2. coroutine in block syscall 这两种情况会出现 coroutine 独占线程
    uv_thread_t thread_id;
    struct coroutine_t *next;
} coroutine_t;

/**
 * 位于 share_processor_t 中的协程，如果运行时间过长会被抢占式调度
 * 共享处理器的数量通畅等于线程的数量, 所以可以将线程维度的无锁内存分配器放置再这里
 */
struct processor_t {
    int index;
    mcache_t mcache;                // 线程维度无锁内存分配器
    aco_t *main_aco;                // 每个 processor 都会绑定一个 main_aco 用于 aco 的切换操作。
    aco_share_stack_t *share_stack; // processor 中的所有的 stack 都使用该共享栈

    struct sigaction sig;
    uv_loop_t *uv_loop; // uv loop 事件循环

    // 仅仅 solo processor 需要使用该锁，因为 solo processor 需要其他 share 进行 scan root 和 worklist
    mutex_t *gc_locker;

    mutex_t *thread_preempt_locker;

    uv_thread_t thread_id;  // 当前 processor 绑定的 pthread 线程
    coroutine_t *coroutine; // 当前正在调度的 coroutine
    uint64_t co_started_at; // 协程调度开始时间, 单位纳秒，一般从系统启动时间开始计算，而不是 unix 时间戳
    linked_t *co_list;      // 当前 processor 下的 coroutine 列表
    coroutine_t *runnable;  // coroutine 链表
    bool share;             // 默认都是共享处理器
    bool safe_point;        // 当前是否处于安全点
    bool exit;              // 是否已经退出
    bool can_preempt;       // 当前 processor 能否被抢占
    bool gc_work_finished;  // 是否完成了 GC WORK 的工作
    linked_t *gc_worklist;  // gc 扫描的 ptr 节点列表
};

int runtime_main(int argc, char *argv[]);

void rt_processor_attach_errort(char *msg);

void processor_dump_errort(n_errort *errort);

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

coroutine_t *coroutine_get();

void *manual_malloc(uint64_t size);

void manual_free(void *ptr);

void *safe_malloc(size_t size);

void *safe_memmove(void *__dest, const void *__src, size_t __n);

void *safe_realloc(void *ptr, size_t size);

void *safe_mallocz(size_t size);

void safe_free(void *ptr);

static inline void log_lock(bool lock, void *udata) {
    pthread_mutex_t *locker = (pthread_mutex_t *)(udata);
    if (lock) {
        pthread_mutex_lock(locker);
    } else {
        pthread_mutex_unlock(locker);
    }
}

static inline char *safe_str_connect_free(char *a, char *b) {
    PREEMPT_LOCK();

    size_t dst_len = strlen(a);
    size_t src_len = strlen(b);
    char *buf = malloc(dst_len + src_len + 1);
    sprintf(buf, "%s%s", a, b);
    free(a);
    free(b);

    PREEMPT_UNLOCK();
    return buf;
}

static inline char *safe_dsprintf(char *format, ...) {
    PREEMPT_LOCK();

    char *buf = mallocz(1024);
    va_list args;
    va_start(args, format);
    int count = vsprintf(buf, format, args);
    va_end(args);

    void *result = realloc(buf, count + 1);

    PREEMPT_UNLOCK();
    return result;
}

static inline char *safe_utoa(uint64_t n) {
    PREEMPT_LOCK();

    int length = snprintf(NULL, 0, "%lu", n);
    char *str = mallocz(length + 1);
    snprintf(str, length + 1, "%lu", n);

    PREEMPT_UNLOCK();
    return str;
}

static inline char *safe_itoa(int64_t n) {
    PREEMPT_LOCK();

    // 计算长度
    int length = snprintf(NULL, 0, "%ld", n);

    // 初始化 buf
    char *str = malloc(length + 1);

    // 转换
    snprintf(str, length + 1, "%ld", n);

    PREEMPT_UNLOCK();
    return str;
}

// TODO 没有特殊系统调用，原则上不需要 safe
static inline uint32_t safe_hash_string(char *str) {
    //    PREEMPT_LOCK();
    if (str == NULL) {
        return 0;
    }
    uint32_t result = hash_data((uint8_t *)str, strlen(str));
    //    PREEMPT_UNLOCK();
    return result;
}

#endif // NATURE_BASIC_H

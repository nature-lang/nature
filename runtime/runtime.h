#ifndef NATURE_RUNTIME_RUNTIME_H
#define NATURE_RUNTIME_RUNTIME_H

#include <pthread.h>
#include <uv.h>

#include "aco/aco.h"
#include "fixalloc.h"
#include "gcbits.h"
#include "rt_linked.h"
#include "utils/bitmap.h"
#include "utils/linked.h"
#include "utils/mutex.h"
#include "utils/type.h"

#define GC_WORKLIST_LIMIT 1024// 每处理 1024 个 ptr 就 yield

#define ARENA_SIZE 67108864// arena 的大小，单位 byte, 64M

#define ARENA_COUNT 4194304// 64 位 linux 按照每 64MB 内存进行拆分，一共可以拆分这个多个 arena

#define ALLOC_PAGE_SIZE 8192// 单位 byte

#define PAGE_MASK (ALLOC_PAGE_SIZE - 1)// 0b1111111111111

#define MSTACK_SIZE (8 * 1024 * 1024)// 8M 由于目前还没有栈扩容机制，所以初始化栈可以大一点

#define ARENA_PAGES_COUNT 8192// 64M / 8K = 8192 个 page

#define ARENA_BITS_COUNT 2097152// 1byte = 8bit 可以索引 4*8byte = 32byte 空间, 64MB 空间需要 64*1024*1024 / 32

#define PAGE_ALLOC_CHUNK_SPLIT 8192// 每组 chunks 中的元素的数量

#define ARENA_HINT_BASE 824633720832 // 0x00c0 << 32 // 单位字节，表示虚拟地址 offset addr = 0.75T
#define ARENA_HINT_SIZE 1099511627776// 1 << 40
#define ARENA_HINT_COUNT 128         // 0.75T ~ 128T

#define ARENA_BASE_OFFSET ARENA_HINT_BASE

#define CHUNK_BITS_COUNT 512// 单位 bit, 一个 chunk 的大小是 512bit

/**
 * 最后一位表示 sizeclass 是否包含指针 是 0 表示 scan 类型， 1 表示 noscan 类型
 */
#define SPANCLASS_COUNT 136

#define STD_MALLOC_LIMIT (32 * 1024)// 32Kb

#define PAGE_SUMMARY_LEVEL 5                                                    // 5 层 radix tree
#define PAGE_SUMMARY_MERGE_COUNT 8                                              // 每个上级 summary 索引的数量
#define PAGE_SUMMARY_COUNT_L5 (128 * 1024 * 1024 / 4)                           // 一个 chunk 表示 4M 空间, 所以 l5 一共有 33554432 个 chunk(128T空间)
#define PAGE_SUMMARY_COUNT_L4 (PAGE_SUMMARY_COUNT_L5 / PAGE_SUMMARY_MERGE_COUNT)// 4194304, 每个 summary 管理 64M 空间
#define PAGE_SUMMARY_COUNT_L3 (PAGE_SUMMARY_COUNT_L4 / PAGE_SUMMARY_MERGE_COUNT)// 524288, 每个 summary 管理 512M 空间
#define PAGE_SUMMARY_COUNT_L2 (PAGE_SUMMARY_COUNT_L3 / PAGE_SUMMARY_MERGE_COUNT)// 65536, 每个 summary 管理 4G 空间
#define PAGE_SUMMARY_COUNT_L1 (PAGE_SUMMARY_COUNT_L2 / PAGE_SUMMARY_MERGE_COUNT)// 8192, 每个 summary 管理 32G 空间
#define PAGE_SUMMARY_MAX_VALUE 2LL ^ 21                                         // 2097152, max=start=end 的最大值

#define DEFAULT_NEXT_GC_BYTES (100 * 1024)// 100KB
#define NEXT_GC_FACTOR 2

#define WAIT_BRIEF_TIME 1 // ms
#define WAIT_SHORT_TIME 10// ms
#define WAIT_MID_TIME 50  // ms
#define WAIT_LONG_TIME 100// ms

typedef void (*void_fn_t)(void);

/**
 * 参考 linux, 栈从上往下增长，所以在数学意义上 base > end
 */
typedef struct {
    addr_t stack_base;  // 虚拟起始地址(按照内存申请的节奏来，这里依旧是低地址位置)
    uint64_t stack_size;// 栈空间
    ucontext_t ctx;
} mmode_t;

typedef struct mspan_t {
    struct mspan_t *next;// mspan 是双向链表
    // struct mspan_t *prev;

    uint32_t sweepgen;// 目前暂时是单线程模式，所以不需要并发垃圾回收
    addr_t base;      // mspan 在 arena 中的起始位置
    addr_t end;
    uint8_t spanclass;// spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint64_t pages_count;// page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages
    // 是不固定的
    uint64_t obj_count;// mspan 中 obj 的数量，也可以通过 sizeclass 直接确定,如果是分配大内存时，其固定为 1，
    // 也就是一个 obj 占用一个 span
    uint64_t obj_size;// obj_count * obj_size 不一定等于 pages_count * page_size, 虽然可以通过 sizeclass
    // 获取，但是不兼容大对象
    uint64_t alloc_count;// 已经用掉的 obj count

    // bitmap 结构, alloc_bits 标记 obj 是否被使用， 1 表示使用，0表示空闲
    gc_bits *alloc_bits;
    gc_bits *gcmark_bits;// gc 阶段标记，1 表示被使用(三色标记中的黑色),0表示空闲(三色标记中的白色)

    mutex_t alloc_locker;
    mutex_t gcmark_locker;
} mspan_t;

/**
 * m_cache 是线程的缓存，大部分情况的内存申请都是通过 mcache 来进行的
 * 物理机有几个线程就注册几个 mcache
 */
typedef struct {
    mspan_t *alloc[SPANCLASS_COUNT];// 136 种 mspan,每种类型的 span 只会持有一个
    uint32_t flush_gen;             // sweepgen 缓存，避免重复进行 mache flush
} mcache_t;

typedef struct {
    uint8_t spanclass;
    mutex_t locker;

    mspan_t *partial_list;// 还有空闲 span obj 的链表
    mspan_t *full_list;
} mcentral_t;

typedef struct {
    uint64_t blocks[8];
} page_chunk_t;// page_chunk 现在占用 64 * 8 = 512bit

// TODO start/end/max 的正确编码值应该是 uint21_t,后续需要正确实现,能表示的最大值是 2^21=2097152
typedef struct {
    uint16_t start;
    uint16_t end;
    uint16_t max;
    uint8_t full;// start=end=max=2^21 最大值， full 设置为 1
} page_summary_t;// page alloc chunk 的摘要数据，组成 [start,max,end]

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

    // uint64_t chunk_count;

    // 核心位图，标记所有申请的 page 的使用情况, chunk 中每一个 bit 对应一个 page 的使用状态。
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
    mspan_t *spans[ARENA_PAGES_COUNT];// page = 8k, 所以 pages 的数量是固定的

    addr_t base;
} arena_t;

/**
 * 从 0.75T 开始 hint,知道 126.75T
 */
typedef struct arena_hint_t {
    addr_t addr;// mmap hint addr
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
    slice_t *arena_indexes;// arena index 列表
    arena_t *arenas[ARENA_COUNT];
    mcentral_t centrals[SPANCLASS_COUNT];
    uint32_t sweepgen;
    slice_t *spans;// 所有分配的 span 都会在这里被引用
    arena_hint_t *arena_hints;
    // cursor ~ end 可能会跨越多个连续的 arena
    struct {
        addr_t cursor;// 指向未被使用的地址
        addr_t end;   // 指向本次申请的终点
    } current_arena;

    fixalloc_t spanalloc;
} mheap_t;

typedef struct {
    mheap_t *mheap;// 全局 heap, 访问时需要加锁
    mutex_t locker;
    uint32_t sweepgen;
    uint64_t gc_count;// gc 循环次数
} memory_t;

typedef enum {
    CO_STATUS_RUNNABLE = 1,// 允许被调度
    CO_STATUS_RUNNING = 2, // 正在运行
    CO_STATUS_TPLCALL = 3, // 陷入系统调用
    CO_STATUS_RTCALL = 4,  // 陷入系统调用
    CO_STATUS_WAITING = 5, // 等待 IO 事件就绪
    CO_STATUS_DEAD = 6,    // 死亡状态
} co_status_t;

typedef enum {
    P_STATUS_INIT = 0,
    P_STATUS_DISPATCH = 1,
    P_STATUS_TPLCALL = 2,
    P_STATUS_RTCALL = 3,
    P_STATUS_RUNNABLE = 4,
    P_STATUS_RUNNING = 5,
    P_STATUS_PREEMPT = 6,
    P_STATUS_EXIT = 7,
} p_status_t;

typedef struct processor_t processor_t;

// 必须和 nature code 保持一致
typedef struct n_future_t {
    int64_t size;
    void *result;
    void *co;
} n_future_t;

typedef struct coroutine_t {
    int64_t id;
    bool main;// 是否是 main 函数
    bool solo;// 当前协程需要独享线程
    co_status_t status;
    aco_t aco;
    void *fn;      // fn 指向
    processor_t *p;// 当前 coroutine 绑定的 p
    //    n_vec_t *args;
    void *result;// coroutine 如果存在返回值，相关的值会放在 result 中
    n_future_t *future;

    struct coroutine_t *await_co;// 可能为 null, 如果不为 null 说明该 co 在等待当前 co exit
    mutex_t dead_locker;

    // 当前 coroutine stack 颜色是否为黑色, 黑色说明当前 goroutine stack 已经扫描完毕
    // gc stage 是 mark 时, 当 gc_black 值小于 memory->gc_count 时，说明当前 coroutine stack 不是黑色的
    uint64_t gc_black;

    bool gc_work;// 当前 coroutine 是否是 gc coroutine

    uint64_t scan_offset;
    uint64_t scan_ret_addr;

    n_errort *error;

    struct coroutine_t *next;// coroutine list
} coroutine_t;

/**
 * 位于 share_processor_t 中的协程，如果运行时间过长会被抢占式调度
 * 共享处理器的数量通畅等于线程的数量, 所以可以将线程维度的无锁内存分配器放置再这里
 */
struct processor_t {
    int index;
    mcache_t mcache;              // 线程维度无锁内存分配器
    aco_t main_aco;               // 每个 processor 都会绑定一个 main_aco 用于 aco 的切换操作。
    aco_share_stack_t share_stack;// processor 中的所有的 stack 都使用该共享栈

    struct sigaction sig;
    uv_timer_t timer; // 辅助协程调度的定时器
    uv_loop_t uv_loop;// uv loop 事件循环

    // - 仅仅 solo processor 需要该锁， share 进行协作时需要上锁，避免在此期间进行任何内存操作
    mutex_t gc_stw_locker;// solo processor 辅助判断
    uint64_t need_stw;    // 外部声明, 内部判断 是否需要 stw
    uint64_t safe_point;  // 内部声明, 外部判断是否已经 stw

    // 当前 p 需要被其他线程读取的一些属性都通过该锁进行保护
    // - 如更新 p 对应的 co 的状态等
    mutex_t thread_locker;
    p_status_t status;

    uv_thread_t thread_id; // 当前 processor 绑定的 pthread 线程
    coroutine_t *coroutine;// 当前正在调度的 coroutine
    uint64_t co_started_at;// 协程调度开始时间, 单位纳秒，一般从系统启动时间开始计算，而不是 unix 时间戳

    rt_linked_fixalloc_t co_list;// 当前 processor 下的 coroutine 列表
    rt_linked_fixalloc_t runnable_list;

    bool share;// 默认都是共享处理器

    rt_linked_fixalloc_t gc_worklist;  // gc 扫描的 ptr 节点列表
    uint64_t gc_work_finished;// 当前处理的 GC 轮次，每完成一轮 + 1

    struct processor_t *next;// processor 链表支持
};

int runtime_main(int argc, char *argv[]);

int test_runtime_main(void *main_fn);

void rt_coroutine_set_error(char *msg);

void coroutine_dump_error(coroutine_t *co, n_errort *error);

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

coroutine_t *coroutine_get();

void *rt_clr_malloc(uint64_t size, rtype_t *rtype);

void processor_set_status(processor_t *p, p_status_t status);

#ifdef __x86_64__
#define BP_VALUE()      \
    uint64_t rbp_value; \
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp_value));
#elif
#define BP_VALUE()      \
    uint64_t rbp_value; \
    assert(false && "not support");
#endif

#define PRE_RTCALL_HOOK(target)                                                                                \
    do {                                                                                                       \
        processor_t *p = processor_get();                                                                      \
        if (!p) {                                                                                              \
            break;                                                                                             \
        }                                                                                                      \
        if (p->status == P_STATUS_RTCALL) {                                                                    \
            break;                                                                                             \
        }                                                                                                      \
        processor_set_status(p, P_STATUS_RTCALL);                                                              \
        DEBUGF("[pre_rtcall_hook] target %s, status set rtcall success, non-preemption", __FUNCTION__);        \
        BP_VALUE();                                                                                            \
        coroutine_t *_co = coroutine_get();                                                                    \
        assert(_co);                                                                                           \
        _co->scan_ret_addr = fetch_addr_value(rbp_value + POINTER_SIZE);                                       \
        _co->scan_offset = (uint64_t) p->share_stack.align_retptr - (rbp_value + POINTER_SIZE + POINTER_SIZE); \
    } while (0);

void pre_tplcall_hook(char *target);

void post_tplcall_hook(char *target);

void post_rtcall_hook(char *target);

#endif// NATURE_BASIC_H

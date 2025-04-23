#ifndef NATURE_RUNTIME_RUNTIME_H
#define NATURE_RUNTIME_RUNTIME_H

#include <include/uv.h>
#include <pthread.h>

#include "aco/aco.h"
#include "fixalloc.h"
#include "gcbits.h"
#include "nutils/nutils.h"
#include "rt_linked.h"
#include "sizeclass.h"
#include "utils/bitmap.h"
#include "utils/custom_links.h"
#include "utils/linked.h"
#include "utils/mutex.h"
#include "utils/type.h"


#ifdef __LINUX
#define ATOMIC
#else
#define ATOMIC _Atomic
#endif

/**
 * crt1.o _start -> main  -> entry
 */
#ifndef HAS_TEST_MAIN
#ifdef __DARWIN
int runtime_main(int argc, char *argv[]) __asm("_main");
#else
int runtime_main(int argc, char *argv[]) __asm("main");
#endif
#endif

//#ifdef __AMD64
//#define CO_SCAN_REQUIRE(_co)                                                                                                                                                           \
//    {                                                                                                                                                                                  \
//        uint64_t rbp_value;                                                                                                                                                            \
//        __asm__ volatile("mov %%rbp, %0" : "=r"(rbp_value));                                                                                                                           \
//        _co->scan_ret_addr = fetch_addr_value(rbp_value + POINTER_SIZE);                                                                                                               \
//        _co->scan_offset = (uint64_t) p->share_stack.align_retptr - (rbp_value + POINTER_SIZE + POINTER_SIZE);                                                                         \
//        TRACEF("[pre_tplcall_hook] co=%p, status=%d, bp_value=%p, scan_offset=%lu, ret_addr=%p", _co, _co->status, (void *) rbp_value, _co->scan_offset, (void *) _co->scan_ret_addr); \
//    };
//#elif __ARM64
//#define CO_SCAN_REQUIRE(_co)                                                                                                                                                                                                                                                                 \
//    do {                                                                                                                                                                                                                                                                                     \
//        addr_t _fp_value;                                                                                                                                                                                                                                                                    \
//        __asm__ volatile("mov %0, x29" : "=r"(_fp_value));                                                                                                                                                                                                                                   \
//        uint64_t _value = fetch_addr_value(_fp_value + POINTER_SIZE);                                                                                                                                                                                                                        \
//        fndef_t *_fn = find_fn(_value, _co->p);                                                                                                                                                                                                                                              \
//        if (!_fn) break;                                                                                                                                                                                                                                                                     \
//        _co->scan_ret_addr = _value;                                                                                                                                                                                                                                                         \
//        assert(_co->scan_ret_addr);                                                                                                                                                                                                                                                          \
//        addr_t _prev_fp_value = fetch_addr_value(_fp_value);                                                                                                                                                                                                                                 \
//        assert(_prev_fp_value);                                                                                                                                                                                                                                                              \
//        _co->scan_offset = (uint64_t) p->share_stack.align_retptr - (_prev_fp_value - _fn->stack_size);                                                                                                                                                                                      \
//        TRACEF("[pre_tplcall_hook] co=%p, status=%d, fp_value=%p, prev_fn_value=%p, fn=%s, stack_size=%ld, scan_offset=%p, ret_addr=%p", _co, _co->status, (void *) _fp_value, (void *) _prev_fp_value, _fn->name, _fn->stack_size, (void *) _co->scan_offset, (void *) _co->scan_ret_addr); \
//    } while (0);
//#else
//// not define
//#endif


#ifdef __AMD64
#define CALLER_RET_ADDR(_co)                                  \
    ({                                                        \
        uint64_t _rbp_value;                                  \
        __asm__ volatile("mov %%rbp, %0" : "=r"(_rbp_value)); \
        fetch_addr_value(_rbp_value + POINTER_SIZE);           \
    });
#elif __ARM64
#define CALLER_RET_ADDR(_co)                                          \
    ({                                                                \
        addr_t _fp_value;                                             \
        __asm__ volatile("mov %0, x29" : "=r"(_fp_value));            \
        uint64_t _value = fetch_addr_value(_fp_value + POINTER_SIZE); \
        _value;                                                       \
    });
#else
#error "platform not supported"
#endif

#define P_LINKCO_CACHE_MAX 128

#define GC_WORKLIST_LIMIT 1024 // 每处理 1024 个 ptr 就 yield

#define ARENA_SIZE 67108864 // arena 的大小，单位 byte, 64M

#define ARENA_COUNT 4194304 // 64 位 linux 按照每 64MB 内存进行拆分，一共可以拆分这个多个 arena

#if defined(__DARWIN) && defined(__ARM64)
#define ALLOC_PAGE_SIZE 16384 // apple silicon 系统内存页按照 16byte 对齐

#define ALLOC_PAGE_MASK (ALLOC_PAGE_SIZE - 1) // 0b1111111111111
#else
#define ALLOC_PAGE_SIZE 8192 // 单位 byte

#define ALLOC_PAGE_MASK (ALLOC_PAGE_SIZE - 1) // 0b1111111111111
#endif


#define MSTACK_SIZE (8 * 1024 * 1024) // 8M 由于目前还没有栈扩容机制，所以初始化栈可以大一点

#define ARENA_PAGES_COUNT 8192 // 64M / 8K = 8192 个 page

#define ARENA_BITS_COUNT 2097152 // 1byte = 8bit 可以索引 4*8byte = 32byte 空间, 64MB 空间需要 64*1024*1024 / 32

#define PAGE_ALLOC_CHUNK_SPLIT 8192 // 每组 chunks 中的元素的数量

#define MMAP_SHARE_STACK_BASE 0xa000000000
#define ARENA_HINT_BASE 0xc000000000 // 0x00c0 << 32 // 单位字节，表示虚拟地址 offset addr = 0.75T
#ifdef __AMD64
#define ARENA_HINT_MAX 0x800000000000 // 128T
#else
#define ARENA_HINT_MAX 0x1000000000000 // 256T
#endif
#define ARENA_HINT_SIZE 1099511627776 // 1 << 40
#define ARENA_HINT_COUNT 128 // 0.75T ~ 128T

#define ARENA_BASE_OFFSET ARENA_HINT_BASE

#define CHUNK_BITS_COUNT 512 // 单位 bit, 一个 chunk 的大小是 512bit

#define STD_MALLOC_LIMIT (32 * 1024) // 32Kb

#define PAGE_SUMMARY_LEVEL 5 // 5 层 radix tree
#define PAGE_SUMMARY_MERGE_COUNT 8 // 每个上级 summary 索引的数量

#define PAGE_SUMMARY_COUNT_L4 (128 * 1024 * 1024 / 4) // l5 一共管理 33554432 个 chunk(4M) (128T空间)
#define PAGE_SUMMARY_COUNT_L3 (PAGE_SUMMARY_COUNT_L4 / PAGE_SUMMARY_MERGE_COUNT) // 4194304, 每个 summary item 管理 32M 空间
#define PAGE_SUMMARY_COUNT_L2 (PAGE_SUMMARY_COUNT_L3 / PAGE_SUMMARY_MERGE_COUNT) // 524288, 每个 summary item 管理 256M 空间
#define PAGE_SUMMARY_COUNT_L1 (PAGE_SUMMARY_COUNT_L2 / PAGE_SUMMARY_MERGE_COUNT) // 65536, 每个 summary item 管理 2G 空间
#define PAGE_SUMMARY_COUNT_L0 (PAGE_SUMMARY_COUNT_L1 / PAGE_SUMMARY_MERGE_COUNT) // 8192, 每个 summary item 管理 16G 空间

// radix tree 每一层级中 summary item，管理的 page 数量
#define L4_MAX_PAGES 512 // 512 个 page, 也就是 4KB
#define L3_MAX_PAGES (L4_MAX_PAGES * 8) // 4096 个 page, 32M
#define L2_MAX_PAGES (L3_MAX_PAGES * 8) // 32768 个 page, 256M
#define L1_MAX_PAGES (L2_MAX_PAGES * 8) // 262144 个 page, 2048M
#define L0_MAX_PAGES (L1_MAX_PAGES * 8) // 2097152 个 page, 16GB,  16GB * L0_count 8192 = 128T

#define PAGE_SUMMARY_MAX_VALUE 2LL ^ 21 // 2097152, max=start=end 的最大值

#define DEFAULT_NEXT_GC_BYTES (100 * 1024) // 100KB
#define NEXT_GC_FACTOR 2

#define WAIT_BRIEF_TIME 1 // ms
#define WAIT_SHORT_TIME 10 // ms
#define WAIT_MID_TIME 50 // ms
#define WAIT_LONG_TIME 100 // ms

typedef void (*void_fn_t)(void);

/**
 * 参考 linux, 栈从上往下增长，所以在数学意义上 base > end
 */
typedef struct {
    addr_t stack_base; // 虚拟起始地址(按照内存申请的节奏来，这里依旧是低地址位置)
    uint64_t stack_size; // 栈空间
    ucontext_t ctx;
} mmode_t;

typedef struct mspan_t {
    struct mspan_t *next; // mspan 是双向链表
    // struct mspan_t *prev;

    uint32_t sweepgen; // 目前暂时是单线程模式，所以不需要并发垃圾回收
    addr_t base; // mspan 在 arena 中的起始位置
    addr_t end;
    uint8_t spanclass; // spanclass index (基于 sizeclass 通过 table 可以确定 page 的数量和 span 的数量)

    uint64_t pages_count; // page 的数量，通常可以通过 sizeclass 确定，但是如果 spanclass = 0 时，表示大型内存，其 pages
    // 是不固定的
    uint64_t obj_count; // mspan 中 obj 的数量，也可以通过 sizeclass 直接确定,如果是分配大内存时，其固定为 1，
    // 也就是一个 obj 占用一个 span
    uint64_t obj_size; // obj_count * obj_size 不一定等于 pages_count * page_size, 虽然可以通过 sizeclass
    // 获取，但是不兼容大对象
    uint64_t alloc_count; // 已经用掉的 obj count
    uint64_t free_index; // 下一个空闲 bit 的位置

    // bitmap 结构, alloc_bits 标记 obj 是否被使用， 1 表示使用，0表示空闲
    gc_bits *alloc_bits;
    gc_bits *gcmark_bits; // gc 阶段标记，1 表示被使用(三色标记中的黑色),0表示空闲(三色标记中的白色)

    mutex_t alloc_locker;
    mutex_t gcmark_locker;
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
    mutex_t locker;

    mspan_t *partial_list; // 还有空闲 span obj 的链表
    mspan_t *full_list;
} mcentral_t;

typedef struct {
    uint64_t blocks[8];
} page_chunk_t; // page_chunk 现在占用 64 * 8 = 512bit

// TODO start/end/max 的正确编码值应该是 uint21_t,后续需要正确实现,能表示的最大值是 2^21=2097152
// 0 标识无空闲， 1 标识有空闲
typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t max;
    uint8_t full; // start=end=max=2^21 最大值， full 设置为 1, 表示所有空间都是空间的
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
    mspan_t *spans[ARENA_PAGES_COUNT]; // page = 8192, 所以 pages 的数量是固定的

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
    // 非连续空间
    arena_t *arenas[ARENA_COUNT];

    mcentral_t centrals[SPANCLASS_COUNT];
    uint32_t sweepgen;
    slice_t *spans; // 所有分配的 span 都会在这里被引用
    arena_hint_t *arena_hints;

    // cursor ~ end 可能会跨越多个连续的 arena
    struct {
        addr_t cursor; // 指向未被使用的地址, 从 0 开始计算
        addr_t end; // 指向本次申请的终点
    } current_arena;

    fixalloc_t spanalloc;
} mheap_t;

typedef struct {
    mheap_t *mheap; // 全局 heap, 访问时需要加锁
    mutex_t locker;
    uint32_t sweepgen;
    uint64_t gc_count; // gc 循环次数
} memory_t;

typedef enum {
    CO_STATUS_RUNNABLE = 1, // 允许被调度
    CO_STATUS_RUNNING = 2, // 正在运行
    CO_STATUS_TPLCALL = 3, // 陷入系统调用
    CO_STATUS_RTCALL = 4, // 陷入系统调用
    CO_STATUS_WAITING = 5, // 等待 IO 事件就绪
    CO_STATUS_DEAD = 6, // 死亡状态
} co_status_t;

typedef enum {
    P_STATUS_INIT = 0,
    P_STATUS_DISPATCH = 1,

    //    P_STATUS_TPLCALL = 2,
    //    P_STATUS_RTCALL = 3,

    P_STATUS_RUNNABLE = 3,
    P_STATUS_RUNNING = 4,
    P_STATUS_PREEMPT = 5,
    P_STATUS_EXIT = 6,
} p_status_t;

typedef struct n_processor_t n_processor_t;
typedef struct coroutine_t coroutine_t;

// lock_of is n_chan_t or rt_mutex_t
typedef bool (*unlock_fn)(coroutine_t *co, void *lock_of);


// 通过 gc malloc 申请
struct linkco_t {
    coroutine_t *co;
    linkco_t *prev;
    linkco_t *next;
    linkco_t *waitlink; // linkco list, use in unlockf and select list

    void *data;
    n_chan_t *chan; // chan
    bool is_select; // 是否在 select 中被使用，需要配合 coroutine.select_done 实现 cas
    bool success; // chan recv/send 是否成功
};

// 必须和 nature code 保持一致
typedef struct n_future_t {
    int64_t size;
    void *result;
    n_union_t *error; // 类似 result 一样可选的 error
    void *await_co;
} n_future_t;

struct coroutine_t {
    int64_t id;
    bool main; // 是否是 main 函数
    bool solo; // 当前协程需要独享线程
    bool ticket;
    int64_t flag;
    co_status_t status;
    aco_t aco;
    void *fn; // fn 指向

    void *data; // 临时额外数据存储, gc 不会读取该数据进行标记，标记数据不能存储在这里
    void *arg; // coroutine 额外请求参数处理, gc 会读取该数据进行标记。

    n_processor_t *p; // 当前 coroutine 绑定的 p

    n_future_t *future;

    struct coroutine_t *await_co; // 可能为 null, 如果不为 null 说明该 co 在等待当前 co exit
    mutex_t dead_locker;

    // 当前 coroutine stack 颜色是否为黑色, 黑色说明当前 goroutine stack 已经扫描完毕
    // gc stage 是 mark 时, 当 gc_black 值小于 memory->gc_count 时，说明当前 coroutine stack 不是黑色的
    uint64_t gc_black;

    /**
    * arm64
     高地址
    +------------------------+
    |     上一个栈帧        |
    +------------------------+
    |     局部变量          |
    +------------------------+
    |     保存的寄存器      |
    +------------------------+
    |     LR (x30)          |
    +------------------------+
    |     FP (x29)          |
    +------------------------+ current FP
    |     参数区域          |
    +------------------------+
    低地址

    amd64
    高地址
    +------------------------+
    |     上一个栈帧        |
    +------------------------+
    |     返回地址          |
    +------------------------+
    |     保存的 RBP        |
    +------------------------+
    |     局部变量          |
    +------------------------+
    |     保存的寄存器      |
    +------------------------+
    |     参数区域          |
    +------------------------+
    低地址
    */
    //    uint64_t scan_offset;
    //    uint64_t scan_ret_addr;

    bool has_error;
    n_interface_t *error; // throwable
    n_vec_t *traces; // element is n_trace_t

    ATOMIC int32_t select_done;
    linkco_t *waiting; // 当前 co 等待的 linkco, 如果存在多个 linkco 时，通过 linkco.waitlink 链接
    unlock_fn wait_unlock_fn;
    void *wait_lock;

    struct coroutine_t *next; // coroutine list
};

/**
 * 位于 share_processor_t 中的协程，如果运行时间过长会被抢占式调度
 * 共享处理器的数量通畅等于线程的数量, 所以可以将线程维度的无锁内存分配器放置再这里
 */
struct n_processor_t {
    int index;
    int64_t *tls_yield_safepoint_ptr;
    mcache_t mcache; // 线程维度无锁内存分配器
    aco_t main_aco; // 每个 processor 都会绑定一个 main_aco 用于 aco 的切换操作。
    aco_share_stack_t share_stack; // processor 中的所有的 stack 都使用该共享栈

    struct sigaction sig;
    uv_timer_t timer; // 辅助协程调度的定时器
    uv_loop_t uv_loop; // uv loop 事件循环

    uint64_t need_stw; // 外部声明, 内部判断 是否需要 stw
    uint64_t in_stw; // 内部声明, 外部判断是否已经 stw

    // 当前 p 需要被其他线程读取的一些属性都通过该锁进行保护
    // - 如更新 p 对应的 co 的状态等
    mutex_t thread_locker;
    p_status_t status;

    uv_thread_t thread_id; // 当前 processor 绑定的 pthread 线程
    coroutine_t *coroutine; // 当前正在调度的 coroutine
    uint64_t co_started_at; // 协程调度开始时间, 单位纳秒，一般从系统启动时间开始计算，而不是 unix 时间戳

    // 存储 linkco_t 的指针，注意 gc 的时候需要遍历进行 mark 避免被错误清理
    linkco_t *linkco_cache[P_LINKCO_CACHE_MAX];
    uint8_t linkco_count;

    rt_linked_fixalloc_t co_list; // 当前 processor 下的 coroutine 列表
    rt_linked_fixalloc_t runnable_list;

    rt_linked_fixalloc_t gc_worklist; // gc 扫描的 ptr 节点列表
    uint64_t gc_work_finished; // 当前处理的 GC 轮次，每完成一轮 + 1

    struct sc_map_64v caller_cache; // 函数缓存定义

    struct n_processor_t *next; // processor 链表支持
};

void rti_throw(char *msg, bool panic);

void rti_co_throw(coroutine_t *co, char *msg, bool panic);

void coroutine_dump_error(coroutine_t *co);

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
n_processor_t *processor_get();

coroutine_t *coroutine_get();

void processor_set_status(n_processor_t *p, p_status_t status);

//#define PRE_RTCALL_HOOK()                                                                               \
//    do {                                                                                                \
//        n_processor_t *p = processor_get();                                                             \
//        if (!p) break;                                                                                  \
//        if (p->status == P_STATUS_RTCALL || p->status == P_STATUS_TPLCALL) break;                       \
//        processor_set_status(p, P_STATUS_RTCALL);                                                       \
//        DEBUGF("[pre_rtcall_hook] target %s, status set rtcall success, non-preemption", __FUNCTION__); \
//        coroutine_t *_co = coroutine_get();                                                             \
//        if (!_co) break;                                                                                \
//        CO_SCAN_REQUIRE(_co);                                                                           \
//    } while (0);

//void pre_tplcall_hook();

//void post_tplcall_hook();

//void post_rtcall_hook(char *target);

void *rti_gc_malloc(uint64_t size, rtype_t *rtype);

#endif // NATURE_BASIC_H

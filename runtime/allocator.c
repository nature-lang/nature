#include "allocator.h"
#include "processor.h"

// TODO 根据 size 计算出 67 个类中的一个
static uint8_t calc_sizeclass(uint8_t size);

// 7位sizeclass + 1位是否包含指针
static uint8_t make_spanclass(uint8_t sizeclass, uint8_t has_ptr);


static uint8_t take_sizeclass(uint8_t spanclass);

static uint span_obj_size(uint8_t spanclass);

static uint span_pages_count(uint8_t spanclass);

/**
 * 从 mheap page_alloc_t 中看看有没空闲的 pages_count 使用
 * @param pages_count
 * @return
 */
static addr_t page_alloc(uint pages_count);

/**
 * TODO 偏移值消除问题
 * @param base
 * @return
 */
static arena_t *take_arena(addr_t base);

/**
 * 计算 mheap 的 pages ->spans
 * @param span
 */
static void mheap_set_spans(mspan_t *span) {
    // - 根据 span.base 定位 arena
}

/**
 * @param pages_count
 * @param spanclass
 * @return
 */
static mspan_t *mheap_alloc(uint pages_count, uint8_t spanclass) {
    // - 从 page_alloc 中查看有没有连续 pages_count 空闲的页，如果有就直接分配
    // 因为有垃圾回收的存在，所以 page_alloc 中的某些部分存在空闲且连续的 pages
    addr_t base = page_alloc(pages_count);

    // - TODO 已有的 arena list 中没有可用的 pages,基于 current arena 对当前 page alloc 进行扩容(以 chunk 为单位)

    // - TODO 经过上面的 grow 再次从 page_alloc 中拉取合适大小的 npages 并组成 span

    // - 新增的 span 需要在 arena 中建立 page -> span 的关联关系
    mspan_t *span = mspan_new(base, pages_count);

    return span;
}

static mspan_t *mcentral_grow(mcentral_t mcentral) {
    // 从 mheap 中按 page 申请一段内存, mspan 对 mheap 是已知的， mheap 同样需要管理 mspan 列表
    uint pages_count = span_pages_count(mcentral.spanclass);

    mspan_t *span = mheap_alloc(pages_count, mcentral.spanclass);
    return span;
}

/**
 * 从 mcache 中找到一个空闲的 mspan 并返回
 * @param mcentral
 * @return
 */
static mspan_t *cache_span(mcentral_t mcentral) {
    mspan_t *span = NULL;
    if (!list_is_empty(mcentral.partial_swept)) {
        // partial 是空的，表示当前 mcentral 中已经没有任何可以使用的 mspan 了，需要去 mheap 中申请咯
        span = list_pop(mcentral.partial_swept);
        goto HAVE_SPAN;
    }

    // 当前 mcentral 中已经没有可以使用的 mspan 需要走 grow 逻辑
    span = mcentral_grow(mcentral);

    HAVE_SPAN:
    assertf(span && span->obj_count - span->alloc_count > 0, "span unavailable");
    return span;
}

/**
 * 将 mspan 归还到 mcentral 队列中
 * @param mcentral
 * @param mspan
 */
static void uncache_span(mcentral_t mcentral, mspan_t *mspan) {
    assertf(mspan->alloc_count == 0, "mspan alloc_count == 0");

    if (mspan->obj_count - mspan->alloc_count > 0) {
        list_push(mcentral.partial_swept, mspan);
    } else {
        list_push(mcentral.full_swept, mspan);
    }
}


/**
 * 从 mcentral 中找到一个可以使用的 span 给 mcache 使用
 * mcache.alloc[spanclass] 对应的 mspan 可能 null
 * @param mcache
 * @param spanclass
 * @return
 */
static mspan_t *mcache_refill(mcache_t mcache, uint64_t spanclass) {
    mspan_t *mspan = mcache.alloc[spanclass];
    mcentral_t mcentral = memory->mheap.centrals[spanclass];

    if (mspan) {
        // mspan to mcentral
        uncache_span(mcentral, mspan);
    }

    // cache
    mspan = cache_span(mcentral);
    mcache.alloc[spanclass] = mspan;
    return mspan;
}

/**
 * 从 spanclass 对应的 span 中找到一个 free 的 obj 并返回
 * @param spanclass
 * @return
 */
static void *mcache_alloc(uint8_t spanclass) {
    processor_t p = processor_get();
    mcache_t mcache = p.mcache;
    mspan_t *mspan = mcache.alloc[spanclass];
    uint64_t obj_size = span_obj_size(spanclass);

    // 如果 mspan 中有空闲的 obj 则优先选择空闲的 obj 进行分配
    // 判断当前 mspan 是否已经满载了，如果满载需要从 mcentral 中填充 mspan
    if (mspan == NULL || mspan->alloc_count == mspan->obj_count) {
        mspan = mcache_refill(mcache, spanclass);
    }

    for (int i = 0; i <= mspan->obj_count; i++) {
        bool used = bitmap_get(mspan->alloc_bits, i);
        if (used) {
            continue;
        }
        // 找到了一个空闲的 obj,计算其
        addr_t addr = mspan->base + i * obj_size;
        // 标记该节点已经被使用
        bitmap_set(mspan->alloc_bits, i);
        mspan->alloc_count += 1;
        return (void *) addr;
    }
}


// 设置 meta heap_arena 的 bits
static void heap_arena_bits_set(void *addr, uint size, uint obj_size, typedef_t *type);

static void mcentral_full_swept_push(uint8_t spanclass, mspan_t *span);

// 单位
static void *std_malloc(uint size, typedef_t *type) {
    bool has_ptr = type != NULL || type->last_ptr_count;
    uint8_t sizeclass = calc_sizeclass(size);
    uint8_t spanclass = make_spanclass(sizeclass, has_ptr);

    uint obj_size = span_obj_size(spanclass);

    void *addr = mcache_alloc(spanclass);
    if (has_ptr) {
        // 对 arena.bits 做标记,标记是指针还是标量
        heap_arena_bits_set(addr, size, obj_size, type);
    }

    return addr;
}

static void *large_malloc(uint size, typedef_t *type) {
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

    // 将 span 推送到 full swept 中，这样才能被 sweept
    mcentral_full_swept_push(spanclass, s);
    s->obj_count = 1;
    return s->base;
}

/**
 * TODO 未完成
 * @return
 */
arena_hint_t *arena_hints_init() {
    arena_hint_t *first = NEW(arena_hint_t);
    first->addr = (uint64_t) ARENA_HINT_BASE;
    first->last = false;
    first->next = NULL;

    arena_hint_t *prev = first;
    for (int i = 1; i < ARENA_HINT_COUNT; i++) {
//        arena_hint_t *item = NEW(arena_hint_t);
//        item->addr = prev->
    }

    return first;
}


/**
 * - 分配的大小是固定的，再 linux 64 中就是 64MB
 * - 基于 hint 调用 mmap 申请一块内存(如果申请成功则更新 hint)
 * - 更新 mheap.current_arena
 * - 由于这是一个新的 arena,所以需要像 mheap.arenas 中添加数据，而不再是 null 了
 * 但是不需要更新 mheap.arenas 相关的值,因为没有真正的开始触发分配逻辑
 * 同样mheap.pages 同样也不需要更新，因为没有相关的值被更新
 * @return
 */
arena_t *arena_new(mheap_t mheap) {
    arena_hint_t *hint = mheap.arena_hints;

    arena_t *arena = NEW(arena_t);
    void *v = NULL;
    while (true) {
        v = memory_sys_alloc(hint->addr, ARENA_SIZE);
        if (v == (void *) hint->addr) {
            // 分配成功, 定义下一个分配点,基于此可以获得 64MB 对齐的内存地址
            hint->addr += ARENA_SIZE;
            break;
        }
        // 释放刚刚申请的内存区域
        memory_sys_free(v, ARENA_SIZE);

        // 进行申请重试
        assertf(!hint->last, "out of memory: arena hint use up");
        hint = hint->next;
    }

    return arena;
}


void memory_init() {
    // - 初始化 mheap
    mheap_t mheap = {0}; // 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap.spans = slice_new();
    for (int i = 0; i < PAGE_SUMMARY_LEVEL; i++) {
        mheap.pages.summary[i] = slice_new();
    }

    // - arena hint init
    mheap.arena_hints = arena_hints_init();

    // - first arena init
    mheap.current_arena = arena_new(mheap);

    // - 初始化 mcentral
    for (int i = 0; i < SPANCLASS_COUNT; i++) {
        mcentral_t item = mheap.centrals[i];
        item.spanclass = i;
        item.partial_swept = list_new();
//        item.partial_unswept = list_new();
        item.full_swept = list_new();
//        item.full_unswept = list_new();
    }
}


/**
 * 调用 malloc 时已经将类型数据传递到了 runtime 中，obj 存储时就可以已经计算了详细的 gc_bits 能够方便的扫描出指针
 * @param size
 * @param type
 * @return
 */
void *runtime_malloc(uint size, typedef_t *type) {
    if (size <= STD_MALLOC_LIMIT) {
        // 1. 标准内存分配(0~32KB)
        return std_malloc(size, type);
    }

    // 2. 大型内存分配(大于>32KB)
    return large_malloc(size, type);
}

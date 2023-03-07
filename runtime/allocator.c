#include "allocator.h"
#include "processor.h"

// TODO 根据 size 计算出 67 个类中的一个
static uint8_t calc_sizeclass(uint8_t size);

// 7位sizeclass + 1位是否包含指针
static uint8_t make_spanclass(uint8_t sizeclass, uint8_t has_ptr);


static uint8_t take_sizeclass(uint8_t spanclass);

static uint span_obj_size(uint8_t spanclass);

static uint span_pages_count(uint8_t spanclass);

static void *sys_mmap(addr_t hint_addr, uint64_t size);

/**
 * 从 mheap page_alloc_t 中看看有没空闲的 pages_count 使用
 * @param pages_count
 * @return
 */
static addr_t page_alloc(uint pages_count) {
}

static uint64_t arena_index(addr_t base);

static addr_t arena_base(uint64_t arena_index);

static uint64_t chunk_index(addr_t base);

static uint64_t chunk_index_l1(uint64_t index);

static uint64_t chunk_index_l2(uint64_t index);

/**
 * start,max,end
 * @param chunk
 * @return
 */
static page_summary_t chunk_summarize(page_chunk_t chunk);


static void page_alloc_summary(addr_t base, uint64_t size) {
    uint64_t start_index = chunk_index(base);
    uint64_t end_index = chunk_index(base + size - 1);

//    l5_summary =
    // 计算每个 chunk 的 size
    for (uint64_t i = start_index; i <= end_index; ++i) {

    }
}

/**
 * 建立 mheap.page_alloc.chunks
 * @param base
 * @param size
 * @return
 */
static void page_alloc_grow(addr_t base, uint64_t size) {
    page_alloc_t *page_alloc = &memory->mheap.pages;
    // 维护 chunks 数据
    for (uint64_t index = chunk_index(base); index < chunk_index(base + size); index++) {
        // 计算 l1 可能为 null
        uint64_t l1 = chunk_index_l1(index);
        if (page_alloc->chunks[l1] == NULL) {
            page_alloc->chunks[l1] = mallocz(sizeof(page_chunk_t) * PAGE_ALLOC_CHUNK_L2);
        }
        // 这里表示数组，通过上面的 mallocz 分配的内存区域已经是 0 了，不需要重复赋值
        // 真正使用的时候才需要设置这里的值
//        page_chunk_t *l2_chunks = page_alloc->chunks[l1];
//        uint64_t l2 = chunk_index_l2(index);
//        l2_chunks[l2].blocks = {0};
    }

    // 计算 summary
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
void *mheap_sys_alloc(mheap_t mheap, uint64_t *size) {
    arena_hint_t *hint = mheap.arena_hints;

    // size 对齐
    uint64_t alloc_size = align((int64_t) *size, ARENA_SIZE);

    void *v = NULL;
    while (true) {
        v = sys_mmap(hint->addr, alloc_size);
        if (v == (void *) hint->addr) {
            // 分配成功, 定义下一个分配点,基于此可以获得 64MB 对齐的内存地址
            hint->addr += alloc_size;
            break;
        }
        // 分配失败
        // 释放刚刚申请的内存区域
        memory_sys_free(v, alloc_size);

        // 进行申请重试
        assertf(!hint->last, "out of memory: arena hint use up");
        hint = hint->next;
    }

    // 申请成功，申请的范围是 v ~ (v+alloc_size), 可能包含多个 arena, 需要创建相关 arena meta
    for (uint64_t i = arena_index((uint64_t) v); i <= arena_index((uint64_t) v + alloc_size - 1); ++i) {
        arena_t *arena = NEW(arena_t);
        arena->base = arena_base(i);
        mheap.arenas[i] = arena;
        slice_push(mheap.arena_indexes, (void *) i);
    }

    *size = alloc_size;
    return v;
}

/**
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
    arena_t *arena = take_arena(span->base);

    uint page_index = (span->base - arena->base) / PAGE_SIZE;
    for (int i = 0; i < span->pages_count; i++) {
        arena->spans[page_index] = span;
        page_index += 1;
    }
}

/**
 * arena heap 增长 pages_count 长度,如果 current arena 没有足够的空间，将会申请 1 个或者 n个崭新的 arena
 * @param pages_count
 */
static void mheap_grow(uint pages_count) {
    // pages_alloc 按 chunk 管理内存，所以需要按 chunk 包含的 pages_count 对齐
    uint64_t size = align(pages_count, PAGE_ALLOC_CHUNK_SIZE) * PAGE_SIZE;

    addr_t cursor = memory->mheap.current_arena.cursor;
    addr_t end = memory->mheap.current_arena.end;
    assertf(end > cursor, "mheap not hold arena failed");

    if (end - cursor < size) {
        // cursor 没有足够的空间，需要重新申请一个新的空间
        // alloc_size 在方法内部会按 arena size 对齐
        uint64_t alloc_size = size;
        addr_t v = (addr_t) mheap_sys_alloc(memory->mheap, &alloc_size);
        if (v == end) {
            // arena 空间连续,此时只需要更新 end
            end += alloc_size;
        } else {
            cursor = v;
            end = v + alloc_size;
        }
    }

    // 基于 current_arena 进行 pages 的分配,分配的 pages 需要被 page_alloc 索引
    page_alloc_grow(cursor, size);

    // 更新 current arena
    memory->mheap.current_arena.cursor = cursor + size;
    memory->mheap.current_arena.end = cursor + end;
}

/**
 * @param pages_count
 * @param spanclass
 * @return
 */
static mspan_t *mheap_alloc_span(uint pages_count, uint8_t spanclass) {
    // - 从 page_alloc 中查看有没有连续 pages_count 空闲的页，如果有就直接分配
    // 因为有垃圾回收的存在，所以 page_alloc 中的某些部分存在空闲且连续的 pages
    addr_t base = page_alloc(pages_count);

    if (base == 0) {
        // -  page_alloc_t 中没有找到可用的 pages,基于 current arena 对当前 page alloc 进行扩容(以 chunk 为单位)
        mheap_grow(pages_count);

        // - 经过上面的 grow 再次从 page_alloc 中拉取合适大小的 pages_count 并组成 span
        base = page_alloc(pages_count);
    }
    assertf(base > 0, "out of memory: page alloc failed");

    // - 新增的 span 需要在 arena 中建立 page -> span 的关联关系
    mspan_t *span = mspan_new(base, pages_count);
    mheap_set_spans(span);

    return span;
}

static mspan_t *mcentral_grow(mcentral_t mcentral) {
    // 从 mheap 中按 page 申请一段内存, mspan 对 mheap 是已知的， mheap 同样需要管理 mspan 列表
    uint pages_count = span_pages_count(mcentral.spanclass);

    mspan_t *span = mheap_alloc_span(pages_count, mcentral.spanclass);
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
    mspan_t *s = mheap_alloc_span(pages_count, spanclass);
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


void memory_init() {
    // - 初始化 mheap
    mheap_t mheap = {0}; // 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap.spans = slice_new();
    for (int i = 0; i < PAGE_SUMMARY_LEVEL; i++) {
        mheap.pages.summary[i] = slice_new();
    }

    // - arena hint init
    mheap.arena_hints = arena_hints_init();

    // - arena index init
    mheap.arena_index = slice_new();

    // - TODO current arena init

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

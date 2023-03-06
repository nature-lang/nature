#include "allocator.h"

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
    for (int i = 1; i < ARENA_BITS_COUNT; i++) {
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
        item.partial_unswept = list_new();
        item.full_swept = list_new();
        item.full_unswept = list_new();
    }


    // - 初始化 mcache
}


/**
 * 调用 malloc 时已经将类型数据传递到了 runtime 中，obj 存储时就可以已经计算了详细的 gc_bits 能够方便的扫描出指针
 * @param size
 * @param type
 * @return
 */
void *runtime_malloc(uint size, typedef_t *type) {
    // 1. 标准内存分配(0~32KB)


    // 2. 大型内存分配(大于>32KB)

    return NULL;
}

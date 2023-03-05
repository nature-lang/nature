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

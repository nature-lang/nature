#include <stdio.h>

#include "runtime/memory.h"
#include "test.h"
#include "test_runtime.h"

int setup() {
    memory = NEW(memory_t);
    memory->sweepgen = 0;
    memory->gc_count = 0;
    mutex_init(&memory->locker, false);

    // 初始化 gc 参数
    allocated_bytes = 0;
    next_gc_bytes = DEFAULT_NEXT_GC_BYTES;

    // - 初始化 mheap
    mheap_t *mheap = mallocz_big(sizeof(mheap_t));// 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap->page_alloc.summary[4] = mallocz_big(PAGE_SUMMARY_COUNT_L5 * sizeof(page_summary_t));
    mheap->page_alloc.summary[3] = mallocz_big(PAGE_SUMMARY_COUNT_L4 * sizeof(page_summary_t));
    mheap->page_alloc.summary[2] = mallocz_big(PAGE_SUMMARY_COUNT_L3 * sizeof(page_summary_t));
    mheap->page_alloc.summary[1] = mallocz_big(PAGE_SUMMARY_COUNT_L2 * sizeof(page_summary_t));
    mheap->page_alloc.summary[0] = mallocz_big(PAGE_SUMMARY_COUNT_L1 * sizeof(page_summary_t));// 8192

    // - arena hint init
    mheap->arena_hints = arena_hints_init();

    // - arena index init
    mheap->arena_indexes = slice_new();
    mheap->spans = slice_new();

    // new current arena
    mheap->current_arena.cursor = 0;
    mheap->current_arena.end = 0;

    // - 初始化 mcentral
    for (int i = 0; i < SPANCLASS_COUNT; i++) {
        mcentral_t *central = &mheap->centrals[i];
        central->spanclass = i;

        central->partial_list = NULL;
        central->full_list = NULL;
        mutex_init(&central->locker, false);
    }

    // - 初始化 fixalloc
    fixalloc_init(&mheap->spanalloc, sizeof(mspan_t));

    // - 初始化 gcbits
    gcbits_areas_init();

    memory->mheap = mheap;
    return 0;
}

int teardown() {
    return 0;
}

static void test_basic() {
    mheap_grow(512 * 9);
    addr_t addr = page_alloc_find(1);
    assert_int_equal(addr, 0xc000000000);

    addr = page_alloc_find(1);
    assert_int_equal(addr, 0xc000002000);

    addr = page_alloc_find(512);
    assert_int_equal(addr, 0xc000004000);


    addr = page_alloc_find(510);
    addr = page_alloc_find(512 * 6);
    assert_int_equal(addr, 0xc000800000);

    addr = page_alloc_find(1);
    assert_int_equal(addr, 0xc002000000);
}


int main(void) {
    setup();
    test_basic();
    teardown();
}
#include "memory.h"

void memory_init() {
    // - 初始化 mheap
    mheap_t mheap = {0}; // 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap.spans = slice_new();
    for (int i = 0; i < PAGE_SUMMARY_LEVEL; i++) {
        mheap.pages.summary[i] = slice_new();
    }

    // - arena hint init

    // - 初始化首个 arena


    // - 初始化 mcentral

    // - 初始化 mcache
}

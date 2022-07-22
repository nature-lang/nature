#ifndef NATURE_SRC_LIB_SLICE_H_
#define NATURE_SRC_LIB_SLICE_H_

#include <stdlib.h>

/**
 * slice 是一个动态数组，存储的内容为数据指针,其内容存储依旧是在内存中的连续空间。
 * 当空间不足时，会将整个数组进行迁移
 */
typedef struct {
    int count;
    int capacity;
    void **take;
} slice;

slice *slice_new();

void slice_insert(slice *s, void *value);

void slice_free(slice *s);

#endif //NATURE_SRC_LIB_SLICE_H_

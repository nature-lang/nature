#include "array.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils/helper.h"

array_t *array_new(int count, int size) {
    assertf(size <= 8, "size must be less than 8"); // 技术问题，目前只能实现长度为 8 的在栈中的动态数组

    int capacity = count;
    if (capacity == 0) {
        capacity = 8;
    }

    array_t *a = malloc(sizeof(array_t));
    a->count = count; // 实际的节点数量
    a->capacity = capacity; // 当前实际申请的内存空间
    a->size = size;
    a->data = calloc(capacity, size);
    return a;
}

void *array_value(array_t *a, int index) {
    // 判断是否越界
    if (index + 1 > a->count) {
        printf("index out of range [%d] with length %d\n", index, a->count);
        exit(1);
    }

    void *p = a->data;
    p += a->size * index;
    return p;
}

/**
 * @param a
 * @param value
 */
void array_push(array_t *a, void *value) {
    if (a->capacity <= a->count) {
        a->capacity = a->capacity * 2;
        a->data = realloc(a->data, a->size * a->capacity);
    }

    // 从 value 中读取 size 个字节，memset 到 a->data 中
    uint8_t *p = a->data;
    p += a->size * a->count; // p 此时一次只传输一个 byte
    memcpy(p, &value, a->size);
    a->count += 1;
}

// 连接两个数组的内容
void array_concat(array_t *a, array_t *b) {
    assertf(a->size == b->size, "array size not match");
    for (int i = 0; i < b->count; ++i) {
        array_push(a, array_value(b, i));
    }
}

/**
 * 从数组 a 中截取 [start, end) 的内容, 如果 start = -1 则从 0 开始， 如果 end = -1 则截取到 a->count
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return
 */
array_t *array_slice(array_t *a, int start, int end) {
    if (start == -1) {
        start = 0;
    }
    if (end == -1 || end >= a->count) {
        end = a->count - 1;
    }

    array_t *b = array_new(end - start, a->size);

    // 使用 memcpy 将 a->data 中的内容拷贝到 b->data 中
    void *p = array_value(a, start);
    memcpy(b->data, p, a->size * b->count);
    return b;
}

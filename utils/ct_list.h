#ifndef NATURE_UTILS_CT_LIST_H
#define NATURE_UTILS_CT_LIST_H

#include <stdlib.h>
#include <stdint.h>

#define LIST_DEFAULT_CAPACITY 8

typedef struct {
    uint64_t element_size; // 元素的大小
    uint64_t length; // 元素的数量
    uint64_t capacity; // 预申请的数量
    uint8_t *take; // 申请的内存区域
} list_t;

/**
 * 和 slice 不同， list 中的元素数量是可以超过指针大小的
 * @return
 */
list_t *ct_list_new(uint64_t element_size);

/**
 * 示例
 * uint64 a = 233;
 * list_push(list, &a) // 将栈中存储的值，按 element_size 进行 copy
 * 依赖 memmove 移动内存数据
 * 如果超长则使用 realloc 扩容 take 区域,realloc 会自动移动过去
 * @param l
 * @param value
 */
void *ct_list_push(list_t *l, void *value);

void *ct_list_value(list_t *l, uint64_t index);

// list 一般没有 pop 的说法
//void list_pop(list_t *l, void *value);
//void ct_list_remove(list_t *l, uint64_t index);

#endif //NATURE_LIST_H

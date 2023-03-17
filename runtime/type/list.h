#ifndef NATURE_LIST_H
#define NATURE_LIST_H

#include "utils/type.h"
#include <stdint.h>

#define LIST_INIT_CAPACITY 8

/**
 * count = 0
 * capacity = 8
 * @param element_size
 * @return
 */
memory_list_t *list_new(uint64_t element_size);

/**
 * 返回 index 对应的 array 处的内存位置
 * @param l
 * @param index
 * @return
 */
void *list_value(memory_list_t *l, uint64_t index);


uint64_t list_count(memory_list_t *l);

/**
 * slice 不会修改原数组
 * 从数组 a 中截取 [start, end) 的内容, 不包含 end
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return 返回切片后的数据
 */
memory_list_t *list_slice(memory_list_t *l, int start, int end);

#endif //NATURE_LIST_H

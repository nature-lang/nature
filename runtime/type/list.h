#ifndef NATURE_LIST_H
#define NATURE_LIST_H

#include "utils/type.h"
#include <stdint.h>

#define LIST_DEFAULT_CAPACITY 8

memory_list_t *list_new(uint64_t rtype_index, uint64_t element_rtype_index, uint64_t capacity);

/**
 * 返回 index 对应的 array 处的内存位置
 * @param l
 * @param index
 * @return
 */
void *list_access(memory_list_t *l, uint64_t index);

void list_assign(memory_list_t *l, uint64_t index, void *ref);

/**
 * @param l
 * @return
 */
uint64_t list_length(memory_list_t *l);

/**
 * 将 reference 处的值通过 memmove 移动 element_size 个字节到 array offest 中
 * @param l
 * @param value
 */
void list_push(memory_list_t *l, void *ref);

/**
 * slice 不会修改原数组
 * 从数组 a 中截取 [start, end) 的内容, 不包含 end
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return 返回切片后的数据
 */
memory_list_t *list_slice(uint64_t rtype_index, memory_list_t *l, uint64_t start, uint64_t end);

/**
 * 合并 a 和 b 两个 list 到一个新的 list 中
 * @param a
 * @param b
 * @return
 */
memory_list_t *list_concat(uint64_t rtype_index, memory_list_t *a, memory_list_t *b);

#endif //NATURE_LIST_H

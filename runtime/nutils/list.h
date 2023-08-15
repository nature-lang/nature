#ifndef NATURE_LIST_H
#define NATURE_LIST_H

#include "utils/type.h"
#include <stdint.h>
#include "utils/autobuf.h"

#define LIST_DEFAULT_CAPACITY 8

n_list_t *list_new(uint64_t rtype_hash, uint64_t element_rtype_hash, uint64_t length, uint64_t capacity);

n_cptr_t list_element_addr(n_list_t *l, uint64_t index);

/**
 * 返回 index 对应的 array 处的内存位置
 * @param l
 * @param index
 * @return
 */
void list_access(n_list_t *l, uint64_t index, void *value_ref);

void list_assign(n_list_t *l, uint64_t index, void *ref);

/**
 * @param l
 * @return
 */
uint64_t list_length(n_list_t *l);

uint64_t list_capacity(n_list_t *l);

void *list_raw(n_list_t *l);

/**
 * 将 reference 处的值通过 memmove 移动 element_size 个字节到 array offest 中
 * @param l
 * @param value
 */
void list_push(n_list_t *l, void *ref);

n_cptr_t list_next_addr(n_list_t *l);

/**
 * slice 不会修改原数组
 * 从数组 a 中截取 [start, end) 的内容, 不包含 end
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return 返回切片后的数据
 */
n_list_t *list_slice(uint64_t rtype_hash, n_list_t *l, uint64_t start, uint64_t end);

/**
 * 合并 a 和 b 两个 list 到一个新的 list 中
 * @param a
 * @param b
 * @return
 */
n_list_t *list_concat(uint64_t rtype_hash, n_list_t *a, n_list_t *b);

#endif //NATURE_LIST_H

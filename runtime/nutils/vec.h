#ifndef NATURE_VEC_H
#define NATURE_VEC_H

#include "utils/type.h"
#include <stdint.h>
#include "utils/autobuf.h"

#define VEC_DEFAULT_CAPACITY 8

n_vec_t *vec_new(uint64_t rtype_hash, uint64_t element_rtype_hash, uint64_t length, uint64_t capacity);

n_cptr_t vec_element_addr(n_vec_t *l, uint64_t index);

/**
 * 返回 index 对应的 array 处的内存位置
 * @param l
 * @param index
 * @return
 */
void vec_access(n_vec_t *l, uint64_t index, void *value_ref);

void vec_assign(n_vec_t *l, uint64_t index, void *ref);

/**
 * @param l
 * @return
 */
uint64_t vec_length(n_vec_t *l);

uint64_t vec_capacity(n_vec_t *l);

void *vec_ref(n_vec_t *l);

/**
 * 将 reference 处的值通过 memmove 移动 element_size 个字节到 array offest 中
 * @param l
 * @param value
 */
void vec_push(n_vec_t *l, void *ref);

n_cptr_t vec_iterator(n_vec_t *l);

/**
 * slice 不会修改原数组
 * 从数组 a 中截取 [start, end) 的内容, 不包含 end
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return 返回切片后的数据
 */
n_vec_t *vec_slice(uint64_t rtype_hash, n_vec_t *l, int64_t start, int64_t end);

/**
 * 合并 a 和 b 两个 vec 到一个新的 vec 中
 * @param a
 * @param b
 * @return
 */
n_vec_t *vec_concat(uint64_t rtype_hash, n_vec_t *a, n_vec_t *b);

#endif //NATURE_VEC_H

#ifndef NATURE_VEC_H
#define NATURE_VEC_H

#include "utils/type.h"
#include <stdint.h>

#define VEC_DEFAULT_CAPACITY 8

/**
 * rti = runtime_internal
 * rt = runtime
 * @param ele_rhash
 * @param length
 * @param capacity
 * @return
 */
n_vec_t *rti_vec_new(rtype_t *ele_rtype, int64_t length, int64_t capacity);

n_vec_t *rt_vec_new(int64_t rhash, int64_t ele_rhash, int64_t length, void *value_ref);

n_vec_t *rt_vec_cap(int64_t rhash, int64_t ele_rhash, int64_t capacity);

n_anyptr_t rt_vec_element_addr(n_vec_t *l, uint64_t index);

/**
 * 返回 index 对应的 array 处的内存位置
 * @param l
 * @param index
 * @return
 */
void rt_vec_access(n_vec_t *l, uint64_t index, void *value_ref);

void rt_vec_assign(n_vec_t *l, uint64_t index, void *ref);

/**
 * @param l
 * @return
 */
uint64_t rt_vec_length(n_vec_t *l);

uint64_t rt_vec_capacity(n_vec_t *l);

void *rt_vec_ref(n_vec_t *l);

void rt_vec_append(n_vec_t *dst, n_vec_t *src);

/**
 * 将 reference 处的值通过 memmove 移动 element_size 个字节到 array offest 中
 * @param vec
 * @param value
 */
void rt_vec_push(n_vec_t *vec, void *ref);

n_anyptr_t rt_vec_iterator(n_vec_t *l);

/**
 * slice 不会修改原数组
 * 从数组 a 中截取 [start, end) 的内容, 不包含 end
 * start to end (end not included)
 * @param a
 * @param start
 * @param end
 * @return 返回切片后的数据
 */
n_vec_t *rt_vec_slice(n_vec_t *l, int64_t start, int64_t end);

/**
 * 合并 a 和 b 两个 vec 到一个新的 vec 中
 * @param a
 * @param b
 * @return
 */
n_vec_t *rt_vec_concat(n_vec_t *a, n_vec_t *b);

uint64_t rt_vec_copy(n_vec_t *dst, n_vec_t *src);

#endif//NATURE_VEC_H

#ifndef NATURE_BASIC_H
#define NATURE_BASIC_H

#include "utils/type.h"

#define POINTER_SIZE sizeof(void*)
#define INT_SIZE sizeof(int64_t)

void union_assert(memory_union_t *mu, int64_t target_rtype_index, void *value_ref);

bool union_is(memory_union_t *mu, int64_t target_rtype_index);

/**
 * input rtype index 是 value 具体的类型
 * @param input_rtype_index
 * @param value_ref
 * @return
 */
memory_union_t *union_casting(uint64_t input_rtype_index, void *value_ref);

void number_casting(uint64_t input_rtype_index, void *input_ref, uint64_t output_rtype_index, void *output_ref);

memory_bool_t bool_casting(uint64_t input_rtype_index, int64_t int_value, double float_value);

int64_t iterator_next_key(void *iterator, uint64_t rtype_index, int64_t cursor, void *key_ref);

void iterator_value(void *iterator, uint64_t rtype_index, int64_t cursor, void *value_ref);

void memory_move(uint8_t *dst, uint64_t dst_offset, void *src, uint64_t src_offset, uint64_t size);

void zero_fn();

#endif //NATURE_BASIC_H

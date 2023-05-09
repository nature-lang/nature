#ifndef NATURE_BASIC_H
#define NATURE_BASIC_H

#include "utils/type.h"

#define POINTER_SIZE sizeof(void*)
#define INT_SIZE sizeof(int64_t)

/**
 * compiler 在面对 number 类型的转换时，其实什么都不需要做？改类型就可以了
 * any 都则需要将 type 和 value 封装在一起并做值替换
 * @param target_index
 * @param value_ref
 */
memory_any_t *convert_any(uint64_t input_rtype_index, void *value_ref);

void number_casting(uint64_t input_rtype_index, void *input_ref, uint64_t output_rtype_index, void *output_ref);

memory_bool_t convert_bool(uint64_t input_rtype_index, int64_t int_value, double float_value);

int64_t iterator_next_key(void *iterator, uint64_t rtype_index, int64_t cursor, void *key_ref);

void iterator_value(void *iterator, uint64_t rtype_index, int64_t cursor, void *value_ref);

void memory_move(byte *dst, uint64_t dst_offset, void *src, uint64_t src_offset, uint64_t size);

#endif //NATURE_BASIC_H

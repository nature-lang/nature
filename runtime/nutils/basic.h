#ifndef NATURE_BASIC_H
#define NATURE_BASIC_H

#include "utils/type.h"
#include "list.h"

#define INT_SIZE sizeof(int64_t)

int command_argc;
char **command_argv;

void union_assert(n_union_t *mu, int64_t target_rtype_hash, void *value_ref);

bool union_is(n_union_t *mu, int64_t target_rtype_hash);

/**
 * input rtype index 是 value 具体的类型
 * @param input_rtype_hash
 * @param value_ref
 * @return
 */
n_union_t *union_casting(uint64_t input_rtype_hash, void *value_ref);

void number_casting(uint64_t input_rtype_hash, void *input_ref, uint64_t output_rtype_hash, void *output_ref);

n_bool_t bool_casting(uint64_t input_rtype_hash, int64_t int_value, double float_value);

int64_t iterator_next_key(void *iterator, uint64_t rtype_hash, int64_t cursor, void *key_ref);

int64_t iterator_next_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref);

void iterator_take_value(void *iterator, uint64_t rtype_hash, int64_t cursor, void *value_ref);

void memory_move(uint8_t *dst, uint64_t dst_offset, void *src, uint64_t src_offset, uint64_t size);

void zero_fn();

void processor_throw_errort(n_string_t *msg, char *path, char *fn_name, n_int_t line, n_int_t column);

n_errort processor_remove_errort();

uint8_t processor_has_errort(char *path, char *fn_name, n_int_t line, n_int_t column);

n_list_t *string_to_list(n_string_t *str);

n_string_t *list_to_string(n_list_t *list);

n_cptr_t cptr_casting(value_casting v);

static inline n_list_t *list_u8_new(uint64_t length, uint64_t capacity) {
    rtype_t *list_rtype = gc_rtype(TYPE_LIST, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);

    return list_new(list_rtype->hash, element_rtype->hash, length, capacity);
}

n_list_t *std_args();

#endif //NATURE_BASIC_H

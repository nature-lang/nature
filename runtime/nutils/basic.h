#ifndef NATURE_BASIC_H
#define NATURE_BASIC_H

#include "utils/type.h"
#include "vec.h"

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

void zero_fn();

void processor_throw_errort(n_string_t *msg, char *path, char *fn_name, n_int_t line, n_int_t column);

n_errort processor_remove_errort();

uint8_t processor_has_errort(char *path, char *fn_name, n_int_t line, n_int_t column);

n_cptr_t cptr_casting(value_casting v);

n_vec_t *std_args();

char *rtype_value_str(rtype_t *rtype, void *data_ref);

#endif //NATURE_BASIC_H

#ifndef NATURE_NUTILS_BASIC_H
#define NATURE_NUTILS_BASIC_H

#include "runtime/runtime.h"
#include "utils/type.h"

#define INT_SIZE sizeof(int64_t)

extern int command_argc;
extern char **command_argv;

#define ASSERT_ADDR(_addr) assertf((addr_t) _addr > 0xa000 && (addr_t) _addr <= ARENA_HINT_MAX, "addr '%p' cannot valid", _addr)

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

void co_throw_error(n_string_t *msg, char *path, char *fn_name, n_int_t line, n_int_t column);

n_error_t co_remove_error();

uint8_t co_has_error(char *path, char *fn_name, n_int_t line, n_int_t column);

uint8_t co_has_panic(bool be_catch, char *path, char *fn_name, n_int_t line, n_int_t column);

n_void_ptr_t void_ptr_casting(value_casting v);

value_casting casting_to_void_ptr(void *ptr);

n_vec_t *std_args();

char *rtype_value_str(rtype_t *rtype, void *data_ref);

void rt_write_barrier(void *slot, void *new_obj);

void write_barrier(void *slot, void *new_obj);

//rtype_t *gc_rtype(type_kind kind, uint32_t count, ...);

rtype_t *gc_rtype_array(type_kind kind, uint32_t count);

rtype_t rti_rtype_array(rtype_t *element_rtype, uint64_t length);

void raw_ptr_valid(void *raw_ptr);

void rt_panic(n_string_t *msg);

#endif // NATURE_BASIC_H

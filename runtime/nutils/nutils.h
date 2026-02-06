#ifndef NATURE_NUTILS_BASIC_H
#define NATURE_NUTILS_BASIC_H

#include "runtime/runtime.h"
#include "utils/sc_map.h"
#include "utils/type.h"

#define INT_SIZE sizeof(int64_t)

extern int command_argc;
extern char **command_argv;


#define ASSERT_ADDR(_addr) assertf((addr_t) _addr > 0xa000 && (addr_t) _addr <= 0x1000000000000, "addr '%p' cannot valid", _addr)

void union_assert(n_union_t *mu, int64_t target_rtype_hash, void *value_ref);

void interface_assert(n_interface_t *mu, int64_t target_rtype_hash, void *value_ref);

bool union_is(n_union_t *mu, int64_t target_rtype_hash);

bool interface_is(n_interface_t *mu, int64_t target_rtype_hash);

/**
 * input rtype index 是 value 具体的类型
 * @param input_rtype_hash
 * @param value_ref
 * @return
 */
n_union_t union_casting(int64_t input_rtype_hash, void *value_ref);

n_tagged_union_t *tagged_union_casting(int64_t id, int64_t value_rtype_hash, void *value_ref);

n_interface_t *interface_casting(uint64_t input_rtype_hash, void *value_ref, int64_t method_count, int64_t *methods);

int64_t iterator_next_key(void *iterator, uint64_t rtype_hash, int64_t cursor, void *key_ref);

int64_t iterator_next_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref);

void iterator_take_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref);

void co_throw_error(n_interface_t *error, char *path, char *fn_name, n_int_t line, n_int_t column);

void throw_index_out_error(n_int_t *index, n_int_t *len, n_bool_t be_catch);

n_interface_t *co_remove_error();

uint8_t co_has_error(char *path, char *fn_name, n_int_t line, n_int_t column);

uint8_t co_has_panic(bool be_catch, char *path, char *fn_name, n_int_t line, n_int_t column);

n_anyptr_t anyptr_casting(value_casting v);

value_casting casting_to_anyptr(void *ptr);

n_vec_t *std_args();

char *rtype_value_to_str(rtype_t *rtype, void *data_ref);

void rti_write_barrier_ptr(void *slot, void *new_obj, bool mark_black_new_obj);

void write_barrier(void *slot, void *new_obj);

void ptr_valid(void *ptr);

void rt_panic(n_string_t *msg);

void rt_assert(n_bool_t cond);


n_vec_t *unsafe_vec_new(int64_t hash, int64_t element_hash, int64_t len, void *data_ptr);

n_string_t *rt_strerror();

n_string_t *rt_string_ref_new(void *raw_string, int64_t length);

void *rt_string_ref(n_string_t *n_str);

n_string_t *rt_string_new(n_anyptr_t raw_string);

bool rt_in_heap(n_anyptr_t addr);

#endif // NATURE_BASIC_H

#ifndef NATURE_NUTILS_BASIC_H
#define NATURE_NUTILS_BASIC_H

#include "runtime/runtime.h"
#include "utils/sc_map.h"
#include "utils/type.h"

#define INT_SIZE sizeof(int64_t)

extern int command_argc;
extern char **command_argv;


#define ASSERT_ADDR(_addr) assertf((addr_t) _addr > 0xa000 && (addr_t) _addr <= 0x1000000000000, "addr '%p' cannot valid", _addr)

bool union_assert(n_union_t *mu, int64_t target_rtype_hash, void *value_ref, bool caught);

bool any_assert(n_any_t *mu, int64_t target_rtype_hash, void *value_ref, bool caught);

bool interface_assert(n_interface_t *mu, int64_t target_rtype_hash, void *value_ref, bool caught);

bool union_is(n_union_t *mu, int64_t target_rtype_hash);

bool any_is(n_any_t *mu, int64_t target_rtype_hash);

bool interface_is(n_interface_t *mu, int64_t target_rtype_hash);

/**
 * input rtype index 是 value 具体的类型
 * @param out
 * @param input_rtype_hash
 * @param value_ref
 */
void union_casting(n_union_t *out, int64_t input_rtype_hash, void *value_ref);

void any_casting(n_any_t *out, int64_t input_rtype_hash, void *value_ref, n_bool_t is_x);

void union_to_any(n_any_t *out, n_union_t *input, n_bool_t is_x);

void tagged_union_casting(n_tagged_union_t *out, int64_t id, int64_t value_rtype_hash, void *value_ref);

void interface_casting(n_interface_t *out, n_vtable_t *vtable, void *value_ref);

int64_t iterator_next_key(void *iterator, uint64_t rtype_hash, int64_t cursor, void *key_ref);

int64_t iterator_next_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref);

void iterator_take_value(void *iterator, int64_t hash, int64_t cursor, void *value_ref);

void co_error_trace(char *path, char *fn_name, n_int_t line, n_int_t column);

void co_throw_error(char *path, char *fn_name, n_int_t line, n_int_t column);

void throw_index_out_error(n_int_t *index, n_int_t *len);

uint8_t co_has_panic(char *path, char *fn_name, n_int_t line, n_int_t column);

n_anyptr_t anyptr_casting(value_casting v);

value_casting casting_to_anyptr(void *ptr);

n_vec_t std_args();

char *rtype_value_to_str(rtype_t *rtype, void *data_ref);

void rti_write_barrier_ptr(void *slot, void *new_obj, bool mark_black_new_obj);

void rti_write_barrier_rtype(void *dst, void *src, rtype_t *rtype);

void write_barrier(void *slot, void *new_obj);

void ptr_valid(void *ptr);

void rti_panic_at(addr_t ret_addr, char *msg);

void rt_panic(n_string_t msg);

n_string_t rt_errset_name(uint32_t errset);

n_string_t rt_errset_msg(uint32_t errset);

uint32_t rt_sys_errset(n_int_t code);

void rt_dump_stack_backtrace(void);

#ifdef __WINDOWS
void rt_install_windows_exception_handler(void);
#endif

void rt_assert(n_bool_t cond);

// allocate array data by element rtype hash
n_anyptr_t rt_array_new(int64_t element_hash, int64_t length);

n_vec_t unsafe_vec_new(int64_t hash, int64_t element_hash, int64_t len, void *data_ptr);

n_string_t rt_strerror();

n_string_t rt_string_ref_new(void *raw_string, int64_t length);

n_string_t rt_string_alloc(int64_t length);

void *rt_string_ref(n_string_t *n_str);

n_string_t rt_string_new(n_anyptr_t raw_string);

// LIR out-parameter helpers for builtin structs
void rt_string_concat_out(n_string_t *out, n_string_t *a, n_string_t *b);
void rt_string_to_vec_out(n_vec_t *out, n_string_t *src);
void rt_vec_to_string_out(n_string_t *out, n_vec_t *src);
bool rt_vec_slice_out(n_vec_t *out, n_vec_t *vec, int64_t start, int64_t end, bool caught);

bool rt_in_heap(n_anyptr_t addr);

#endif // NATURE_BASIC_H

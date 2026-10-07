#ifndef NATURE_SYSCALL_H
#define NATURE_SYSCALL_H

#include "utils/type.h"

n_int_result_t syscall_call6(n_int_t number, n_uint_t a1, n_uint_t a2, n_uint_t a3, n_uint_t a4, n_uint_t a5, n_uint_t a6);

n_void_result_t syscall_exec(n_string_t path, n_vec_t argv, n_vec_t envp);

n_u32_result_t syscall_wait(n_int_t pid);

#endif //NATURE_SYSCALL_H

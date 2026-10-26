#ifndef NAX_SYSCALL_H
#define NAX_SYSCALL_H

#include "runtime/runtime.h"
#include "utils/type.h"

n_errable_t syscall_call6(n_int_t number, n_uint_t a1, n_uint_t a2, n_uint_t a3, n_uint_t a4, n_uint_t a5, n_uint_t a6);

n_errable_t syscall_exec(n_string_t path, n_vec_t argv, n_vec_t envp);

n_u32_t syscall_wait(n_int_t pid);

#endif //NAX_SYSCALL_H

#ifndef NATURE_SYSCALL_H
#define NATURE_SYSCALL_H

#include "utils/type.h"

n_int_t syscall_call6(n_int_t number, n_uint_t a1, n_uint_t a2, n_uint_t a3, n_uint_t a4, n_uint_t a5, n_uint_t a6);

void syscall_exec(n_string_t *path, n_list_t *argv, n_list_t *envp);

n_u32_t syscall_wait(n_int_t pid);

#endif //NATURE_SYSCALL_H

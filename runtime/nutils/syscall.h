#ifndef NATURE_SYSCALL_H
#define NATURE_SYSCALL_H

#include "utils/type.h"

n_int_t syscall_open(n_string_t *filename, n_int_t flags, n_u32_t perm);

void syscall_close(n_int_t fd);

n_list_t *syscall_read(n_int_t fd, n_int_t len);

n_int_t syscall_write(n_int_t fd, n_list_t *buf);

void syscall_unlink(n_string_t *path);

n_int_t syscall_lseek(n_int_t fd, n_int_t offset, n_int_t whence);

n_int_t syscall_fork();

void syscall_exec(n_string_t *path, n_list_t *argv, n_list_t *envp);

n_struct_t *syscall_stat(n_string_t *filename);

n_struct_t *syscall_fstat(n_int_t fd);

#endif //NATURE_SYSCALL_H

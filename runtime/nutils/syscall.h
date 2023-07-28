#ifndef NATURE_SYSCALL_H
#define NATURE_SYSCALL_H

#include "utils/type.h"

n_int_t syscall_open(n_string_t *filename, n_int_t flags, n_u32_t perm);

void syscall_close(n_int_t fd);

n_list_t *syscall_read(n_int_t fd, n_int_t len);

n_int_t syscall_write(n_int_t fd, n_list_t *buf);

void syscall_link(n_string_t *oldpath, n_string_t *newpath);

void syscall_unlink(n_string_t *path);

n_int_t syscall_lseek(n_int_t fd, n_int_t offset, n_int_t whence);

n_int_t syscall_fork();

void syscall_exec(n_string_t *path, n_list_t *argv, n_list_t *envp);

n_struct_t *syscall_stat(n_string_t *filename);

n_struct_t *syscall_fstat(n_int_t fd);

void syscall_mkdir(n_string_t *path, n_u32_t mode);

void syscall_rmdir(n_string_t *path);

void syscall_rename(n_string_t *oldpath, n_string_t *newpath);

void syscall_exit(n_int_t status);

n_int_t syscall_getpid();

n_int_t syscall_getppid();

void syscall_kill(n_int_t pid, n_int_t sig);

n_u32_t syscall_wait(n_int_t pid);

void syscall_chdir(n_string_t *path);

void syscall_chroot(n_string_t *path);

void syscall_chown(n_string_t *path, n_int_t uid, n_int_t gid);

void syscall_chmod(n_string_t *path, n_u32_t mode);

n_string_t *syscall_getcwd();

#endif //NATURE_SYSCALL_H

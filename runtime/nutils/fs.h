#ifndef NATURE_FS_H
#define NATURE_FS_H

#include "runtime/processor.h"
#include "utils/helper.h"
#include "uv.h"

#define BUFFER_SIZE 4096

typedef struct {
    n_int_t fd;
    char *data;
    n_int64_t data_len;
    n_int64_t data_cap;
    bool closed;

    uv_fs_t req; // 对同一个文件的所有的操作都共用该 req, 不同文件操作之间通过 fd 进行关联
    uv_buf_t buf;
    int64_t flags;
    int64_t mode;
    int64_t offset; // 用于 fallback 时的 pread/pwrite offset
} fs_context_t;

fs_context_t *rt_uv_fs_open(n_string_t path, int64_t flags, int64_t mode);

n_int_t rt_uv_fs_read(fs_context_t *ctx, n_vec_t buf);

n_int_t rt_uv_fs_write(fs_context_t *ctx, n_vec_t buf);

void rt_uv_fs_close(fs_context_t *ctx);

n_int_t rt_uv_fs_read_at(fs_context_t *ctx, n_vec_t buf, int offset);

n_int_t rt_uv_fs_write_at(fs_context_t *ctx, n_vec_t buf, int offset);

uv_stat_t rt_uv_fs_stat(fs_context_t *ctx);

fs_context_t *rt_uv_fs_from(n_int_t fd, n_string_t name);

#endif //NATURE_FS_H

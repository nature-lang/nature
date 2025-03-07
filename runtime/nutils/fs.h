#ifndef NATURE_FS_H
#define NATURE_FS_H

#include "uv.h"
#include "utils/helper.h"
#include "runtime/processor.h"

#define BUFFER_SIZE 4096

typedef struct {
    int32_t fd;
    char* data;
    int64_t data_len;
    int64_t data_cap;

    uv_fs_t req; // 对同一个文件的所有的操作都共用该 req, 不同文件操作之间通过 fd 进行关联
    uv_buf_t buf;
} fs_context_t;

fs_context_t *rt_uv_fs_open(n_string_t *path, int64_t flags, int64_t mode);

n_string_t *rt_uv_fs_read(fs_context_t *ctx);

void rt_uv_fs_write(fs_context_t *ctx, n_string_t *data);

void rt_uv_fs_close(fs_context_t *ctx);

n_string_t *rt_uv_fs_read_at(fs_context_t *ctx, int offset, int len);

void rt_uv_fs_write_at(fs_context_t *ctx, n_string_t *data, int offset, int len);

#endif //NATURE_FS_H

#ifndef NATURE_WINDOWS_UTF8_H
#define NATURE_WINDOWS_UTF8_H

#include <stddef.h>
#include <stdint.h>

#ifdef __WINDOWS

int32_t rt_windows_open_mode(int32_t mode);
int32_t rt_windows_open_utf8(const char *path, int32_t flags, int32_t mode);
int32_t rt_windows_stat_utf8(const char *path, void *result);
int32_t rt_windows_mkdir_utf8(const char *path);
int32_t rt_windows_rmdir_utf8(const char *path);
int32_t rt_windows_unlink_utf8(const char *path);
int32_t rt_windows_rename_utf8(const char *old_path, const char *new_path);
int32_t rt_windows_chmod_utf8(const char *path, int32_t mode);
int32_t rt_windows_chdir_utf8(const char *path);

int64_t rt_windows_getcwd_utf8(char *output, size_t capacity);
int64_t rt_windows_readlink_utf8(const char *path, char *output,
                                 size_t capacity);

int64_t rt_windows_get_env_utf8(const char *key, char *output,
                                size_t capacity);
int32_t rt_windows_set_env_utf8(const char *key, const char *value);
void *rt_windows_env_first_utf8(char *entry, size_t capacity);
int32_t rt_windows_env_next_utf8(void *iterator, char *entry,
                                 size_t capacity);
int32_t rt_windows_env_close_utf8(void *iterator);

void *rt_windows_find_first_utf8(const char *pattern, char *name,
                                 size_t capacity);
int32_t rt_windows_find_next_utf8(void *iterator, char *name,
                                  size_t capacity);
int32_t rt_windows_find_close_utf8(void *iterator);

#endif

#endif

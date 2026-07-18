#ifndef NATURE_TEST_FS_H
#define NATURE_TEST_FS_H

#ifdef _WIN32
#include <direct.h>

static inline int test_make_directory(const char *path) {
    return _mkdir(path);
}
#else
#include <sys/stat.h>

static inline int test_make_directory(const char *path) {
    return mkdir(path, 0700);
}
#endif

#endif

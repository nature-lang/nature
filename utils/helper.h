#ifndef NATURE_SRC_LIB_HELPER_H_
#define NATURE_SRC_LIB_HELPER_H_

#include "value.h"
#include "utils/assertf.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#define DEBUG 1

#ifdef DEBUG
#define DEBUGF(...) printf(__VA_ARGS__);printf("\n"); fflush(stdout);
#else
#define DEBUGF(...)
#endif

#define COPY_NEW(_type, src) ({ \
    _type *dst = malloc(sizeof(_type)); \
    memcpy(dst, src, sizeof(_type));    \
    dst;                               \
})

//void string_to_unique_list(string *list, string value);

char *itoa(int64_t n);

bool str_equal(string a, string b);

char *path_join(char *dst, char *src);

char *str_connect(char *a, char *b);

void str_replace_char(char *str, char from, char to);

char *str_replace(char *orig, char *rep, char *with);

char *file_read(char *path);

int64_t align(int64_t n, int64_t align);

bool file_exists(char *path);

bool ends_with(char *str, char *suffix);

char *get_work_dir();

char *rtrim(char *str, size_t trim_len);

void *copy(char *dst, char *src, uint mode);

ssize_t full_read(int fd, void *buf, size_t count);

static inline uint16_t read16le(unsigned char *p) {
    return p[0] | (uint16_t) p[1] << 8;
}

static inline void write16le(unsigned char *p, uint16_t x) {
    p[0] = x & 255;
    p[1] = x >> 8 & 255;
}

static inline uint32_t read32le(unsigned char *p) {
    return read16le(p) | (uint32_t) read16le(p + 2) << 16;
}

static inline void write32le(unsigned char *p, uint32_t x) {
    write16le(p, x);
    write16le(p + 2, x >> 16);
}

static inline void add32le(unsigned char *p, int32_t x) {
    write32le(p, read32le(p) + x);
}

static inline uint64_t read64le(unsigned char *p) {
    return read32le(p) | (uint64_t) read32le(p + 4) << 32;
}

static inline void write64le(unsigned char *p, uint64_t x) {
    write32le(p, x);
    write32le(p + 4, x >> 32);
}

static inline void add64le(unsigned char *p, int64_t x) {
    write64le(p, read64le(p) + x);
}


static inline char *dsprintf(char *format, ...) {
    char *buf = mallocz(2000);
    va_list args;
    va_start(args, format);
    int count = vsprintf(buf, format, args);
    va_end(args);

    return realloc(buf, count + 1);
}

static inline char *fixed_sprintf(char *format, ...) {
    char *buf = mallocz(2000);
    va_list args;
    va_start(args, format);
    int count = vsprintf(buf, format, args);
    va_end(args);

    return realloc(buf, count + 1);
}

static inline int max(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

/**
 * 基于 mmap 申请一片可用且空闲的内存区域
 * 避免因为使用 calloc 造成缺页中断导致的物理内存消耗
 * @param size
 * @return
 */
void *mallocz_big(size_t size);

/**
 * 按提示的地址分配 mmap(0x1065ff000, 10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
 * @param hint
 * @param size
 * @return
 */
void *sys_memory_map(void *hint, uint64_t size);

/**
 * 卸载 map
 * @param hint
 * @param size
 * @return
 */
void sys_memory_unmap(void *base, uint64_t size);

/**
 * 将内存归还给操作系统，但是不影响 map 映射
 * memory advice
 * @param addr
 * @param size
 */
void sys_memory_remove(void *addr, uint64_t size);

#endif //NATURE_SRC_LIB_HELPER_H_

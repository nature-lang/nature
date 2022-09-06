#ifndef NATURE_SRC_LIB_HELPER_H_
#define NATURE_SRC_LIB_HELPER_H_

#include "src/value.h"
#include <unistd.h>

//void string_to_unique_list(string *list, string value);

char *itoa(int n);

bool str_equal(string a, string b);

char *file_join(char *dst, char *src);

char *str_connect(char *a, char *b);

void str_replace(char *str, char from, char to);

char *file_read(char *path);

uint64_t memory_align(uint64_t n, uint8_t align);

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

static inline void *mallocz(size_t size) {
    void *ptr;
    ptr = malloc(size);
    if (size) {
        memset(ptr, 0, size);
    }
    return ptr;
}

#endif //NATURE_SRC_LIB_HELPER_H_
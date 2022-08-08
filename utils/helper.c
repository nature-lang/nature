#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "helper.h"
#include "error.h"

char *itoa(int n) {
    // 计算长度
    int length = snprintf(NULL, 0, "%d", n);

    // 初始化 buf
    char *str = malloc(length + 1);

    // 转换
    snprintf(str, length + 1, "%d", n);

    return str;
}

bool str_equal(char *a, char *b) {
    return strcmp(a, b) == 0;
}

char *str_connect(char *dst, char *src) {
    char *buf = malloc(strlen(dst) + strlen(src) + 1);
    sprintf(buf, "%s%s", dst, src);
    return buf;
}

char *file_join(char *dst, char *src) {
    dst = str_connect(dst, "/");
    dst = str_connect(dst, src);
    return dst;
}

void str_replace(char *str, char from, char to) {
    size_t len = strlen(str);
    for (int i = 0; i < len; ++i) {
        if (str[i] == from) {
            str[i] = to;
        }
    }
}

char *file_read(char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *) malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

uint64_t memory_align(uint64_t n, uint8_t align) {
    return (n + align - 1) & (~(align - 1));
}


bool file_exists(char *path) {
    return (access(path, R_OK) == 0);
}

char *rtrim(char *str, size_t trim_len) {
    size_t len = strlen(str) - trim_len + 1; // +1 表示 \0 部分
    char *res = malloc(len);

    strncpy(res, str, len);
    res[len - 1] = '\0';

    return res;
}

char *get_work_dir() {
    int size = 256;
    char *buf = malloc(size);
    getcwd(buf, size);
    return buf;
}

void *copy(char *dst, char *src, uint mode) {
    FILE *src_fd, *dst_fd;
    src_fd = fopen(src, "rb");
    if (src_fd == NULL) {
        error_exit("[copy] src %s not found", src);
    }
    dst_fd = fopen(dst, "wb");
    fchmod(fileno(dst_fd), mode);

    char buf[1024] = {0};
    while (!feof(src_fd)) {
        size_t size = fread(buf, 1, 1024, src_fd);
        fwrite(buf, 1, size, dst_fd);
    }

    fclose(src_fd);
    fclose(dst_fd);
    return 0;
}

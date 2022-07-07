#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void str_replace(char *str, char from, char to) {
    size_t len = strlen(str);
    for (int i = 0; i < len; ++i) {
        if (str[i] == from) {
            str[i] = to;
        }
    }
}
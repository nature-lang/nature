#include "helper.h"

// 辅助函数：检查字符是否为路径分隔符
static bool is_separator(char c) {
    return c == '/' || c == '\\';
}

char *path_clean(const char *path) {
    if (path == NULL) return NULL;

    char *result = malloc(PATH_MAX);
    if (result == NULL) return NULL;

    const char *src = path;
    char *dst = result;
    char *last_slash = NULL;
    bool is_absolute = is_separator(*src);

    // 保留开头的 '/'（如果存在）
    if (is_absolute) {
        *dst++ = '/';
        src++;
    }

    while (*src) {
        if (is_separator(*src)) {
            // 跳过连续的分隔符
            while (is_separator(*src)) src++;
            if (*src == '\0') break;

            if (*src == '.') {
                if (*(src + 1) == '.' && (is_separator(*(src + 2)) || *(src + 2) == '\0')) {
                    // 处理 ".."
                    if (dst > result + is_absolute) {
                        dst = last_slash ? last_slash : result + is_absolute;
                        src += 2;
                        last_slash = NULL;
                        for (char *p = dst - 1; p >= result; p--) {
                            if (is_separator(*p)) {
                                last_slash = p;
                                break;
                            }
                        }
                    } else {
                        src += 2;
                    }
                    continue;
                } else if (is_separator(*(src + 1)) || *(src + 1) == '\0') {
                    // 跳过 "."
                    src++;
                    continue;
                }
            }

            // 添加分隔符
            if (dst == result || !is_separator(*(dst - 1))) {
                last_slash = dst;
                *dst++ = '/';
            }
        } else {
            *dst++ = *src++;
        }
    }

    // 处理路径末尾
    if (dst > result && is_separator(*(dst - 1)) && dst - 1 != result) {
        dst--;
    }

    *dst = '\0';
    return result;
}

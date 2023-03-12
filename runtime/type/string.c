#include "string.h"

/**
 * point 就是一个堆内存的地址，谁能够在堆内存中写入一组数据？大概只有系统调用了
 * @param point
 * @param length
 * @return
 */
void *string_new(uint8_t *point, int length) {
    type_string_t *s = NEW(type_string_t);
    s->content = malloc(sizeof(uint8_t) * length);
    memcpy(s->content, point, length);
    s->length = length;
    return s;
}

void *string_addr(void *point) {
    type_string_t *s = point;
    return s->content;
}

int string_length(void *point) {
    return ((type_string_t *) point)->length;;
}

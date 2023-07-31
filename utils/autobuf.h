#ifndef NATURE_AUTOBUF_H
#define NATURE_AUTOBUF_H

#include <stdlib.h>
#include <stdint.h>

typedef struct {
    uint8_t *data; // uint8_t 保证了加法操作一次只移动一个字节
    uint64_t len;
    uint64_t cap;
} autobuf_t;

autobuf_t *autobuf_new(uint64_t cap);

/**
 * 将 element 中的内存 copy 到 buf 中
 * @param element
 * @param element_size
 * @return
 */
void autobuf_push(autobuf_t *buf, void *element, uint64_t element_size);

void autobuf_padding(autobuf_t *buf, uint64_t element_size);

void autobuf_free(autobuf_t *buf);

#endif //NATURE_AUTOBUF_H

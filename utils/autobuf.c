#include "autobuf.h"
#include "helper.h"

autobuf_t *autobuf_new(uint64_t cap) {
    autobuf_t *buf = NEW(autobuf_t);
    assertf(buf, "Memory allocation for autobuf_t failed");

    buf->data = mallocz(cap);
    assertf(buf->data, "Memory allocation for autobuf_t failed");

    buf->len = 0; // Initially, the length is zero
    buf->cap = cap; // Set the initial capacity

    return buf;
}


void autobuf_push(autobuf_t *buf, void *element, uint64_t element_size) {
    assertf(buf, "Buffer failed");

    // Check if the buffer has enough capacity, if not, reallocate the buffer
    if (buf->len + element_size > buf->cap) {
        uint64_t cap = (buf->len + element_size) * 2;

        void *new_data = realloc(buf->data, cap);
        assertf(new_data, "Memory reallocation for autobuf_t failed");
        buf->data = new_data;
        buf->cap = cap;
    }

    memcpy(buf->data + buf->len, element, element_size);
    buf->len += element_size;
}

void autobuf_free(autobuf_t *buf) {
    assertf(buf, "Buffer failed");
    free(buf->data);
    free(buf);
}

// 填充 0
void autobuf_padding(autobuf_t *buf, uint64_t element_size) {
    uint8_t zero = 0;
    for (int i = 0; i < element_size; ++i) {
        autobuf_push(buf, &zero, 1);
    }
}

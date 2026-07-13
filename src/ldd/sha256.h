#ifndef NATURE_LDD_SHA256_H
#define NATURE_LDD_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_len;
} ldd_sha256_t;

void ldd_sha256_init(ldd_sha256_t *ctx);
void ldd_sha256_update(ldd_sha256_t *ctx, const void *data, size_t size);
void ldd_sha256_final(ldd_sha256_t *ctx, uint8_t digest[32]);
void ldd_sha256(const void *data, size_t size, uint8_t digest[32]);

#endif

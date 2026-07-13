#ifndef NATURE_LD_SHA256_H
#define NATURE_LD_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_len;
} ld_sha256_t;

void ld_sha256_init(ld_sha256_t *ctx);
void ld_sha256_update(ld_sha256_t *ctx, const void *data, size_t size);
void ld_sha256_final(ld_sha256_t *ctx, uint8_t digest[32]);
void ld_sha256(const void *data, size_t size, uint8_t digest[32]);

#endif

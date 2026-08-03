#ifndef NATURE_LD_ELF_ZLIB_H
#define NATURE_LD_ELF_ZLIB_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LD_ELF_ZLIB_OK = 0,
    LD_ELF_ZLIB_INVALID_ARGUMENT,
    LD_ELF_ZLIB_ALLOCATION_FAILED,
    LD_ELF_ZLIB_OUTPUT_TOO_LARGE,
    LD_ELF_ZLIB_TRUNCATED,
    LD_ELF_ZLIB_BAD_HEADER,
    LD_ELF_ZLIB_PRESET_DICTIONARY,
    LD_ELF_ZLIB_INVALID_BLOCK_TYPE,
    LD_ELF_ZLIB_STORED_LENGTH_MISMATCH,
    LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER,
    LD_ELF_ZLIB_OVERSUBSCRIBED_HUFFMAN,
    LD_ELF_ZLIB_INCOMPLETE_HUFFMAN,
    LD_ELF_ZLIB_MISSING_END_OF_BLOCK,
    LD_ELF_ZLIB_INVALID_CODE,
    LD_ELF_ZLIB_INVALID_DISTANCE,
    LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH,
    LD_ELF_ZLIB_CHECKSUM_MISMATCH,
    LD_ELF_ZLIB_TRAILING_DATA,
} ld_elf_zlib_result_t;

typedef void *(*ld_elf_zlib_allocate_fn)(void *context, size_t size);
typedef void (*ld_elf_zlib_deallocate_fn)(void *context, void *allocation);

typedef struct {
    ld_elf_zlib_allocate_fn allocate;
    ld_elf_zlib_deallocate_fn deallocate;
    void *context;
} ld_elf_zlib_allocator_t;

/*
 * Decompress one RFC 1950 zlib stream containing RFC 1951 DEFLATE data.
 * expected_size is the uncompressed size supplied by Elf64_Chdr.ch_size.
 *
 * On success, *output is owned by the selected allocator and *output_size is
 * exactly expected_size.  A zero-length result is represented by NULL/0.
 * On failure, the function releases all temporary storage and publishes
 * NULL/0, so callers never observe partially decompressed data.
 */
ld_elf_zlib_result_t ld_elf_zlib_decompress(
        const uint8_t *input, size_t input_size, uint64_t expected_size,
        uint8_t **output, size_t *output_size);

/* The allocator variant exists for deterministic allocation-failure tests. */
ld_elf_zlib_result_t ld_elf_zlib_decompress_with_allocator(
        const uint8_t *input, size_t input_size, uint64_t expected_size,
        const ld_elf_zlib_allocator_t *allocator, uint8_t **output,
        size_t *output_size);

const char *ld_elf_zlib_result_string(ld_elf_zlib_result_t result);

#endif

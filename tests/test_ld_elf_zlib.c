#include "src/ld/ld_elf_zlib.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bytes[1024];
    size_t bit_position;
} test_zlib_bit_writer_t;

typedef struct {
    size_t allocations;
    size_t deallocations;
    int fail_allocation;
} test_zlib_allocator_state_t;

static uint32_t test_zlib_adler32(const uint8_t *data, size_t size) {
    uint32_t first = 1U;
    uint32_t second = 0U;
    while (size != 0U) {
        size_t chunk = size < 5552U ? size : 5552U;
        size -= chunk;
        for (size_t i = 0; i < chunk; i++) {
            first += *data++;
            second += first;
        }
        first %= 65521U;
        second %= 65521U;
    }
    return second << 16U | first;
}

static void test_zlib_writer_init(test_zlib_bit_writer_t *writer) {
    memset(writer, 0, sizeof(*writer));
    writer->bytes[0] = 0x78U;
    writer->bytes[1] = 0x9cU;
    writer->bit_position = 16U;
}

static void test_zlib_write_bits(test_zlib_bit_writer_t *writer,
                                 uint32_t value, unsigned count) {
    assert(writer && count <= 16U);
    for (unsigned i = 0; i < count; i++) {
        size_t byte_index = writer->bit_position / 8U;
        unsigned bit_index = (unsigned) (writer->bit_position % 8U);
        assert(byte_index < sizeof(writer->bytes));
        writer->bytes[byte_index] |=
                (uint8_t) (((value >> i) & 1U) << bit_index);
        writer->bit_position++;
    }
}

static uint32_t test_zlib_reverse_bits(uint32_t value, unsigned count) {
    uint32_t reversed = 0U;
    for (unsigned i = 0; i < count; i++) {
        reversed = reversed << 1U | (value & 1U);
        value >>= 1U;
    }
    return reversed;
}

static void test_zlib_write_fixed_symbol(test_zlib_bit_writer_t *writer,
                                         uint16_t symbol) {
    uint32_t code;
    unsigned bits;
    if (symbol <= 143U) {
        code = 0x30U + symbol;
        bits = 8U;
    } else if (symbol <= 255U) {
        code = 0x190U + symbol - 144U;
        bits = 9U;
    } else if (symbol <= 279U) {
        code = symbol - 256U;
        bits = 7U;
    } else {
        assert(symbol <= 287U);
        code = 0xc0U + symbol - 280U;
        bits = 8U;
    }
    test_zlib_write_bits(writer, test_zlib_reverse_bits(code, bits), bits);
}

static void test_zlib_write_fixed_distance(test_zlib_bit_writer_t *writer,
                                           uint16_t symbol) {
    assert(symbol <= 31U);
    test_zlib_write_bits(writer, test_zlib_reverse_bits(symbol, 5U), 5U);
}

static size_t test_zlib_finish_stream(test_zlib_bit_writer_t *writer,
                                      const uint8_t *output,
                                      size_t output_size) {
    writer->bit_position = (writer->bit_position + 7U) & ~(size_t) 7U;
    size_t byte_position = writer->bit_position / 8U;
    assert(byte_position <= sizeof(writer->bytes) - 4U);
    uint32_t checksum = test_zlib_adler32(output, output_size);
    writer->bytes[byte_position++] = (uint8_t) (checksum >> 24U);
    writer->bytes[byte_position++] = (uint8_t) (checksum >> 16U);
    writer->bytes[byte_position++] = (uint8_t) (checksum >> 8U);
    writer->bytes[byte_position++] = (uint8_t) checksum;
    return byte_position;
}

static void test_zlib_expect_success(const uint8_t *stream,
                                     size_t stream_size,
                                     const uint8_t *expected,
                                     size_t expected_size) {
    uint8_t *output = (uint8_t *) (uintptr_t) 1U;
    size_t output_size = SIZE_MAX;
    assert(ld_elf_zlib_decompress(stream, stream_size, expected_size,
                                  &output, &output_size) ==
           LD_ELF_ZLIB_OK);
    assert(output_size == expected_size);
    if (expected_size == 0U) {
        assert(output == NULL);
    } else {
        assert(output != NULL);
        assert(memcmp(output, expected, expected_size) == 0);
    }
    free(output);
}

static void test_zlib_expect_failure(const uint8_t *stream,
                                     size_t stream_size,
                                     uint64_t expected_size,
                                     ld_elf_zlib_result_t expected_result) {
    uint8_t *output = (uint8_t *) (uintptr_t) 1U;
    size_t output_size = SIZE_MAX;
    assert(ld_elf_zlib_decompress(stream, stream_size, expected_size,
                                  &output, &output_size) ==
           expected_result);
    assert(output == NULL && output_size == 0U);
}

static void test_zlib_block_types(void) {
    static const uint8_t expected[] = "Hello world\n";
    static const uint8_t stored[] = {
            0x78U,
            0x9cU,
            0x01U,
            0x0cU,
            0x00U,
            0xf3U,
            0xffU,
            'H',
            'e',
            'l',
            'l',
            'o',
            ' ',
            'w',
            'o',
            'r',
            'l',
            'd',
            0x0aU,
            0x1cU,
            0xf2U,
            0x04U,
            0x47U,
    };
    static const uint8_t fixed[] = {
            0x78U,
            0x9cU,
            0xf3U,
            0x48U,
            0xcdU,
            0xc9U,
            0xc9U,
            0x57U,
            0x28U,
            0xcfU,
            0x2fU,
            0xcaU,
            0x49U,
            0xe1U,
            0x02U,
            0x00U,
            0x1cU,
            0xf2U,
            0x04U,
            0x47U,
    };
    static const uint8_t dynamic[] = {
            0x78U,
            0x9cU,
            0x3dU,
            0xc6U,
            0x39U,
            0x11U,
            0x00U,
            0x00U,
            0x0cU,
            0x02U,
            0x30U,
            0x2bU,
            0xb5U,
            0x52U,
            0x1eU,
            0xffU,
            0x96U,
            0x38U,
            0x16U,
            0x96U,
            0x5cU,
            0x1eU,
            0x94U,
            0xcbU,
            0x6dU,
            0x01U,
            0x30U,
            0x33U,
            0x04U,
            0xd3U,
    };
    static const uint8_t dynamic_expected[] = "ABCDEABCD ABCDEABCD";

    test_zlib_expect_success(stored, sizeof(stored), expected,
                             sizeof(expected) - 1U);
    test_zlib_expect_success(fixed, sizeof(fixed), expected,
                             sizeof(expected) - 1U);
    test_zlib_expect_success(dynamic, sizeof(dynamic), dynamic_expected,
                             sizeof(dynamic_expected) - 1U);
}

static void test_zlib_empty_stream(void) {
    static const uint8_t empty[] = {
            0x78U,
            0x9cU,
            0x03U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x01U,
    };
    test_zlib_expect_success(empty, sizeof(empty), NULL, 0U);
}

static void test_zlib_32k_window(void) {
    enum {
        PREFIX_A_COUNT = 32767,
        EXPECTED_SIZE = 32771,
    };
    uint8_t expected[EXPECTED_SIZE];
    memset(expected, 'A', PREFIX_A_COUNT);
    expected[PREFIX_A_COUNT] = 'B';
    memset(expected + PREFIX_A_COUNT + 1U, 'A', 3U);

    test_zlib_bit_writer_t writer;
    test_zlib_writer_init(&writer);
    test_zlib_write_bits(&writer, 3U, 3U); /* final fixed block */
    test_zlib_write_fixed_symbol(&writer, 'A');
    for (size_t i = 0; i < 127U; i++) {
        test_zlib_write_fixed_symbol(&writer, 285U); /* length 258 */
        test_zlib_write_fixed_distance(&writer, 0U); /* distance 1 */
    }
    test_zlib_write_fixed_symbol(&writer, 'B');
    test_zlib_write_fixed_symbol(&writer, 257U); /* length 3 */
    test_zlib_write_fixed_distance(&writer, 29U);
    test_zlib_write_bits(&writer, 8191U, 13U); /* distance 32768 */
    test_zlib_write_fixed_symbol(&writer, 256U);
    size_t stream_size =
            test_zlib_finish_stream(&writer, expected, sizeof(expected));

    test_zlib_expect_success(writer.bytes, stream_size, expected,
                             sizeof(expected));
}

static void test_zlib_header_and_container_failures(void) {
    static const uint8_t invalid_check[] = {0x78U, 0x9dU};
    static const uint8_t dictionary[] = {0x78U, 0x20U};
    static const uint8_t invalid_block[] = {
            0x78U,
            0x9cU,
            0x07U,
            0x00U,
            0x00U,
            0x00U,
            0x01U,
    };
    static const uint8_t truncated[] = {0x78U};
    test_zlib_expect_failure(invalid_check, sizeof(invalid_check), 0U,
                             LD_ELF_ZLIB_BAD_HEADER);
    test_zlib_expect_failure(dictionary, sizeof(dictionary), 0U,
                             LD_ELF_ZLIB_PRESET_DICTIONARY);
    test_zlib_expect_failure(invalid_block, sizeof(invalid_block), 0U,
                             LD_ELF_ZLIB_INVALID_BLOCK_TYPE);
    test_zlib_expect_failure(truncated, sizeof(truncated), 0U,
                             LD_ELF_ZLIB_TRUNCATED);

    uint8_t *output = (uint8_t *) (uintptr_t) 1U;
    size_t output_size = SIZE_MAX;
    assert(ld_elf_zlib_decompress(NULL, 0U, 0U, &output, &output_size) ==
           LD_ELF_ZLIB_INVALID_ARGUMENT);
    assert(output == NULL && output_size == 0U);
    assert(ld_elf_zlib_decompress(invalid_check, sizeof(invalid_check), 0U,
                                  NULL, &output_size) ==
           LD_ELF_ZLIB_INVALID_ARGUMENT);
}

static void test_zlib_stored_and_size_failures(void) {
    static const uint8_t fixed[] = {
            0x78U,
            0x9cU,
            0xf3U,
            0x48U,
            0xcdU,
            0xc9U,
            0xc9U,
            0x57U,
            0x28U,
            0xcfU,
            0x2fU,
            0xcaU,
            0x49U,
            0xe1U,
            0x02U,
            0x00U,
            0x1cU,
            0xf2U,
            0x04U,
            0x47U,
    };
    static const uint8_t bad_stored_length[] = {
            0x78U,
            0x9cU,
            0x01U,
            0x01U,
            0x00U,
            0x00U,
            0x00U,
            'x',
            0x00U,
            0x00U,
            0x00U,
            0x01U,
    };
    test_zlib_expect_failure(bad_stored_length,
                             sizeof(bad_stored_length), 1U,
                             LD_ELF_ZLIB_STORED_LENGTH_MISMATCH);
    test_zlib_expect_failure(fixed, sizeof(fixed), 11U,
                             LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH);
    test_zlib_expect_failure(fixed, sizeof(fixed), 13U,
                             LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH);
    test_zlib_expect_failure(fixed, sizeof(fixed) - 2U, 12U,
                             LD_ELF_ZLIB_TRUNCATED);
}

static void test_zlib_huffman_failures(void) {
    test_zlib_bit_writer_t writer;
    test_zlib_writer_init(&writer);
    test_zlib_write_bits(&writer, 5U, 3U); /* final dynamic block */
    test_zlib_write_bits(&writer, 0U, 5U); /* 257 literals */
    test_zlib_write_bits(&writer, 0U, 5U); /* one distance */
    test_zlib_write_bits(&writer, 0U, 4U); /* four code lengths */
    for (size_t i = 0; i < 4U; i++) {
        test_zlib_write_bits(&writer, 1U, 3U);
    }
    size_t size = test_zlib_finish_stream(&writer, NULL, 0U);
    test_zlib_expect_failure(writer.bytes, size, 0U,
                             LD_ELF_ZLIB_OVERSUBSCRIBED_HUFFMAN);

    test_zlib_writer_init(&writer);
    test_zlib_write_bits(&writer, 5U, 3U);
    test_zlib_write_bits(&writer, 0U, 5U);
    test_zlib_write_bits(&writer, 0U, 5U);
    test_zlib_write_bits(&writer, 0U, 4U);
    test_zlib_write_bits(&writer, 2U, 3U);
    test_zlib_write_bits(&writer, 0U, 3U);
    test_zlib_write_bits(&writer, 0U, 3U);
    test_zlib_write_bits(&writer, 0U, 3U);
    size = test_zlib_finish_stream(&writer, NULL, 0U);
    test_zlib_expect_failure(writer.bytes, size, 0U,
                             LD_ELF_ZLIB_INCOMPLETE_HUFFMAN);
}

static void test_zlib_invalid_distance(void) {
    test_zlib_bit_writer_t writer;
    test_zlib_writer_init(&writer);
    test_zlib_write_bits(&writer, 3U, 3U);
    test_zlib_write_fixed_symbol(&writer, 257U); /* length 3 */
    test_zlib_write_fixed_distance(&writer, 0U); /* no history */
    test_zlib_write_fixed_symbol(&writer, 256U);
    size_t size = test_zlib_finish_stream(&writer, NULL, 0U);
    test_zlib_expect_failure(writer.bytes, size, 3U,
                             LD_ELF_ZLIB_INVALID_DISTANCE);
}

static void test_zlib_checksum_and_trailing_data(void) {
    static const uint8_t fixed[] = {
            0x78U,
            0x9cU,
            0xf3U,
            0x48U,
            0xcdU,
            0xc9U,
            0xc9U,
            0x57U,
            0x28U,
            0xcfU,
            0x2fU,
            0xcaU,
            0x49U,
            0xe1U,
            0x02U,
            0x00U,
            0x1cU,
            0xf2U,
            0x04U,
            0x46U,
    };
    uint8_t trailing[sizeof(fixed) + 1U];
    memcpy(trailing, fixed, sizeof(fixed));
    trailing[sizeof(fixed) - 1U] = 0x47U;
    trailing[sizeof(fixed)] = 0U;

    test_zlib_expect_failure(fixed, sizeof(fixed), 12U,
                             LD_ELF_ZLIB_CHECKSUM_MISMATCH);
    test_zlib_expect_failure(trailing, sizeof(trailing), 12U,
                             LD_ELF_ZLIB_TRAILING_DATA);
}

static void *test_zlib_allocate(void *context, size_t size) {
    test_zlib_allocator_state_t *state = context;
    state->allocations++;
    if (state->fail_allocation) return NULL;
    return malloc(size);
}

static void test_zlib_deallocate(void *context, void *allocation) {
    test_zlib_allocator_state_t *state = context;
    state->deallocations++;
    free(allocation);
}

static void test_zlib_allocator_failures_are_atomic(void) {
    static const uint8_t fixed[] = {
            0x78U,
            0x9cU,
            0xf3U,
            0x48U,
            0xcdU,
            0xc9U,
            0xc9U,
            0x57U,
            0x28U,
            0xcfU,
            0x2fU,
            0xcaU,
            0x49U,
            0xe1U,
            0x02U,
            0x00U,
            0x1cU,
            0xf2U,
            0x04U,
            0x46U,
    };
    test_zlib_allocator_state_t state = {.fail_allocation = 1};
    ld_elf_zlib_allocator_t allocator = {
            .allocate = test_zlib_allocate,
            .deallocate = test_zlib_deallocate,
            .context = &state,
    };
    uint8_t *output = (uint8_t *) (uintptr_t) 1U;
    size_t output_size = SIZE_MAX;
    assert(ld_elf_zlib_decompress_with_allocator(
                   fixed, sizeof(fixed), 12U, &allocator, &output,
                   &output_size) == LD_ELF_ZLIB_ALLOCATION_FAILED);
    assert(output == NULL && output_size == 0U);
    assert(state.allocations == 1U && state.deallocations == 0U);

    state = (test_zlib_allocator_state_t) {0};
    allocator.context = &state;
    output = (uint8_t *) (uintptr_t) 1U;
    output_size = SIZE_MAX;
    assert(ld_elf_zlib_decompress_with_allocator(
                   fixed, sizeof(fixed), 12U, &allocator, &output,
                   &output_size) == LD_ELF_ZLIB_CHECKSUM_MISMATCH);
    assert(output == NULL && output_size == 0U);
    assert(state.allocations == 1U && state.deallocations == 1U);
}

static void test_zlib_damaged_stream_smoke(void) {
    uint32_t random = UINT32_C(0x6d2b79f5);
    for (size_t iteration = 0; iteration < 2000U; iteration++) {
        uint8_t stream[80];
        stream[0] = 0x78U;
        stream[1] = 0x9cU;
        for (size_t i = 2U; i < sizeof(stream); i++) {
            random ^= random << 13U;
            random ^= random >> 17U;
            random ^= random << 5U;
            stream[i] = (uint8_t) random;
        }

        uint8_t *output = (uint8_t *) (uintptr_t) 1U;
        size_t output_size = SIZE_MAX;
        ld_elf_zlib_result_t result = ld_elf_zlib_decompress(
                stream, sizeof(stream), random & 127U, &output, &output_size);
        assert(result >= LD_ELF_ZLIB_OK &&
               result <= LD_ELF_ZLIB_TRAILING_DATA);
        if (result == LD_ELF_ZLIB_OK) {
            assert(output_size == (random & 127U));
            free(output);
        } else {
            assert(output == NULL && output_size == 0U);
        }
    }
}

void test_ld_elf_zlib(void) {
    test_zlib_block_types();
    test_zlib_empty_stream();
    test_zlib_32k_window();
    test_zlib_header_and_container_failures();
    test_zlib_stored_and_size_failures();
    test_zlib_huffman_failures();
    test_zlib_invalid_distance();
    test_zlib_checksum_and_trailing_data();
    test_zlib_allocator_failures_are_atomic();
    test_zlib_damaged_stream_smoke();
    assert(strcmp(ld_elf_zlib_result_string(
                          LD_ELF_ZLIB_CHECKSUM_MISMATCH),
                  "zlib Adler-32 checksum mismatch") == 0);
}

#ifdef LD_ELF_ZLIB_STANDALONE_TEST
int main(void) {
    test_ld_elf_zlib();
    return 0;
}
#endif

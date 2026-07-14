#include "ld_elf_zlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The DEFLATE state machine, canonical Huffman validation, and match-copy
 * rules are a portable C translation of the corresponding algorithms in
 * Zig's lib/std/compress/flate/Decompress.zig at commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224.  Zig is MIT licensed; the full
 * license is kept in src/ld/ZIG-LICENSE.txt.  This batch implementation owns
 * a fixed-size destination instead of carrying over Zig's Reader/Writer and
 * allocator abstractions.
 */

enum {
    LD_ELF_ZLIB_MAX_CODE_BITS = 15,
    LD_ELF_ZLIB_MAX_SYMBOLS = 288,
    LD_ELF_ZLIB_MAX_WINDOW = 32768,
};

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
    uint64_t bits;
    unsigned bit_count;
} ld_elf_zlib_reader_t;

typedef struct {
    uint16_t counts[LD_ELF_ZLIB_MAX_CODE_BITS + 1];
    uint16_t symbols[LD_ELF_ZLIB_MAX_SYMBOLS];
    uint16_t symbol_count;
    uint8_t max_bits;
} ld_elf_zlib_huffman_t;

typedef struct {
    ld_elf_zlib_reader_t reader;
    uint8_t *output;
    size_t output_size;
    size_t output_offset;
    uint32_t window_size;
} ld_elf_zlib_context_t;

static const uint16_t ld_elf_zlib_length_base[29] = {
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        13,
        15,
        17,
        19,
        23,
        27,
        31,
        35,
        43,
        51,
        59,
        67,
        83,
        99,
        115,
        131,
        163,
        195,
        227,
        258,
};

static const uint8_t ld_elf_zlib_length_extra[29] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        1,
        1,
        1,
        2,
        2,
        2,
        2,
        3,
        3,
        3,
        3,
        4,
        4,
        4,
        4,
        5,
        5,
        5,
        5,
        0,
};

static const uint16_t ld_elf_zlib_distance_base[30] = {
        1,
        2,
        3,
        4,
        5,
        7,
        9,
        13,
        17,
        25,
        33,
        49,
        65,
        97,
        129,
        193,
        257,
        385,
        513,
        769,
        1025,
        1537,
        2049,
        3073,
        4097,
        6145,
        8193,
        12289,
        16385,
        24577,
};

static const uint8_t ld_elf_zlib_distance_extra[30] = {
        0,
        0,
        0,
        0,
        1,
        1,
        2,
        2,
        3,
        3,
        4,
        4,
        5,
        5,
        6,
        6,
        7,
        7,
        8,
        8,
        9,
        9,
        10,
        10,
        11,
        11,
        12,
        12,
        13,
        13,
};

static void *ld_elf_zlib_default_allocate(void *context, size_t size) {
    (void) context;
    return malloc(size);
}

static void ld_elf_zlib_default_deallocate(void *context, void *allocation) {
    (void) context;
    free(allocation);
}

static ld_elf_zlib_result_t ld_elf_zlib_read_bits(
        ld_elf_zlib_reader_t *reader, unsigned count, uint32_t *value) {
    if (!reader || !value || count > 16U) {
        return LD_ELF_ZLIB_INVALID_ARGUMENT;
    }
    while (reader->bit_count < count) {
        if (reader->offset >= reader->size) return LD_ELF_ZLIB_TRUNCATED;
        reader->bits |= (uint64_t) reader->data[reader->offset++]
                        << reader->bit_count;
        reader->bit_count += 8U;
    }

    if (count == 0U) {
        *value = 0U;
        return LD_ELF_ZLIB_OK;
    }
    uint64_t mask = (UINT64_C(1) << count) - 1U;
    *value = (uint32_t) (reader->bits & mask);
    reader->bits >>= count;
    reader->bit_count -= count;
    return LD_ELF_ZLIB_OK;
}

static void ld_elf_zlib_align_reader(ld_elf_zlib_reader_t *reader) {
    unsigned discard = reader->bit_count & 7U;
    reader->bits >>= discard;
    reader->bit_count -= discard;
}

static ld_elf_zlib_result_t ld_elf_zlib_read_aligned_byte(
        ld_elf_zlib_reader_t *reader, uint8_t *value) {
    if (!reader || !value || reader->bit_count != 0U) {
        return LD_ELF_ZLIB_INVALID_ARGUMENT;
    }
    if (reader->offset >= reader->size) return LD_ELF_ZLIB_TRUNCATED;
    *value = reader->data[reader->offset++];
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_read_u16_le(
        ld_elf_zlib_reader_t *reader, uint16_t *value) {
    uint8_t low;
    uint8_t high;
    ld_elf_zlib_result_t result =
            ld_elf_zlib_read_aligned_byte(reader, &low);
    if (result != LD_ELF_ZLIB_OK) return result;
    result = ld_elf_zlib_read_aligned_byte(reader, &high);
    if (result != LD_ELF_ZLIB_OK) return result;
    *value = (uint16_t) ((uint16_t) low | (uint16_t) high << 8U);
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_build_huffman(
        ld_elf_zlib_huffman_t *tree, const uint8_t *lengths,
        size_t length_count, unsigned max_bits, bool allow_single,
        bool require_end_of_block) {
    if (!tree || !lengths || length_count > LD_ELF_ZLIB_MAX_SYMBOLS ||
        max_bits == 0U || max_bits > LD_ELF_ZLIB_MAX_CODE_BITS) {
        return LD_ELF_ZLIB_INVALID_ARGUMENT;
    }
    memset(tree, 0, sizeof(*tree));
    tree->max_bits = (uint8_t) max_bits;
    tree->symbol_count = (uint16_t) length_count;

    if (require_end_of_block &&
        (length_count <= 256U || lengths[256] == 0U)) {
        return LD_ELF_ZLIB_MISSING_END_OF_BLOCK;
    }

    unsigned maximum = 0U;
    for (size_t i = 0; i < length_count; i++) {
        unsigned length = lengths[i];
        if (length > max_bits) return LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER;
        if (length == 0U) continue;
        tree->counts[length]++;
        if (length > maximum) maximum = length;
    }
    if (maximum == 0U) return LD_ELF_ZLIB_OK;

    int32_t codes_left = 1;
    for (unsigned bits = 1U; bits <= max_bits; bits++) {
        codes_left <<= 1U;
        codes_left -= tree->counts[bits];
        if (codes_left < 0) return LD_ELF_ZLIB_OVERSUBSCRIBED_HUFFMAN;
    }
    if (codes_left > 0 &&
        !(allow_single && maximum == 1U && tree->counts[1] == 1U)) {
        return LD_ELF_ZLIB_INCOMPLETE_HUFFMAN;
    }

    uint16_t offsets[LD_ELF_ZLIB_MAX_CODE_BITS + 1] = {0};
    for (unsigned bits = 1U; bits < max_bits; bits++) {
        offsets[bits + 1U] =
                (uint16_t) (offsets[bits] + tree->counts[bits]);
    }
    for (size_t symbol = 0; symbol < length_count; symbol++) {
        unsigned length = lengths[symbol];
        if (length != 0U) {
            tree->symbols[offsets[length]++] = (uint16_t) symbol;
        }
    }
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_decode_symbol(
        ld_elf_zlib_reader_t *reader, const ld_elf_zlib_huffman_t *tree,
        uint16_t *symbol) {
    uint32_t code = 0U;
    uint32_t first = 0U;
    uint32_t index = 0U;

    for (unsigned length = 1U; length <= tree->max_bits; length++) {
        uint32_t bit;
        ld_elf_zlib_result_t result =
                ld_elf_zlib_read_bits(reader, 1U, &bit);
        if (result != LD_ELF_ZLIB_OK) return result;
        code |= bit;

        uint32_t count = tree->counts[length];
        if (code < first + count) {
            uint32_t symbol_index = index + code - first;
            if (symbol_index >= tree->symbol_count) {
                return LD_ELF_ZLIB_INVALID_CODE;
            }
            *symbol = tree->symbols[symbol_index];
            return LD_ELF_ZLIB_OK;
        }
        index += count;
        first = (first + count) << 1U;
        code <<= 1U;
    }
    return LD_ELF_ZLIB_INVALID_CODE;
}

static ld_elf_zlib_result_t ld_elf_zlib_write_literal(
        ld_elf_zlib_context_t *context, uint8_t value) {
    if (context->output_offset >= context->output_size) {
        return LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH;
    }
    context->output[context->output_offset++] = value;
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_write_match(
        ld_elf_zlib_context_t *context, uint32_t length, uint32_t distance) {
    if (distance == 0U || distance > context->window_size ||
        distance > context->output_offset) {
        return LD_ELF_ZLIB_INVALID_DISTANCE;
    }
    if (length < 3U || length > 258U) return LD_ELF_ZLIB_INVALID_CODE;
    if (length > context->output_size - context->output_offset) {
        return LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH;
    }

    for (uint32_t i = 0U; i < length; i++) {
        context->output[context->output_offset] =
                context->output[context->output_offset - distance];
        context->output_offset++;
    }
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_make_fixed_trees(
        ld_elf_zlib_huffman_t *literal_tree,
        ld_elf_zlib_huffman_t *distance_tree) {
    uint8_t literal_lengths[288];
    uint8_t distance_lengths[32];
    for (size_t i = 0; i <= 143U; i++) literal_lengths[i] = 8U;
    for (size_t i = 144U; i <= 255U; i++) literal_lengths[i] = 9U;
    for (size_t i = 256U; i <= 279U; i++) literal_lengths[i] = 7U;
    for (size_t i = 280U; i <= 287U; i++) literal_lengths[i] = 8U;
    memset(distance_lengths, 5, sizeof(distance_lengths));

    ld_elf_zlib_result_t result = ld_elf_zlib_build_huffman(
            literal_tree, literal_lengths, 288U, 15U, true, true);
    if (result != LD_ELF_ZLIB_OK) return result;
    return ld_elf_zlib_build_huffman(distance_tree, distance_lengths, 32U,
                                     15U, true, false);
}

static ld_elf_zlib_result_t ld_elf_zlib_make_dynamic_trees(
        ld_elf_zlib_reader_t *reader,
        ld_elf_zlib_huffman_t *literal_tree,
        ld_elf_zlib_huffman_t *distance_tree) {
    uint32_t encoded;
    ld_elf_zlib_result_t result =
            ld_elf_zlib_read_bits(reader, 5U, &encoded);
    if (result != LD_ELF_ZLIB_OK) return result;
    size_t literal_count = (size_t) encoded + 257U;
    result = ld_elf_zlib_read_bits(reader, 5U, &encoded);
    if (result != LD_ELF_ZLIB_OK) return result;
    size_t distance_count = (size_t) encoded + 1U;
    result = ld_elf_zlib_read_bits(reader, 4U, &encoded);
    if (result != LD_ELF_ZLIB_OK) return result;
    size_t code_length_count = (size_t) encoded + 4U;

    if (literal_count > 286U || distance_count > 30U) {
        return LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER;
    }

    static const uint8_t code_length_order[19] = {
            16,
            17,
            18,
            0,
            8,
            7,
            9,
            6,
            10,
            5,
            11,
            4,
            12,
            3,
            13,
            2,
            14,
            1,
            15,
    };
    uint8_t code_lengths[19] = {0};
    for (size_t i = 0; i < code_length_count; i++) {
        result = ld_elf_zlib_read_bits(reader, 3U, &encoded);
        if (result != LD_ELF_ZLIB_OK) return result;
        code_lengths[code_length_order[i]] = (uint8_t) encoded;
    }

    ld_elf_zlib_huffman_t code_length_tree;
    result = ld_elf_zlib_build_huffman(&code_length_tree, code_lengths, 19U,
                                       7U, false, false);
    if (result != LD_ELF_ZLIB_OK) return result;

    uint8_t lengths[286U + 30U] = {0};
    size_t total = literal_count + distance_count;
    size_t position = 0U;
    while (position < total) {
        uint16_t symbol;
        result = ld_elf_zlib_decode_symbol(reader, &code_length_tree, &symbol);
        if (result != LD_ELF_ZLIB_OK) return result;

        if (symbol <= 15U) {
            lengths[position++] = (uint8_t) symbol;
            continue;
        }

        uint32_t repeat_bits;
        size_t repeat;
        uint8_t repeated_length = 0U;
        if (symbol == 16U) {
            if (position == 0U) return LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER;
            result = ld_elf_zlib_read_bits(reader, 2U, &repeat_bits);
            if (result != LD_ELF_ZLIB_OK) return result;
            repeat = (size_t) repeat_bits + 3U;
            repeated_length = lengths[position - 1U];
        } else if (symbol == 17U) {
            result = ld_elf_zlib_read_bits(reader, 3U, &repeat_bits);
            if (result != LD_ELF_ZLIB_OK) return result;
            repeat = (size_t) repeat_bits + 3U;
        } else if (symbol == 18U) {
            result = ld_elf_zlib_read_bits(reader, 7U, &repeat_bits);
            if (result != LD_ELF_ZLIB_OK) return result;
            repeat = (size_t) repeat_bits + 11U;
        } else {
            return LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER;
        }

        if (repeat > total - position) {
            return LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER;
        }
        for (size_t i = 0; i < repeat; i++) {
            lengths[position++] = repeated_length;
        }
    }

    result = ld_elf_zlib_build_huffman(literal_tree, lengths, literal_count,
                                       15U, true, true);
    if (result != LD_ELF_ZLIB_OK) return result;
    return ld_elf_zlib_build_huffman(distance_tree,
                                     lengths + literal_count,
                                     distance_count, 15U, true, false);
}

static ld_elf_zlib_result_t ld_elf_zlib_decode_compressed_block(
        ld_elf_zlib_context_t *context,
        const ld_elf_zlib_huffman_t *literal_tree,
        const ld_elf_zlib_huffman_t *distance_tree) {
    for (;;) {
        uint16_t symbol;
        ld_elf_zlib_result_t result = ld_elf_zlib_decode_symbol(
                &context->reader, literal_tree, &symbol);
        if (result != LD_ELF_ZLIB_OK) return result;

        if (symbol < 256U) {
            result = ld_elf_zlib_write_literal(context, (uint8_t) symbol);
            if (result != LD_ELF_ZLIB_OK) return result;
            continue;
        }
        if (symbol == 256U) return LD_ELF_ZLIB_OK;
        if (symbol < 257U || symbol > 285U) {
            return LD_ELF_ZLIB_INVALID_CODE;
        }

        size_t length_index = (size_t) symbol - 257U;
        uint32_t extra = 0U;
        result = ld_elf_zlib_read_bits(
                &context->reader, ld_elf_zlib_length_extra[length_index],
                &extra);
        if (result != LD_ELF_ZLIB_OK) return result;
        uint32_t length = ld_elf_zlib_length_base[length_index] + extra;

        uint16_t distance_symbol;
        result = ld_elf_zlib_decode_symbol(&context->reader, distance_tree,
                                           &distance_symbol);
        if (result != LD_ELF_ZLIB_OK) return result;
        if (distance_symbol > 29U) return LD_ELF_ZLIB_INVALID_CODE;
        result = ld_elf_zlib_read_bits(
                &context->reader,
                ld_elf_zlib_distance_extra[distance_symbol], &extra);
        if (result != LD_ELF_ZLIB_OK) return result;
        uint32_t distance =
                ld_elf_zlib_distance_base[distance_symbol] + extra;

        result = ld_elf_zlib_write_match(context, length, distance);
        if (result != LD_ELF_ZLIB_OK) return result;
    }
}

static ld_elf_zlib_result_t ld_elf_zlib_decode_stored_block(
        ld_elf_zlib_context_t *context) {
    ld_elf_zlib_align_reader(&context->reader);
    uint16_t length;
    uint16_t inverse_length;
    ld_elf_zlib_result_t result =
            ld_elf_zlib_read_u16_le(&context->reader, &length);
    if (result != LD_ELF_ZLIB_OK) return result;
    result = ld_elf_zlib_read_u16_le(&context->reader, &inverse_length);
    if (result != LD_ELF_ZLIB_OK) return result;
    if (inverse_length != (uint16_t) ~length) {
        return LD_ELF_ZLIB_STORED_LENGTH_MISMATCH;
    }
    if ((size_t) length >
        context->output_size - context->output_offset) {
        return LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH;
    }
    if ((size_t) length >
        context->reader.size - context->reader.offset) {
        return LD_ELF_ZLIB_TRUNCATED;
    }
    if (length != 0U) {
        memcpy(context->output + context->output_offset,
               context->reader.data + context->reader.offset, length);
        context->reader.offset += length;
        context->output_offset += length;
    }
    return LD_ELF_ZLIB_OK;
}

static ld_elf_zlib_result_t ld_elf_zlib_decode_blocks(
        ld_elf_zlib_context_t *context) {
    bool final_block = false;
    while (!final_block) {
        uint32_t encoded;
        ld_elf_zlib_result_t result =
                ld_elf_zlib_read_bits(&context->reader, 1U, &encoded);
        if (result != LD_ELF_ZLIB_OK) return result;
        final_block = encoded != 0U;
        result = ld_elf_zlib_read_bits(&context->reader, 2U, &encoded);
        if (result != LD_ELF_ZLIB_OK) return result;

        if (encoded == 0U) {
            result = ld_elf_zlib_decode_stored_block(context);
        } else if (encoded == 1U || encoded == 2U) {
            ld_elf_zlib_huffman_t literal_tree;
            ld_elf_zlib_huffman_t distance_tree;
            if (encoded == 1U) {
                result = ld_elf_zlib_make_fixed_trees(&literal_tree,
                                                      &distance_tree);
            } else {
                result = ld_elf_zlib_make_dynamic_trees(
                        &context->reader, &literal_tree, &distance_tree);
            }
            if (result == LD_ELF_ZLIB_OK) {
                result = ld_elf_zlib_decode_compressed_block(
                        context, &literal_tree, &distance_tree);
            }
        } else {
            return LD_ELF_ZLIB_INVALID_BLOCK_TYPE;
        }
        if (result != LD_ELF_ZLIB_OK) return result;
    }
    return LD_ELF_ZLIB_OK;
}

static uint32_t ld_elf_zlib_adler32(const uint8_t *data, size_t size) {
    const uint32_t modulus = 65521U;
    uint32_t first = 1U;
    uint32_t second = 0U;

    while (size != 0U) {
        size_t chunk = size < 5552U ? size : 5552U;
        size -= chunk;
        for (size_t i = 0; i < chunk; i++) {
            first += *data++;
            second += first;
        }
        first %= modulus;
        second %= modulus;
    }
    return second << 16U | first;
}

static ld_elf_zlib_result_t ld_elf_zlib_finish(
        ld_elf_zlib_context_t *context) {
    ld_elf_zlib_align_reader(&context->reader);
    uint8_t checksum_bytes[4];
    for (size_t i = 0; i < sizeof(checksum_bytes); i++) {
        ld_elf_zlib_result_t result = ld_elf_zlib_read_aligned_byte(
                &context->reader, &checksum_bytes[i]);
        if (result != LD_ELF_ZLIB_OK) return result;
    }
    if (context->reader.offset != context->reader.size) {
        return LD_ELF_ZLIB_TRAILING_DATA;
    }
    if (context->output_offset != context->output_size) {
        return LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH;
    }

    uint32_t expected_checksum =
            (uint32_t) checksum_bytes[0] << 24U |
            (uint32_t) checksum_bytes[1] << 16U |
            (uint32_t) checksum_bytes[2] << 8U |
            (uint32_t) checksum_bytes[3];
    uint32_t actual_checksum =
            ld_elf_zlib_adler32(context->output, context->output_size);
    if (actual_checksum != expected_checksum) {
        return LD_ELF_ZLIB_CHECKSUM_MISMATCH;
    }
    return LD_ELF_ZLIB_OK;
}

ld_elf_zlib_result_t ld_elf_zlib_decompress_with_allocator(
        const uint8_t *input, size_t input_size, uint64_t expected_size,
        const ld_elf_zlib_allocator_t *allocator, uint8_t **output,
        size_t *output_size) {
    if (!output || !output_size) return LD_ELF_ZLIB_INVALID_ARGUMENT;
    *output = NULL;
    *output_size = 0U;
    if (!input || !allocator || !allocator->allocate ||
        !allocator->deallocate) {
        return LD_ELF_ZLIB_INVALID_ARGUMENT;
    }
#if SIZE_MAX < UINT64_MAX
    if (expected_size > SIZE_MAX) return LD_ELF_ZLIB_OUTPUT_TOO_LARGE;
#endif
    if (input_size < 2U) return LD_ELF_ZLIB_TRUNCATED;

    uint8_t cmf = input[0];
    uint8_t flg = input[1];
    if ((cmf & 0x0fU) != 8U || (cmf >> 4U) > 7U ||
        (((uint32_t) cmf << 8U) | flg) % 31U != 0U) {
        return LD_ELF_ZLIB_BAD_HEADER;
    }
    if ((flg & 0x20U) != 0U) return LD_ELF_ZLIB_PRESET_DICTIONARY;

    size_t uncompressed_size = (size_t) expected_size;
    uint8_t *buffer = NULL;
    if (uncompressed_size != 0U) {
        buffer = allocator->allocate(allocator->context, uncompressed_size);
        if (!buffer) return LD_ELF_ZLIB_ALLOCATION_FAILED;
    }

    ld_elf_zlib_context_t context = {
            .reader =
                    {
                            .data = input,
                            .size = input_size,
                            .offset = 2U,
                    },
            .output = buffer,
            .output_size = uncompressed_size,
            .window_size = UINT32_C(1) << ((cmf >> 4U) + 8U),
    };
    if (context.window_size > LD_ELF_ZLIB_MAX_WINDOW) {
        allocator->deallocate(allocator->context, buffer);
        return LD_ELF_ZLIB_BAD_HEADER;
    }

    ld_elf_zlib_result_t result = ld_elf_zlib_decode_blocks(&context);
    if (result == LD_ELF_ZLIB_OK) result = ld_elf_zlib_finish(&context);
    if (result != LD_ELF_ZLIB_OK) {
        if (buffer) allocator->deallocate(allocator->context, buffer);
        return result;
    }

    *output = buffer;
    *output_size = uncompressed_size;
    return LD_ELF_ZLIB_OK;
}

ld_elf_zlib_result_t ld_elf_zlib_decompress(
        const uint8_t *input, size_t input_size, uint64_t expected_size,
        uint8_t **output, size_t *output_size) {
    const ld_elf_zlib_allocator_t allocator = {
            .allocate = ld_elf_zlib_default_allocate,
            .deallocate = ld_elf_zlib_default_deallocate,
    };
    return ld_elf_zlib_decompress_with_allocator(
            input, input_size, expected_size, &allocator, output,
            output_size);
}

const char *ld_elf_zlib_result_string(ld_elf_zlib_result_t result) {
    switch (result) {
        case LD_ELF_ZLIB_OK:
            return "success";
        case LD_ELF_ZLIB_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_ZLIB_ALLOCATION_FAILED:
            return "allocation failed";
        case LD_ELF_ZLIB_OUTPUT_TOO_LARGE:
            return "uncompressed size cannot be represented on this host";
        case LD_ELF_ZLIB_TRUNCATED:
            return "truncated zlib stream";
        case LD_ELF_ZLIB_BAD_HEADER:
            return "invalid zlib header";
        case LD_ELF_ZLIB_PRESET_DICTIONARY:
            return "zlib preset dictionaries are unsupported";
        case LD_ELF_ZLIB_INVALID_BLOCK_TYPE:
            return "invalid DEFLATE block type";
        case LD_ELF_ZLIB_STORED_LENGTH_MISMATCH:
            return "stored DEFLATE block LEN/NLEN mismatch";
        case LD_ELF_ZLIB_INVALID_DYNAMIC_HEADER:
            return "invalid dynamic DEFLATE block header";
        case LD_ELF_ZLIB_OVERSUBSCRIBED_HUFFMAN:
            return "oversubscribed DEFLATE Huffman tree";
        case LD_ELF_ZLIB_INCOMPLETE_HUFFMAN:
            return "incomplete DEFLATE Huffman tree";
        case LD_ELF_ZLIB_MISSING_END_OF_BLOCK:
            return "literal Huffman tree has no end-of-block code";
        case LD_ELF_ZLIB_INVALID_CODE:
            return "invalid DEFLATE code";
        case LD_ELF_ZLIB_INVALID_DISTANCE:
            return "invalid DEFLATE match distance";
        case LD_ELF_ZLIB_OUTPUT_SIZE_MISMATCH:
            return "DEFLATE output size does not match Elf64_Chdr.ch_size";
        case LD_ELF_ZLIB_CHECKSUM_MISMATCH:
            return "zlib Adler-32 checksum mismatch";
        case LD_ELF_ZLIB_TRAILING_DATA:
            return "trailing data after zlib stream";
    }
    return "unknown zlib decompression result";
}

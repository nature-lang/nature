#include "ld_elf_debug.h"

#include "ld_elf_riscv_uleb.h"

#include <limits.h>
#include <string.h>

/*
 * The section filtering, tombstone rule, and relocation formulae in this file
 * are C translations of Object.skipShdr, Atom.debugTombstoneValue, and the
 * x86_64/aarch64/riscv resolveRelocNonAlloc functions in Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. Zig is distributed under the MIT
 * license; see ZIG-LICENSE.txt. Nature adds checked bounds/range validation and
 * stages every write so malformed input cannot partially modify the output.
 */

static bool ld_elf_debug_starts_with(const char *text,
                                     const char *prefix) {
    if (!text || !prefix) return false;
    while (*prefix) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

ld_elf_debug_section_kind_t ld_elf_debug_classify_nonalloc_section(
        const char *name, uint32_t type, uint64_t flags, bool keep_dwarf) {
    if (!name || (flags & LD_ELF_SHF_ALLOC) != 0U ||
        (flags & LD_ELF_SHF_EXCLUDE) != 0U ||
        type != LD_ELF_SHT_PROGBITS ||
        ld_elf_debug_starts_with(name, ".note") ||
        ld_elf_debug_starts_with(name, ".llvm_addrsig") ||
        ld_elf_debug_starts_with(name, ".riscv.attributes")) {
        return LD_ELF_DEBUG_SECTION_SKIP;
    }
    if (ld_elf_debug_starts_with(name, ".debug")) {
        return keep_dwarf ? LD_ELF_DEBUG_SECTION_DWARF
                          : LD_ELF_DEBUG_SECTION_SKIP;
    }
    if (strcmp(name, ".comment") == 0)
        return LD_ELF_DEBUG_SECTION_COMMENT;
    return LD_ELF_DEBUG_SECTION_OTHER;
}

ld_elf_debug_tombstone_t ld_elf_debug_tombstone(
        const char *relocating_section_name, bool target_discarded) {
    if (!target_discarded ||
        !ld_elf_debug_starts_with(relocating_section_name, ".debug")) {
        return LD_ELF_DEBUG_TOMBSTONE_NONE;
    }
    if (strcmp(relocating_section_name, ".debug_loc") == 0 ||
        strcmp(relocating_section_name, ".debug_ranges") == 0) {
        return LD_ELF_DEBUG_TOMBSTONE_ONE;
    }
    return LD_ELF_DEBUG_TOMBSTONE_ZERO;
}

/*
 * One additional magnitude bit is sufficient for uint64 + int64 - uint64.
 * Keeping the value in sign/magnitude form avoids implementation-defined
 * casts and does not require a host compiler with a 128-bit integer type.
 */
typedef struct {
    uint64_t magnitude;
    bool magnitude_high;
    bool negative;
} ld_elf_debug_integer_t;

static uint64_t ld_elf_debug_addend_magnitude(int64_t addend) {
    return (uint64_t) (-(addend + 1)) + 1U;
}

static void ld_elf_debug_integer_normalize(ld_elf_debug_integer_t *value) {
    if (!value->magnitude_high && value->magnitude == 0U)
        value->negative = false;
}

static void ld_elf_debug_integer_add(uint64_t base, int64_t addend,
                                     ld_elf_debug_integer_t *result) {
    memset(result, 0, sizeof(*result));
    if (addend >= 0) {
        uint64_t amount = (uint64_t) addend;
        result->magnitude = base + amount;
        result->magnitude_high = result->magnitude < base;
        return;
    }

    uint64_t amount = ld_elf_debug_addend_magnitude(addend);
    if (base >= amount) {
        result->magnitude = base - amount;
    } else {
        result->magnitude = amount - base;
        result->negative = true;
    }
    ld_elf_debug_integer_normalize(result);
}

static void ld_elf_debug_integer_subtract_unsigned(
        ld_elf_debug_integer_t *value, uint64_t subtrahend) {
    if (subtrahend == 0U) return;
    if (value->negative) {
        uint64_t previous = value->magnitude;
        value->magnitude += subtrahend;
        if (value->magnitude < previous) value->magnitude_high = true;
        return;
    }
    if (value->magnitude_high) {
        if (value->magnitude >= subtrahend) {
            value->magnitude -= subtrahend;
        } else {
            value->magnitude -= subtrahend;
            value->magnitude_high = false;
        }
    } else if (value->magnitude >= subtrahend) {
        value->magnitude -= subtrahend;
    } else {
        value->magnitude = subtrahend - value->magnitude;
        value->negative = true;
    }
    ld_elf_debug_integer_normalize(value);
}

static void ld_elf_debug_integer_expression(
        uint64_t base, int64_t addend, uint64_t subtrahend,
        ld_elf_debug_integer_t *result) {
    ld_elf_debug_integer_add(base, addend, result);
    ld_elf_debug_integer_subtract_unsigned(result, subtrahend);
}

static bool ld_elf_debug_integer_to_signed(
        const ld_elf_debug_integer_t *value, unsigned bits,
        int64_t *result) {
    if (!value || !result || bits == 0U || bits > 64U ||
        value->magnitude_high) {
        return false;
    }

    uint64_t negative_limit = UINT64_C(1) << (bits - 1U);
    uint64_t positive_limit = negative_limit - 1U;
    if (value->negative) {
        if (value->magnitude > negative_limit) return false;
        if (value->magnitude == (UINT64_C(1) << 63U)) {
            *result = INT64_MIN;
        } else {
            *result = -(int64_t) value->magnitude;
        }
    } else {
        if (value->magnitude > positive_limit) return false;
        *result = (int64_t) value->magnitude;
    }
    return true;
}

static bool ld_elf_debug_add_signed(uint64_t base, int64_t addend,
                                    unsigned bits, int64_t *result) {
    ld_elf_debug_integer_t value;
    ld_elf_debug_integer_add(base, addend, &value);
    return ld_elf_debug_integer_to_signed(&value, bits, result);
}

static bool ld_elf_debug_expression_signed(uint64_t base, int64_t addend,
                                           uint64_t subtrahend,
                                           unsigned bits, int64_t *result) {
    ld_elf_debug_integer_t value;
    ld_elf_debug_integer_expression(base, addend, subtrahend, &value);
    return ld_elf_debug_integer_to_signed(&value, bits, result);
}

static uint16_t ld_elf_debug_read_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] |
                       ((uint16_t) bytes[1] << 8U));
}

static uint32_t ld_elf_debug_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) |
           ((uint32_t) bytes[3] << 24U);
}

static uint64_t ld_elf_debug_read_u64(const uint8_t *bytes) {
    return (uint64_t) ld_elf_debug_read_u32(bytes) |
           ((uint64_t) ld_elf_debug_read_u32(bytes + 4U) << 32U);
}

static void ld_elf_debug_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void ld_elf_debug_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_debug_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_debug_write_u32(bytes, (uint32_t) value);
    ld_elf_debug_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static uint64_t ld_elf_debug_read_width(const uint8_t *bytes,
                                        unsigned bits) {
    switch (bits) {
        case 8U:
            return bytes[0];
        case 16U:
            return ld_elf_debug_read_u16(bytes);
        case 32U:
            return ld_elf_debug_read_u32(bytes);
        default:
            return ld_elf_debug_read_u64(bytes);
    }
}

static void ld_elf_debug_write_width(uint8_t *bytes, unsigned bits,
                                     uint64_t value) {
    switch (bits) {
        case 8U:
            bytes[0] = (uint8_t) value;
            break;
        case 16U:
            ld_elf_debug_write_u16(bytes, (uint16_t) value);
            break;
        case 32U:
            ld_elf_debug_write_u32(bytes, (uint32_t) value);
            break;
        default:
            ld_elf_debug_write_u64(bytes, value);
            break;
    }
}

static int64_t ld_elf_debug_sign_extend(uint64_t value, unsigned bits) {
    uint64_t sign = UINT64_C(1) << (bits - 1U);
    uint64_t mask = bits == 64U ? UINT64_MAX
                                : (UINT64_C(1) << bits) - 1U;
    value &= mask;
    if ((value & sign) == 0U) return (int64_t) value;
    uint64_t magnitude = ((~value) & mask) + 1U;
    if (bits == 64U && magnitude == (UINT64_C(1) << 63U))
        return INT64_MIN;
    return -(int64_t) magnitude;
}

static void ld_elf_debug_signed_bounds(unsigned bits, int64_t *minimum,
                                       int64_t *maximum) {
    if (bits == 64U) {
        *minimum = INT64_MIN;
        *maximum = INT64_MAX;
    } else {
        int64_t limit = INT64_C(1) << (bits - 1U);
        *minimum = -limit;
        *maximum = limit - 1;
    }
}

static int64_t ld_elf_debug_saturating_add(int64_t left, int64_t right,
                                           unsigned bits) {
    int64_t minimum;
    int64_t maximum;
    ld_elf_debug_signed_bounds(bits, &minimum, &maximum);
    if (right > 0 && left > maximum - right) return maximum;
    if (right < 0 && left < minimum - right) return minimum;
    return left + right;
}

static int64_t ld_elf_debug_saturating_subtract(int64_t left,
                                                int64_t right,
                                                unsigned bits) {
    int64_t minimum;
    int64_t maximum;
    ld_elf_debug_signed_bounds(bits, &minimum, &maximum);
    if (right > 0 && left < minimum + right) return minimum;
    if (right < 0 && left > maximum + right) return maximum;
    return left - right;
}

static ld_elf_debug_reloc_result_t ld_elf_debug_require_place(
        uint8_t *place, size_t place_size, size_t width) {
    if (!place) return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;
    if (place_size < width) return LD_ELF_DEBUG_RELOC_TRUNCATED;
    return LD_ELF_DEBUG_RELOC_OK;
}

static ld_elf_debug_reloc_result_t ld_elf_debug_apply_x86_64(
        const ld_elf_debug_relocation_t *relocation,
        const ld_elf_debug_reloc_values_t *values, uint8_t *place,
        size_t place_size, size_t *written_size) {
    size_t width;
    bool tombstone = values->has_tombstone;
    switch (relocation->type) {
        case LD_ELF_R_X86_64_8:
            width = 1U;
            break;
        case LD_ELF_R_X86_64_16:
            width = 2U;
            break;
        case LD_ELF_R_X86_64_32:
        case LD_ELF_R_X86_64_32S:
        case LD_ELF_R_X86_64_SIZE32:
            width = 4U;
            break;
        case LD_ELF_R_X86_64_64:
        case LD_ELF_R_X86_64_DTPOFF64:
        case LD_ELF_R_X86_64_GOTOFF64:
        case LD_ELF_R_X86_64_GOTPC64:
        case LD_ELF_R_X86_64_SIZE64:
            width = 8U;
            break;
        case LD_ELF_R_X86_64_DTPOFF32:
            /* Zig 738d2be9 writes the u64 tombstone in this case. */
            width = tombstone ? 8U : 4U;
            break;
        default:
            return LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION;
    }

    ld_elf_debug_reloc_result_t status =
            ld_elf_debug_require_place(place, place_size, width);
    if (status != LD_ELF_DEBUG_RELOC_OK) return status;

    uint8_t replacement[8];
    int64_t result;
    if (tombstone &&
        (relocation->type == LD_ELF_R_X86_64_64 ||
         relocation->type == LD_ELF_R_X86_64_DTPOFF32 ||
         relocation->type == LD_ELF_R_X86_64_DTPOFF64)) {
        ld_elf_debug_write_u64(replacement, values->tombstone_value);
    } else {
        unsigned bits = (unsigned) (width * 8U);
        bool valid;
        switch (relocation->type) {
            case LD_ELF_R_X86_64_DTPOFF32:
            case LD_ELF_R_X86_64_DTPOFF64:
                valid = ld_elf_debug_expression_signed(
                        values->symbol_value, relocation->addend,
                        values->dynamic_thread_pointer, bits, &result);
                break;
            case LD_ELF_R_X86_64_GOTOFF64:
                valid = ld_elf_debug_expression_signed(
                        values->symbol_value, relocation->addend,
                        values->got_address, 64U, &result);
                break;
            case LD_ELF_R_X86_64_GOTPC64:
                valid = ld_elf_debug_add_signed(
                        values->got_address, relocation->addend, 64U,
                        &result);
                break;
            case LD_ELF_R_X86_64_SIZE32:
            case LD_ELF_R_X86_64_SIZE64:
                valid = ld_elf_debug_add_signed(
                        values->symbol_size, relocation->addend, bits,
                        &result);
                break;
            default:
                valid = ld_elf_debug_add_signed(
                        values->symbol_value, relocation->addend, bits,
                        &result);
                break;
        }
        if (!valid) return LD_ELF_DEBUG_RELOC_OVERFLOW;
        ld_elf_debug_write_width(replacement, bits, (uint64_t) result);
    }

    memcpy(place, replacement, width);
    if (written_size) *written_size = width;
    return LD_ELF_DEBUG_RELOC_OK;
}

static ld_elf_debug_reloc_result_t ld_elf_debug_apply_aarch64(
        const ld_elf_debug_relocation_t *relocation,
        const ld_elf_debug_reloc_values_t *values, uint8_t *place,
        size_t place_size, size_t *written_size) {
    size_t width;
    switch (relocation->type) {
        case LD_ELF_R_AARCH64_ABS32:
            width = 4U;
            break;
        case LD_ELF_R_AARCH64_ABS64:
            width = 8U;
            break;
        default:
            return LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION;
    }
    ld_elf_debug_reloc_result_t status =
            ld_elf_debug_require_place(place, place_size, width);
    if (status != LD_ELF_DEBUG_RELOC_OK) return status;

    uint8_t replacement[8];
    if (relocation->type == LD_ELF_R_AARCH64_ABS64 &&
        values->has_tombstone) {
        ld_elf_debug_write_u64(replacement, values->tombstone_value);
    } else {
        int64_t result;
        if (!ld_elf_debug_add_signed(values->symbol_value,
                                     relocation->addend,
                                     (unsigned) (width * 8U), &result)) {
            return LD_ELF_DEBUG_RELOC_OVERFLOW;
        }
        ld_elf_debug_write_width(replacement, (unsigned) (width * 8U),
                                 (uint64_t) result);
    }
    memcpy(place, replacement, width);
    if (written_size) *written_size = width;
    return LD_ELF_DEBUG_RELOC_OK;
}

static unsigned ld_elf_debug_riscv_integer_width(uint32_t type) {
    switch (type) {
        case LD_ELF_R_RISCV_ADD8:
        case LD_ELF_R_RISCV_SUB8:
            return 8U;
        case LD_ELF_R_RISCV_ADD16:
        case LD_ELF_R_RISCV_SUB16:
            return 16U;
        case LD_ELF_R_RISCV_ADD32:
        case LD_ELF_R_RISCV_SUB32:
            return 32U;
        case LD_ELF_R_RISCV_ADD64:
        case LD_ELF_R_RISCV_SUB64:
            return 64U;
        default:
            return 0U;
    }
}

static ld_elf_debug_reloc_result_t ld_elf_debug_apply_riscv64(
        const ld_elf_debug_relocation_t *relocation,
        const ld_elf_debug_reloc_values_t *values, uint8_t *place,
        size_t place_size, size_t *written_size) {
    if (relocation->type == LD_ELF_R_RISCV_SET_ULEB128 ||
        relocation->type == LD_ELF_R_RISCV_SUB_ULEB128) {
        size_t encoded_size = 0U;
        ld_elf_riscv_uleb_result_t result = ld_elf_riscv_uleb_apply(
                place, place_size,
                relocation->type == LD_ELF_R_RISCV_SUB_ULEB128,
                values->symbol_value, relocation->addend, &encoded_size);
        switch (result) {
            case LD_ELF_RISCV_ULEB_OK:
                if (written_size) *written_size = encoded_size;
                return LD_ELF_DEBUG_RELOC_OK;
            case LD_ELF_RISCV_ULEB_INVALID_ARGUMENT:
                return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;
            case LD_ELF_RISCV_ULEB_TRUNCATED:
                return LD_ELF_DEBUG_RELOC_TRUNCATED;
            case LD_ELF_RISCV_ULEB_FIELD_OVERFLOW:
                return LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128;
            default:
                return LD_ELF_DEBUG_RELOC_OVERFLOW;
        }
    }

    size_t width;
    switch (relocation->type) {
        case LD_ELF_R_RISCV_32:
        case LD_ELF_R_RISCV_SET32:
            width = 4U;
            break;
        case LD_ELF_R_RISCV_64:
            width = 8U;
            break;
        case LD_ELF_R_RISCV_ADD8:
        case LD_ELF_R_RISCV_SUB8:
        case LD_ELF_R_RISCV_SET6:
        case LD_ELF_R_RISCV_SUB6:
        case LD_ELF_R_RISCV_SET8:
            width = 1U;
            break;
        case LD_ELF_R_RISCV_ADD16:
        case LD_ELF_R_RISCV_SUB16:
        case LD_ELF_R_RISCV_SET16:
            width = 2U;
            break;
        case LD_ELF_R_RISCV_ADD32:
        case LD_ELF_R_RISCV_SUB32:
            width = 4U;
            break;
        case LD_ELF_R_RISCV_ADD64:
        case LD_ELF_R_RISCV_SUB64:
            width = 8U;
            break;
        default:
            return LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION;
    }
    ld_elf_debug_reloc_result_t status =
            ld_elf_debug_require_place(place, place_size, width);
    if (status != LD_ELF_DEBUG_RELOC_OK) return status;

    uint8_t replacement[8];
    memcpy(replacement, place, width);
    if (relocation->type == LD_ELF_R_RISCV_64 &&
        values->has_tombstone) {
        ld_elf_debug_write_u64(replacement, values->tombstone_value);
    } else {
        int64_t expression;
        if (!ld_elf_debug_add_signed(values->symbol_value,
                                     relocation->addend, 64U,
                                     &expression)) {
            return LD_ELF_DEBUG_RELOC_OVERFLOW;
        }

        if (relocation->type == LD_ELF_R_RISCV_32) {
            if (expression < INT32_MIN || expression > INT32_MAX)
                return LD_ELF_DEBUG_RELOC_OVERFLOW;
            ld_elf_debug_write_u32(replacement, (uint32_t) expression);
        } else if (relocation->type == LD_ELF_R_RISCV_64) {
            ld_elf_debug_write_u64(replacement, (uint64_t) expression);
        } else {
            unsigned bits =
                    ld_elf_debug_riscv_integer_width(relocation->type);
            if (bits != 0U) {
                int64_t current = ld_elf_debug_sign_extend(
                        ld_elf_debug_read_width(replacement, bits), bits);
                int64_t operand = ld_elf_debug_sign_extend(
                        (uint64_t) expression, bits);
                bool subtract =
                        relocation->type >= LD_ELF_R_RISCV_SUB8 &&
                        relocation->type <= LD_ELF_R_RISCV_SUB64;
                int64_t result =
                        subtract
                                ? ld_elf_debug_saturating_subtract(
                                          current, operand, bits)
                                : ld_elf_debug_saturating_add(
                                          current, operand, bits);
                ld_elf_debug_write_width(replacement, bits,
                                         (uint64_t) result);
            } else if (relocation->type == LD_ELF_R_RISCV_SET6 ||
                       relocation->type == LD_ELF_R_RISCV_SUB6) {
                int64_t operand = ld_elf_debug_sign_extend(
                        (uint8_t) expression, 8U);
                uint8_t original = replacement[0];
                uint8_t low;
                if (relocation->type == LD_ELF_R_RISCV_SET6) {
                    low = (uint8_t) operand;
                } else {
                    int64_t current =
                            ld_elf_debug_sign_extend(original, 8U);
                    low = (uint8_t) ld_elf_debug_saturating_subtract(
                            current, operand, 8U);
                }
                replacement[0] =
                        (uint8_t) ((original & 0xc0U) | (low & 0x3fU));
            } else {
                unsigned set_bits =
                        relocation->type == LD_ELF_R_RISCV_SET8
                                ? 8U
                                : (relocation->type ==
                                                   LD_ELF_R_RISCV_SET16
                                           ? 16U
                                           : 32U);
                ld_elf_debug_write_width(replacement, set_bits,
                                         (uint64_t) expression);
            }
        }
    }

    memcpy(place, replacement, width);
    if (written_size) *written_size = width;
    return LD_ELF_DEBUG_RELOC_OK;
}

ld_elf_debug_reloc_result_t ld_elf_debug_apply_nonalloc_relocation(
        uint16_t machine, const ld_elf_debug_relocation_t *relocation,
        const ld_elf_debug_reloc_values_t *values, uint8_t *place,
        size_t place_size, size_t *written_size) {
    if (written_size) *written_size = 0U;
    if (!relocation) return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;
    if (machine != LD_ELF_EM_X86_64 && machine != LD_ELF_EM_AARCH64 &&
        machine != LD_ELF_EM_RISCV) {
        return LD_ELF_DEBUG_RELOC_UNSUPPORTED_MACHINE;
    }
    if (relocation->type == 0U) return LD_ELF_DEBUG_RELOC_OK;
    if (!values) return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;

    switch (machine) {
        case LD_ELF_EM_X86_64:
            return ld_elf_debug_apply_x86_64(
                    relocation, values, place, place_size, written_size);
        case LD_ELF_EM_AARCH64:
            return ld_elf_debug_apply_aarch64(
                    relocation, values, place, place_size, written_size);
        default:
            return ld_elf_debug_apply_riscv64(
                    relocation, values, place, place_size, written_size);
    }
}

ld_elf_debug_reloc_result_t ld_elf_debug_apply_riscv_uleb_pair(
        const ld_elf_debug_relocation_t *set_relocation,
        const ld_elf_debug_reloc_values_t *set_values,
        const ld_elf_debug_relocation_t *sub_relocation,
        const ld_elf_debug_reloc_values_t *sub_values, uint8_t *place,
        size_t place_size, size_t *written_size) {
    if (written_size) *written_size = 0U;
    if (!set_relocation || !set_values || !sub_relocation || !sub_values ||
        !place) {
        return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;
    }
    if (set_relocation->type != LD_ELF_R_RISCV_SET_ULEB128 ||
        sub_relocation->type != LD_ELF_R_RISCV_SUB_ULEB128) {
        return LD_ELF_DEBUG_RELOC_INVALID_PAIR;
    }

    size_t width = 0U;
    ld_elf_riscv_uleb_result_t result = ld_elf_riscv_uleb_apply_pair(
            place, place_size, set_values->symbol_value,
            set_relocation->addend, sub_values->symbol_value,
            sub_relocation->addend, &width);
    switch (result) {
        case LD_ELF_RISCV_ULEB_OK:
            if (written_size) *written_size = width;
            return LD_ELF_DEBUG_RELOC_OK;
        case LD_ELF_RISCV_ULEB_INVALID_ARGUMENT:
            return LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT;
        case LD_ELF_RISCV_ULEB_TRUNCATED:
            return LD_ELF_DEBUG_RELOC_TRUNCATED;
        case LD_ELF_RISCV_ULEB_FIELD_OVERFLOW:
            return LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128;
        default:
            return LD_ELF_DEBUG_RELOC_OVERFLOW;
    }
}

const char *ld_elf_debug_reloc_result_string(
        ld_elf_debug_reloc_result_t result) {
    switch (result) {
        case LD_ELF_DEBUG_RELOC_OK:
            return "success";
        case LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT:
            return "invalid non-allocated relocation argument";
        case LD_ELF_DEBUG_RELOC_UNSUPPORTED_MACHINE:
            return "unsupported ELF64 machine";
        case LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION:
            return "unsupported non-allocated relocation";
        case LD_ELF_DEBUG_RELOC_TRUNCATED:
            return "truncated non-allocated relocation field";
        case LD_ELF_DEBUG_RELOC_OVERFLOW:
            return "non-allocated relocation expression overflow";
        case LD_ELF_DEBUG_RELOC_INVALID_PAIR:
            return "invalid RISC-V ULEB128 SET/SUB pair";
        case LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128:
            return "malformed RISC-V ULEB128 relocation field";
    }
    return "unknown non-allocated relocation result";
}

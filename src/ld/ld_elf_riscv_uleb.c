#include "ld_elf_riscv_uleb.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Zig 738d2be9 applies SET_ULEB128 and SUB_ULEB128 independently and in input
 * order.  In particular, neither relocation requires the other to exist.
 * Nature adds checked field decoding and stages each write so malformed input
 * leaves it unchanged. Like Zig's overwriteUleb, values are truncated to the
 * existing field width; this is required because a SET/SUB sequence commonly
 * stores an intermediate absolute address before the shared high bits cancel.
 */

static bool ld_elf_riscv_uleb_add_signed(uint64_t value, int64_t addend,
                                         uint64_t *result) {
    if (addend >= 0) {
        uint64_t amount = (uint64_t) addend;
        if (value > UINT64_MAX - amount) return false;
        *result = value + amount;
        return true;
    }
    uint64_t amount = (uint64_t) (-(addend + 1)) + 1U;
    if (amount > value) return false;
    *result = value - amount;
    return true;
}

static bool ld_elf_riscv_uleb_subtract_signed(uint64_t value,
                                              int64_t addend,
                                              uint64_t *result) {
    if (addend >= 0) {
        uint64_t amount = (uint64_t) addend;
        if (amount > value) return false;
        *result = value - amount;
        return true;
    }
    uint64_t amount = (uint64_t) (-(addend + 1)) + 1U;
    if (value > UINT64_MAX - amount) return false;
    *result = value + amount;
    return true;
}

static ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_field_size(
        const uint8_t *place, size_t place_size, size_t *encoded_size) {
    size_t limit = place_size < 10U ? place_size : 10U;
    for (size_t i = 0; i < limit; i++) {
        uint8_t byte = place[i];
        if (i == 9U && (byte & 0x7fU) > 1U) {
            return LD_ELF_RISCV_ULEB_FIELD_OVERFLOW;
        }
        if ((byte & 0x80U) == 0U) {
            *encoded_size = i + 1U;
            return LD_ELF_RISCV_ULEB_OK;
        }
    }
    return place_size < 10U ? LD_ELF_RISCV_ULEB_TRUNCATED
                            : LD_ELF_RISCV_ULEB_FIELD_OVERFLOW;
}

static uint64_t ld_elf_riscv_uleb_read(const uint8_t *place,
                                       size_t width) {
    uint64_t value = 0U;
    for (size_t i = 0; i < width; i++) {
        value |= (uint64_t) (place[i] & 0x7fU) << (i * 7U);
    }
    return value;
}

static ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_write(
        uint8_t *place, size_t width, uint64_t value) {
    uint8_t replacement[10];
    for (size_t i = 0; i < width; i++) {
        replacement[i] = (uint8_t) (value & 0x7fU);
        value >>= 7U;
        if (i + 1U != width) replacement[i] |= 0x80U;
    }
    memcpy(place, replacement, width);
    return LD_ELF_RISCV_ULEB_OK;
}

ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_apply(
        uint8_t *place, size_t place_size, bool subtract,
        uint64_t symbol_value, int64_t addend, size_t *encoded_size) {
    if (encoded_size) *encoded_size = 0U;
    if (!place || !encoded_size) return LD_ELF_RISCV_ULEB_INVALID_ARGUMENT;

    size_t width = 0U;
    ld_elf_riscv_uleb_result_t result =
            ld_elf_riscv_uleb_field_size(place, place_size, &width);
    if (result != LD_ELF_RISCV_ULEB_OK) return result;
    *encoded_size = width;

    uint64_t operand;
    if (subtract) {
        if (!ld_elf_riscv_uleb_subtract_signed(symbol_value, addend,
                                               &operand)) {
            return LD_ELF_RISCV_ULEB_SUB_OVERFLOW;
        }
        uint64_t current = ld_elf_riscv_uleb_read(place, width);
        operand = current - operand;
    } else if (!ld_elf_riscv_uleb_add_signed(symbol_value, addend,
                                             &operand)) {
        return LD_ELF_RISCV_ULEB_SET_OVERFLOW;
    }
    return ld_elf_riscv_uleb_write(place, width, operand);
}

ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_apply_pair(
        uint8_t *place, size_t place_size, uint64_t set_symbol,
        int64_t set_addend, uint64_t sub_symbol, int64_t sub_addend,
        size_t *encoded_size) {
    if (encoded_size) *encoded_size = 0U;
    if (!place || !encoded_size) return LD_ELF_RISCV_ULEB_INVALID_ARGUMENT;

    size_t width = 0U;
    ld_elf_riscv_uleb_result_t result =
            ld_elf_riscv_uleb_field_size(place, place_size, &width);
    if (result != LD_ELF_RISCV_ULEB_OK) return result;
    *encoded_size = width;

    uint8_t replacement[10];
    memcpy(replacement, place, width);
    size_t ignored_size;
    result = ld_elf_riscv_uleb_apply(replacement, width, false, set_symbol,
                                     set_addend, &ignored_size);
    if (result != LD_ELF_RISCV_ULEB_OK) return result;
    result = ld_elf_riscv_uleb_apply(replacement, width, true, sub_symbol,
                                     sub_addend, &ignored_size);
    if (result != LD_ELF_RISCV_ULEB_OK) return result;
    memcpy(place, replacement, width);
    return LD_ELF_RISCV_ULEB_OK;
}

const char *ld_elf_riscv_uleb_result_string(
        ld_elf_riscv_uleb_result_t result) {
    switch (result) {
        case LD_ELF_RISCV_ULEB_OK:
            return "success";
        case LD_ELF_RISCV_ULEB_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_RISCV_ULEB_TRUNCATED:
            return "truncated ULEB128 relocation field";
        case LD_ELF_RISCV_ULEB_FIELD_OVERFLOW:
            return "ULEB128 relocation field exceeds 64 bits";
        case LD_ELF_RISCV_ULEB_SET_OVERFLOW:
            return "SET symbol plus addend is outside unsigned 64-bit range";
        case LD_ELF_RISCV_ULEB_SUB_OVERFLOW:
            return "SUB symbol minus addend is outside unsigned 64-bit range";
    }
    return "unknown RISC-V ULEB128 result";
}

#include "ld_coff_reader.h"

#include <limits.h>
#include <string.h>

/*
 * Checked wire access used in place of LLVM Object's COFF reader.
 * Source: llvm-project/llvm/lib/Object/COFFObjectFile.cpp
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

bool ld_coff_range_ok(ld_coff_view_t view, uint64_t offset, uint64_t length) {
    return offset <= view.size && length <= (uint64_t) view.size - offset;
}

bool ld_coff_add_ok(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

bool ld_coff_mul_ok(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || (right != 0U && left > UINT64_MAX / right)) return false;
    *result = left * right;
    return true;
}

bool ld_coff_align_ok(uint64_t value, uint64_t alignment, uint64_t *result) {
    if (!result || alignment == 0U || (alignment & (alignment - 1U)) != 0U)
        return false;
    uint64_t mask = alignment - 1U;
    if (value > UINT64_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

bool ld_coff_read_u16(ld_coff_view_t view, uint64_t offset, uint16_t *value) {
    if (!value || !ld_coff_range_ok(view, offset, 2U)) return false;
    *value = (uint16_t) view.bytes[offset] |
             (uint16_t) ((uint16_t) view.bytes[offset + 1U] << 8U);
    return true;
}

bool ld_coff_read_i16(ld_coff_view_t view, uint64_t offset, int16_t *value) {
    uint16_t raw;
    if (!value || !ld_coff_read_u16(view, offset, &raw)) return false;
    memcpy(value, &raw, sizeof(raw));
    return true;
}

bool ld_coff_read_u32(ld_coff_view_t view, uint64_t offset, uint32_t *value) {
    if (!value || !ld_coff_range_ok(view, offset, 4U)) return false;
    *value = (uint32_t) view.bytes[offset] |
             (uint32_t) view.bytes[offset + 1U] << 8U |
             (uint32_t) view.bytes[offset + 2U] << 16U |
             (uint32_t) view.bytes[offset + 3U] << 24U;
    return true;
}

bool ld_coff_read_i32(ld_coff_view_t view, uint64_t offset, int32_t *value) {
    uint32_t raw;
    if (!value || !ld_coff_read_u32(view, offset, &raw)) return false;
    memcpy(value, &raw, sizeof(raw));
    return true;
}

bool ld_coff_read_u64(ld_coff_view_t view, uint64_t offset, uint64_t *value) {
    uint32_t low, high;
    if (!value || !ld_coff_read_u32(view, offset, &low) ||
        !ld_coff_read_u32(view, offset + 4U, &high))
        return false;
    *value = (uint64_t) low | (uint64_t) high << 32U;
    return true;
}

bool ld_coff_write_u16(uint8_t *bytes, size_t size, uint64_t offset,
                       uint16_t value) {
    ld_coff_view_t view = {bytes, size};
    if (!bytes || !ld_coff_range_ok(view, offset, 2U)) return false;
    bytes[offset] = (uint8_t) value;
    bytes[offset + 1U] = (uint8_t) (value >> 8U);
    return true;
}

bool ld_coff_write_u32(uint8_t *bytes, size_t size, uint64_t offset,
                       uint32_t value) {
    ld_coff_view_t view = {bytes, size};
    if (!bytes || !ld_coff_range_ok(view, offset, 4U)) return false;
    for (unsigned i = 0; i < 4U; i++)
        bytes[offset + i] = (uint8_t) (value >> (i * 8U));
    return true;
}

bool ld_coff_write_u64(uint8_t *bytes, size_t size, uint64_t offset,
                       uint64_t value) {
    ld_coff_view_t view = {bytes, size};
    if (!bytes || !ld_coff_range_ok(view, offset, 8U)) return false;
    for (unsigned i = 0; i < 8U; i++)
        bytes[offset + i] = (uint8_t) (value >> (i * 8U));
    return true;
}

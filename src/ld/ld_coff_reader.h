#ifndef NATURE_LD_COFF_READER_H
#define NATURE_LD_COFF_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *bytes;
    size_t size;
} ld_coff_view_t;

bool ld_coff_range_ok(ld_coff_view_t view, uint64_t offset, uint64_t length);
bool ld_coff_add_ok(uint64_t left, uint64_t right, uint64_t *result);
bool ld_coff_mul_ok(uint64_t left, uint64_t right, uint64_t *result);
bool ld_coff_align_ok(uint64_t value, uint64_t alignment, uint64_t *result);
bool ld_coff_read_u16(ld_coff_view_t view, uint64_t offset, uint16_t *value);
bool ld_coff_read_i16(ld_coff_view_t view, uint64_t offset, int16_t *value);
bool ld_coff_read_u32(ld_coff_view_t view, uint64_t offset, uint32_t *value);
bool ld_coff_read_i32(ld_coff_view_t view, uint64_t offset, int32_t *value);
bool ld_coff_read_u64(ld_coff_view_t view, uint64_t offset, uint64_t *value);
bool ld_coff_write_u16(uint8_t *bytes, size_t size, uint64_t offset,
                       uint16_t value);
bool ld_coff_write_u32(uint8_t *bytes, size_t size, uint64_t offset,
                       uint32_t value);
bool ld_coff_write_u64(uint8_t *bytes, size_t size, uint64_t offset,
                       uint64_t value);

#endif

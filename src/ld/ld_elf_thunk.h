#ifndef NATURE_LD_ELF_THUNK_H
#define NATURE_LD_ELF_THUNK_H

#include "ld_elf_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD_ELF_AARCH64_NO_THUNK UINT32_MAX
#define LD_ELF_AARCH64_THUNK_GROUP_MAX UINT64_C(0x500000)
#define LD_ELF_AARCH64_THUNK_SIZE 12U

typedef struct {
    ld_elf_global_t *global;
    ld_elf_object_t *object;
    uint32_t symbol_index;
    int64_t addend;
} ld_elf_aarch64_thunk_key_t;

typedef struct {
    ld_elf_aarch64_thunk_key_t key;
    bool used;
} ld_elf_aarch64_thunk_entry_t;

typedef struct {
    uint64_t payload_start_offset;
    uint64_t payload_end_offset;
    uint64_t thunk_output_offset;
    size_t first_placement_index;
    size_t last_placement_index;
    ld_elf_aarch64_thunk_entry_t *entries;
    size_t entry_count;
    size_t entry_capacity;
} ld_elf_aarch64_thunk_group_t;

typedef struct {
    ld_elf_aarch64_thunk_group_t *groups;
    size_t group_count;
    size_t group_capacity;
} ld_elf_aarch64_thunk_plan_t;

typedef enum {
    LD_ELF_AARCH64_THUNK_ENCODE_OK = 0,
    LD_ELF_AARCH64_THUNK_ENCODE_UNALIGNED,
    LD_ELF_AARCH64_THUNK_ENCODE_RANGE,
} ld_elf_aarch64_thunk_encode_result_t;

void ld_elf_aarch64_thunk_plan_init(ld_elf_aarch64_thunk_plan_t *plan);
void ld_elf_aarch64_thunk_plan_deinit(ld_elf_aarch64_thunk_plan_t *plan);

bool ld_elf_aarch64_thunk_group_append(
        ld_elf_aarch64_thunk_plan_t *plan, uint64_t payload_start_offset,
        size_t placement_index, uint32_t *group_index);

bool ld_elf_aarch64_thunk_entry_find_or_add(
        ld_elf_aarch64_thunk_group_t *group,
        const ld_elf_aarch64_thunk_key_t *key, uint32_t *entry_index,
        bool *added);

bool ld_elf_aarch64_branch26_fits(uint64_t target, uint64_t place,
                                  int64_t *displacement);

/*
 * Encodes the range-extension sequence used by Zig's ELF AArch64 Thunk:
 *
 *     adrp x16, target@PAGE
 *     add  x16, x16, target@PAGEOFF
 *     br   x16
 *
 * The function validates all operands before modifying output.
 */
ld_elf_aarch64_thunk_encode_result_t ld_elf_aarch64_thunk_encode(
        uint8_t output[LD_ELF_AARCH64_THUNK_SIZE], uint64_t thunk_address,
        uint64_t target_address);

#endif

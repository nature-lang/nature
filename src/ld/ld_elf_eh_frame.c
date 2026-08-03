#include "ld_elf_eh_frame.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

/*
 * The encoding and search-table construction are translated from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224,
 * src/link/Elf/eh_frame.zig:writeEhFrameHdr. Zig is MIT licensed; see
 * ZIG-LICENSE.txt. Nature additionally checks every conversion and uses the
 * original input index to make equal-key ordering deterministic.
 */

#define LD_ELF_EH_FRAME_HDR_FIXED_SIZE 12U
#define LD_ELF_EH_FRAME_HDR_ENTRY_SIZE 8U

typedef struct {
    uint64_t first_pc;
    uint64_t fde_address;
    size_t original_index;
    int32_t first_pc_relative;
    int32_t fde_address_relative;
} ld_elf_eh_frame_hdr_work_entry_t;

static bool ld_elf_eh_frame_hdr_relative_i32(uint64_t target, uint64_t base,
                                             int32_t *result) {
    if (target >= base) {
        uint64_t magnitude = target - base;
        if (magnitude > (uint64_t) INT32_MAX) return false;
        *result = (int32_t) magnitude;
        return true;
    }

    uint64_t magnitude = base - target;
    if (magnitude > UINT64_C(0x80000000)) return false;
    if (magnitude == UINT64_C(0x80000000)) {
        *result = INT32_MIN;
    } else {
        *result = -(int32_t) magnitude;
    }
    return true;
}

static void ld_elf_eh_frame_hdr_write_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t) value;
    output[1] = (uint8_t) (value >> 8U);
    output[2] = (uint8_t) (value >> 16U);
    output[3] = (uint8_t) (value >> 24U);
}

static int ld_elf_eh_frame_hdr_compare_entries(const void *left_pointer,
                                               const void *right_pointer) {
    const ld_elf_eh_frame_hdr_work_entry_t *left = left_pointer;
    const ld_elf_eh_frame_hdr_work_entry_t *right = right_pointer;

    if (left->first_pc < right->first_pc) return -1;
    if (left->first_pc > right->first_pc) return 1;
    if (left->fde_address < right->fde_address) return -1;
    if (left->fde_address > right->fde_address) return 1;
    if (left->original_index < right->original_index) return -1;
    if (left->original_index > right->original_index) return 1;
    return 0;
}

ld_elf_eh_frame_hdr_result_t
ld_elf_eh_frame_hdr_size(size_t entry_count, size_t *encoded_size) {
    if (!encoded_size) return LD_ELF_EH_FRAME_HDR_INVALID_ARGUMENT;
    if (entry_count > UINT32_MAX)
        return LD_ELF_EH_FRAME_HDR_ENTRY_COUNT_OVERFLOW;
    if (entry_count >
        (SIZE_MAX - LD_ELF_EH_FRAME_HDR_FIXED_SIZE) /
                LD_ELF_EH_FRAME_HDR_ENTRY_SIZE)
        return LD_ELF_EH_FRAME_HDR_ENCODED_SIZE_OVERFLOW;

    *encoded_size = LD_ELF_EH_FRAME_HDR_FIXED_SIZE +
                    entry_count * LD_ELF_EH_FRAME_HDR_ENTRY_SIZE;
    return LD_ELF_EH_FRAME_HDR_OK;
}

ld_elf_eh_frame_hdr_result_t ld_elf_eh_frame_hdr_encode(
        uint8_t *output, size_t output_size, uint64_t header_address,
        uint64_t eh_frame_address,
        const ld_elf_eh_frame_hdr_entry_t *entries, size_t entry_count,
        size_t *error_entry_index) {
    if (error_entry_index) *error_entry_index = SIZE_MAX;

    size_t encoded_size;
    ld_elf_eh_frame_hdr_result_t size_result =
            ld_elf_eh_frame_hdr_size(entry_count, &encoded_size);
    if (size_result != LD_ELF_EH_FRAME_HDR_OK) return size_result;
    if (!output || (entry_count != 0U && !entries))
        return LD_ELF_EH_FRAME_HDR_INVALID_ARGUMENT;
    if (output_size < encoded_size)
        return LD_ELF_EH_FRAME_HDR_OUTPUT_TOO_SMALL;
    if (entry_count > SIZE_MAX / sizeof(ld_elf_eh_frame_hdr_work_entry_t))
        return LD_ELF_EH_FRAME_HDR_WORK_SIZE_OVERFLOW;
    if (header_address > UINT64_MAX - 4U)
        return LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE;

    int32_t eh_frame_pointer_relative;
    if (!ld_elf_eh_frame_hdr_relative_i32(
                eh_frame_address, header_address + 4U,
                &eh_frame_pointer_relative))
        return LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE;

    ld_elf_eh_frame_hdr_work_entry_t *work = NULL;
    if (entry_count != 0U) {
        work = malloc(entry_count * sizeof(*work));
        if (!work) return LD_ELF_EH_FRAME_HDR_OUT_OF_MEMORY;
        for (size_t i = 0; i < entry_count; i++) {
            work[i].first_pc = entries[i].first_pc;
            work[i].fde_address = entries[i].fde_address;
            work[i].original_index = i;
        }
        qsort(work, entry_count, sizeof(*work),
              ld_elf_eh_frame_hdr_compare_entries);
    }

    for (size_t i = 0; i < entry_count; i++) {
        if (!ld_elf_eh_frame_hdr_relative_i32(
                    work[i].first_pc, header_address,
                    &work[i].first_pc_relative)) {
            if (error_entry_index)
                *error_entry_index = work[i].original_index;
            free(work);
            return LD_ELF_EH_FRAME_HDR_FIRST_PC_RANGE;
        }
        if (!ld_elf_eh_frame_hdr_relative_i32(
                    work[i].fde_address, header_address,
                    &work[i].fde_address_relative)) {
            if (error_entry_index)
                *error_entry_index = work[i].original_index;
            free(work);
            return LD_ELF_EH_FRAME_HDR_FDE_ADDRESS_RANGE;
        }
    }

    output[0] = 1U;
    output[1] = 0x1bU; /* DW_EH_PE_pcrel | DW_EH_PE_sdata4 */
    output[2] = 0x03U; /* DW_EH_PE_udata4 */
    output[3] = 0x3bU; /* DW_EH_PE_datarel | DW_EH_PE_sdata4 */
    ld_elf_eh_frame_hdr_write_u32(output + 4U,
                                  (uint32_t) eh_frame_pointer_relative);
    ld_elf_eh_frame_hdr_write_u32(output + 8U, (uint32_t) entry_count);
    for (size_t i = 0; i < entry_count; i++) {
        uint8_t *encoded_entry =
                output + LD_ELF_EH_FRAME_HDR_FIXED_SIZE +
                i * LD_ELF_EH_FRAME_HDR_ENTRY_SIZE;
        ld_elf_eh_frame_hdr_write_u32(
                encoded_entry, (uint32_t) work[i].first_pc_relative);
        ld_elf_eh_frame_hdr_write_u32(
                encoded_entry + 4U,
                (uint32_t) work[i].fde_address_relative);
    }

    free(work);
    return LD_ELF_EH_FRAME_HDR_OK;
}

const char *ld_elf_eh_frame_hdr_result_string(
        ld_elf_eh_frame_hdr_result_t result) {
    switch (result) {
        case LD_ELF_EH_FRAME_HDR_OK:
            return "success";
        case LD_ELF_EH_FRAME_HDR_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_EH_FRAME_HDR_ENTRY_COUNT_OVERFLOW:
            return "FDE count does not fit the ELF udata4 field";
        case LD_ELF_EH_FRAME_HDR_ENCODED_SIZE_OVERFLOW:
            return "encoded .eh_frame_hdr size overflow";
        case LD_ELF_EH_FRAME_HDR_WORK_SIZE_OVERFLOW:
            return ".eh_frame_hdr working allocation size overflow";
        case LD_ELF_EH_FRAME_HDR_OUTPUT_TOO_SMALL:
            return ".eh_frame_hdr output buffer is too small";
        case LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE:
            return ".eh_frame pointer does not fit pcrel sdata4";
        case LD_ELF_EH_FRAME_HDR_FIRST_PC_RANGE:
            return "FDE first PC does not fit datarel sdata4";
        case LD_ELF_EH_FRAME_HDR_FDE_ADDRESS_RANGE:
            return "FDE address does not fit datarel sdata4";
        case LD_ELF_EH_FRAME_HDR_OUT_OF_MEMORY:
            return "out of memory while building .eh_frame_hdr";
    }
    return "unknown .eh_frame_hdr encoding error";
}

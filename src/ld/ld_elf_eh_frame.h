#ifndef NATURE_LD_ELF_EH_FRAME_H
#define NATURE_LD_ELF_EH_FRAME_H

#include <stddef.h>
#include <stdint.h>

/*
 * One live FDE in the binary-search table of an ELF64 .eh_frame_hdr.
 * Addresses are absolute virtual addresses after final section layout.
 */
typedef struct {
    uint64_t first_pc;
    uint64_t fde_address;
} ld_elf_eh_frame_hdr_entry_t;

typedef enum {
    LD_ELF_EH_FRAME_HDR_OK = 0,
    LD_ELF_EH_FRAME_HDR_INVALID_ARGUMENT,
    LD_ELF_EH_FRAME_HDR_ENTRY_COUNT_OVERFLOW,
    LD_ELF_EH_FRAME_HDR_ENCODED_SIZE_OVERFLOW,
    LD_ELF_EH_FRAME_HDR_WORK_SIZE_OVERFLOW,
    LD_ELF_EH_FRAME_HDR_OUTPUT_TOO_SMALL,
    LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE,
    LD_ELF_EH_FRAME_HDR_FIRST_PC_RANGE,
    LD_ELF_EH_FRAME_HDR_FDE_ADDRESS_RANGE,
    LD_ELF_EH_FRAME_HDR_OUT_OF_MEMORY,
} ld_elf_eh_frame_hdr_result_t;

/*
 * Computes the exact encoded size: 12 + entry_count * 8. The entry count
 * must fit the udata4 count field. encoded_size is not modified on failure.
 */
ld_elf_eh_frame_hdr_result_t
ld_elf_eh_frame_hdr_size(size_t entry_count, size_t *encoded_size);

/*
 * Encodes an ELF64 little-endian .eh_frame_hdr with a binary-search table.
 *
 * The table is ordered by first_pc and then fde_address. Exact duplicate
 * entries preserve their input order. All arguments and relative offsets are
 * validated before output is modified. On an entry-specific range failure,
 * error_entry_index receives the corresponding index in entries; otherwise
 * it receives SIZE_MAX. The error index pointer may be NULL.
 */
ld_elf_eh_frame_hdr_result_t ld_elf_eh_frame_hdr_encode(
        uint8_t *output, size_t output_size, uint64_t header_address,
        uint64_t eh_frame_address,
        const ld_elf_eh_frame_hdr_entry_t *entries, size_t entry_count,
        size_t *error_entry_index);

const char *ld_elf_eh_frame_hdr_result_string(
        ld_elf_eh_frame_hdr_result_t result);

#endif

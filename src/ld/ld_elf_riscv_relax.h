#ifndef NATURE_LD_ELF_RISCV_RELAX_H
#define NATURE_LD_ELF_RISCV_RELAX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t offset;
    int64_t addend;
    size_t source_index;
} ld_elf_riscv_align_input_t;

typedef struct {
    uint64_t input_offset;
    uint64_t padding_size;
    uint64_t alignment;
    uint64_t output_offset;
    uint64_t kept_size;
    size_t source_index;
} ld_elf_riscv_align_region_t;

typedef struct {
    ld_elf_riscv_align_region_t *regions;
    size_t count;
    uint64_t input_size;
    uint64_t output_size;
    bool rvc;
    bool laid_out;
} ld_elf_riscv_relax_plan_t;

typedef enum {
    LD_ELF_RISCV_RELAX_OK = 0,
    LD_ELF_RISCV_RELAX_INVALID_ARGUMENT,
    LD_ELF_RISCV_RELAX_ALLOCATION_OVERFLOW,
    LD_ELF_RISCV_RELAX_OUT_OF_MEMORY,
    LD_ELF_RISCV_RELAX_NEGATIVE_PADDING,
    LD_ELF_RISCV_RELAX_PADDING_OUT_OF_RANGE,
    LD_ELF_RISCV_RELAX_PADDING_GRANULARITY,
    LD_ELF_RISCV_RELAX_NON_NOP_PADDING,
    LD_ELF_RISCV_RELAX_OVERLAPPING_PADDING,
    LD_ELF_RISCV_RELAX_ADDRESS_OVERFLOW,
    LD_ELF_RISCV_RELAX_ALIGNMENT_IMPOSSIBLE,
    LD_ELF_RISCV_RELAX_NOT_LAID_OUT,
    LD_ELF_RISCV_RELAX_OUTPUT_SIZE_MISMATCH,
} ld_elf_riscv_relax_result_t;

void ld_elf_riscv_relax_plan_init(ld_elf_riscv_relax_plan_t *plan);
void ld_elf_riscv_relax_plan_deinit(ld_elf_riscv_relax_plan_t *plan);

/*
 * Builds a checked relaxation plan from the R_RISCV_ALIGN relocations in one
 * ELF64 input section. source_index is returned through error_source_index so
 * the caller can diagnose the exact relocation.
 */
ld_elf_riscv_relax_result_t ld_elf_riscv_relax_plan_build(
        ld_elf_riscv_relax_plan_t *plan, const uint8_t *input,
        size_t input_size, const ld_elf_riscv_align_input_t *alignments,
        size_t alignment_count, bool rvc, size_t *error_source_index);

/* Resolves every padding size after the input section's final address is known. */
ld_elf_riscv_relax_result_t ld_elf_riscv_relax_plan_layout(
        ld_elf_riscv_relax_plan_t *plan, uint64_t section_address,
        size_t *error_source_index);

bool ld_elf_riscv_relax_plan_active(
        const ld_elf_riscv_relax_plan_t *plan);

/* Maps an original input range into the compacted output section. */
bool ld_elf_riscv_relax_map(const ld_elf_riscv_relax_plan_t *plan,
                            uint64_t input_offset, uint64_t width,
                            uint64_t *output_offset, bool *alive,
                            uint64_t *available);

/* Copies an input section while replacing retained alignment padding by NOPs. */
ld_elf_riscv_relax_result_t ld_elf_riscv_relax_emit(
        const ld_elf_riscv_relax_plan_t *plan, uint8_t *output,
        size_t output_size, const uint8_t *input, size_t input_size);

const char *ld_elf_riscv_relax_result_string(
        ld_elf_riscv_relax_result_t result);

#endif

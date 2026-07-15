#include "ld_elf_riscv_relax.h"

#include <stdlib.h>
#include <string.h>

/*
 * Zig commit 738d2be9d6b6ef3ff3559130c05159ef53336224 supplies the
 * surrounding ELF relocation and layout model, but its RISC-V backend did
 * not yet implement R_RISCV_ALIGN.  This module follows the RISC-V psABI and
 * the monotonic byte-deletion behavior of GNU binutils 2.42.  Zig is MIT
 * licensed; see ZIG-LICENSE.txt.
 */

static uint16_t ld_elf_riscv_relax_read_u16(const uint8_t *bytes) {
    return (uint16_t) bytes[0] | (uint16_t) ((uint16_t) bytes[1] << 8U);
}

static uint32_t ld_elf_riscv_relax_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) | ((uint32_t) bytes[3] << 24U);
}

static void ld_elf_riscv_relax_write_nops(uint8_t *output, uint64_t size,
                                          bool rvc) {
    if (rvc && (size & 2U) != 0U) {
        output[0] = 0x01U;
        output[1] = 0x00U;
        output += 2U;
        size -= 2U;
    }
    while (size != 0U) {
        output[0] = 0x13U;
        output[1] = 0x00U;
        output[2] = 0x00U;
        output[3] = 0x00U;
        output += 4U;
        size -= 4U;
    }
}

static bool ld_elf_riscv_relax_padding_is_nops(const uint8_t *bytes,
                                               size_t size, bool rvc) {
    size_t offset = 0U;
    while (offset < size) {
        size_t remaining = size - offset;
        if (rvc && remaining >= 2U &&
            ld_elf_riscv_relax_read_u16(bytes + offset) == 0x0001U) {
            offset += 2U;
        } else if (remaining >= 4U &&
                   ld_elf_riscv_relax_read_u32(bytes + offset) ==
                           0x00000013U) {
            offset += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static int ld_elf_riscv_relax_compare_regions(const void *left_pointer,
                                              const void *right_pointer) {
    const ld_elf_riscv_align_region_t *left = left_pointer;
    const ld_elf_riscv_align_region_t *right = right_pointer;
    if (left->input_offset < right->input_offset) return -1;
    if (left->input_offset > right->input_offset) return 1;
    if (left->source_index < right->source_index) return -1;
    if (left->source_index > right->source_index) return 1;
    return 0;
}

void ld_elf_riscv_relax_plan_init(ld_elf_riscv_relax_plan_t *plan) {
    if (plan) memset(plan, 0, sizeof(*plan));
}

void ld_elf_riscv_relax_plan_deinit(ld_elf_riscv_relax_plan_t *plan) {
    if (!plan) return;
    free(plan->regions);
    memset(plan, 0, sizeof(*plan));
}

ld_elf_riscv_relax_result_t ld_elf_riscv_relax_plan_build(
        ld_elf_riscv_relax_plan_t *plan, const uint8_t *input,
        size_t input_size, const ld_elf_riscv_align_input_t *alignments,
        size_t alignment_count, bool rvc, size_t *error_source_index) {
    if (error_source_index) *error_source_index = SIZE_MAX;
    if (!plan || (input_size != 0U && !input) ||
        (alignment_count != 0U && !alignments)) {
        return LD_ELF_RISCV_RELAX_INVALID_ARGUMENT;
    }
    if (alignment_count > SIZE_MAX / sizeof(ld_elf_riscv_align_region_t)) {
        return LD_ELF_RISCV_RELAX_ALLOCATION_OVERFLOW;
    }

    ld_elf_riscv_relax_plan_t built;
    ld_elf_riscv_relax_plan_init(&built);
    built.input_size = input_size;
    built.output_size = input_size;
    built.rvc = rvc;
    built.laid_out = true;
    if (alignment_count != 0U) {
        built.regions = calloc(alignment_count, sizeof(*built.regions));
        if (!built.regions) return LD_ELF_RISCV_RELAX_OUT_OF_MEMORY;
    }

    ld_elf_riscv_relax_result_t result = LD_ELF_RISCV_RELAX_OK;
    for (size_t i = 0; i < alignment_count; i++) {
        const ld_elf_riscv_align_input_t *alignment = &alignments[i];
        if (alignment->addend < 0) {
            result = LD_ELF_RISCV_RELAX_NEGATIVE_PADDING;
            if (error_source_index)
                *error_source_index = alignment->source_index;
            break;
        }
        uint64_t padding = (uint64_t) alignment->addend;
        if (alignment->offset > input_size ||
            padding > (uint64_t) input_size - alignment->offset) {
            result = LD_ELF_RISCV_RELAX_PADDING_OUT_OF_RANGE;
            if (error_source_index)
                *error_source_index = alignment->source_index;
            break;
        }
        if (padding == 0U) continue;
        uint64_t granularity = rvc ? 2U : 4U;
        if (padding % granularity != 0U) {
            result = LD_ELF_RISCV_RELAX_PADDING_GRANULARITY;
            if (error_source_index)
                *error_source_index = alignment->source_index;
            break;
        }
        if (!ld_elf_riscv_relax_padding_is_nops(
                    input + (size_t) alignment->offset, (size_t) padding,
                    rvc)) {
            result = LD_ELF_RISCV_RELAX_NON_NOP_PADDING;
            if (error_source_index)
                *error_source_index = alignment->source_index;
            break;
        }

        uint64_t required_alignment = 1U;
        while (required_alignment <= padding) required_alignment <<= 1U;
        built.regions[built.count++] = (ld_elf_riscv_align_region_t) {
                .input_offset = alignment->offset,
                .padding_size = padding,
                .alignment = required_alignment,
                .source_index = alignment->source_index,
        };
    }

    if (result == LD_ELF_RISCV_RELAX_OK && built.count != 0U) {
        qsort(built.regions, built.count, sizeof(*built.regions),
              ld_elf_riscv_relax_compare_regions);
        uint64_t previous_end = 0U;
        for (size_t i = 0; i < built.count; i++) {
            ld_elf_riscv_align_region_t *region = &built.regions[i];
            if (i != 0U && region->input_offset < previous_end) {
                result = LD_ELF_RISCV_RELAX_OVERLAPPING_PADDING;
                if (error_source_index)
                    *error_source_index = region->source_index;
                break;
            }
            previous_end = region->input_offset + region->padding_size;
        }
        built.laid_out = false;
    }

    if (result != LD_ELF_RISCV_RELAX_OK) {
        ld_elf_riscv_relax_plan_deinit(&built);
        ld_elf_riscv_relax_plan_deinit(plan);
        return result;
    }
    ld_elf_riscv_relax_plan_deinit(plan);
    *plan = built;
    return LD_ELF_RISCV_RELAX_OK;
}

static ld_elf_riscv_relax_result_t ld_elf_riscv_relax_layout_pass(
        const ld_elf_riscv_relax_plan_t *plan, uint64_t section_address,
        bool write, ld_elf_riscv_align_region_t *regions,
        uint64_t *output_size, size_t *error_source_index) {
    uint64_t deleted = 0U;
    uint64_t granularity = plan->rvc ? 2U : 4U;
    for (size_t i = 0; i < plan->count; i++) {
        const ld_elf_riscv_align_region_t *region = &plan->regions[i];
        uint64_t output_offset = region->input_offset - deleted;
        if (section_address > UINT64_MAX - output_offset) {
            if (error_source_index)
                *error_source_index = region->source_index;
            return LD_ELF_RISCV_RELAX_ADDRESS_OVERFLOW;
        }
        uint64_t place = section_address + output_offset;
        uint64_t mask = region->alignment - 1U;
        uint64_t kept = (region->alignment - (place & mask)) & mask;
        if (kept > region->padding_size || kept % granularity != 0U) {
            if (error_source_index)
                *error_source_index = region->source_index;
            return LD_ELF_RISCV_RELAX_ALIGNMENT_IMPOSSIBLE;
        }
        uint64_t removed = region->padding_size - kept;
        if (deleted > UINT64_MAX - removed) {
            if (error_source_index)
                *error_source_index = region->source_index;
            return LD_ELF_RISCV_RELAX_ADDRESS_OVERFLOW;
        }
        if (write) {
            regions[i].output_offset = output_offset;
            regions[i].kept_size = kept;
        }
        deleted += removed;
    }
    *output_size = plan->input_size - deleted;
    return LD_ELF_RISCV_RELAX_OK;
}

ld_elf_riscv_relax_result_t ld_elf_riscv_relax_plan_layout(
        ld_elf_riscv_relax_plan_t *plan, uint64_t section_address,
        size_t *error_source_index) {
    if (error_source_index) *error_source_index = SIZE_MAX;
    if (!plan) return LD_ELF_RISCV_RELAX_INVALID_ARGUMENT;
    uint64_t output_size = 0U;
    ld_elf_riscv_relax_result_t result = ld_elf_riscv_relax_layout_pass(
            plan, section_address, false, NULL, &output_size,
            error_source_index);
    if (result != LD_ELF_RISCV_RELAX_OK) return result;
    result = ld_elf_riscv_relax_layout_pass(
            plan, section_address, true, plan->regions, &output_size,
            error_source_index);
    if (result != LD_ELF_RISCV_RELAX_OK) return result;
    plan->output_size = output_size;
    plan->laid_out = true;
    return LD_ELF_RISCV_RELAX_OK;
}

bool ld_elf_riscv_relax_plan_active(
        const ld_elf_riscv_relax_plan_t *plan) {
    return plan && plan->count != 0U;
}

bool ld_elf_riscv_relax_map(const ld_elf_riscv_relax_plan_t *plan,
                            uint64_t input_offset, uint64_t width,
                            uint64_t *output_offset, bool *alive,
                            uint64_t *available) {
    if (!plan || !plan->laid_out || input_offset > plan->input_size ||
        width > plan->input_size - input_offset) {
        return false;
    }

    uint64_t mapped = input_offset;
    bool range_alive = true;
    uint64_t input_end = input_offset + width;
    for (size_t i = 0; i < plan->count; i++) {
        const ld_elf_riscv_align_region_t *region = &plan->regions[i];
        uint64_t removed_start = region->input_offset + region->kept_size;
        uint64_t removed_end = region->input_offset + region->padding_size;
        uint64_t removed_size = removed_end - removed_start;

        if (input_offset >= removed_end) {
            mapped -= removed_size;
            continue;
        }
        if (input_offset < removed_start) {
            if (width != 0U && input_end > removed_start &&
                input_offset < removed_end) {
                range_alive = false;
                mapped = region->output_offset + region->kept_size;
            }
            break;
        }
        if (input_offset == region->input_offset && width == 0U) {
            mapped = region->output_offset;
            break;
        }
        range_alive = false;
        mapped = region->output_offset + region->kept_size;
        break;
    }

    if (mapped > plan->output_size) return false;
    if (output_offset) *output_offset = mapped;
    if (alive) *alive = range_alive;
    if (available) *available = plan->output_size - mapped;
    return true;
}

ld_elf_riscv_relax_result_t ld_elf_riscv_relax_emit(
        const ld_elf_riscv_relax_plan_t *plan, uint8_t *output,
        size_t output_size, const uint8_t *input, size_t input_size) {
    if (!plan || !plan->laid_out || (input_size != 0U && !input) ||
        (output_size != 0U && !output)) {
        return plan && !plan->laid_out ? LD_ELF_RISCV_RELAX_NOT_LAID_OUT
                                       : LD_ELF_RISCV_RELAX_INVALID_ARGUMENT;
    }
    if (plan->input_size != input_size || plan->output_size != output_size)
        return LD_ELF_RISCV_RELAX_OUTPUT_SIZE_MISMATCH;

    size_t source_offset = 0U;
    size_t destination_offset = 0U;
    for (size_t i = 0; i < plan->count; i++) {
        const ld_elf_riscv_align_region_t *region = &plan->regions[i];
        size_t region_offset = (size_t) region->input_offset;
        size_t prefix_size = region_offset - source_offset;
        if (prefix_size != 0U) {
            memcpy(output + destination_offset, input + source_offset,
                   prefix_size);
            destination_offset += prefix_size;
        }
        if (region->kept_size != 0U) {
            ld_elf_riscv_relax_write_nops(output + destination_offset,
                                          region->kept_size, plan->rvc);
            destination_offset += (size_t) region->kept_size;
        }
        source_offset = region_offset + (size_t) region->padding_size;
    }
    size_t tail_size = input_size - source_offset;
    if (tail_size != 0U) {
        memcpy(output + destination_offset, input + source_offset,
               tail_size);
        destination_offset += tail_size;
    }
    return destination_offset == output_size
                   ? LD_ELF_RISCV_RELAX_OK
                   : LD_ELF_RISCV_RELAX_OUTPUT_SIZE_MISMATCH;
}

const char *ld_elf_riscv_relax_result_string(
        ld_elf_riscv_relax_result_t result) {
    switch (result) {
        case LD_ELF_RISCV_RELAX_OK:
            return "success";
        case LD_ELF_RISCV_RELAX_INVALID_ARGUMENT:
            return "invalid RISC-V relaxation argument";
        case LD_ELF_RISCV_RELAX_ALLOCATION_OVERFLOW:
            return "RISC-V alignment plan allocation size overflow";
        case LD_ELF_RISCV_RELAX_OUT_OF_MEMORY:
            return "out of memory building RISC-V alignment plan";
        case LD_ELF_RISCV_RELAX_NEGATIVE_PADDING:
            return "R_RISCV_ALIGN padding addend is negative";
        case LD_ELF_RISCV_RELAX_PADDING_OUT_OF_RANGE:
            return "R_RISCV_ALIGN padding exceeds its input section";
        case LD_ELF_RISCV_RELAX_PADDING_GRANULARITY:
            return "R_RISCV_ALIGN padding has invalid instruction granularity";
        case LD_ELF_RISCV_RELAX_NON_NOP_PADDING:
            return "R_RISCV_ALIGN region contains a non-NOP instruction";
        case LD_ELF_RISCV_RELAX_OVERLAPPING_PADDING:
            return "R_RISCV_ALIGN padding regions overlap";
        case LD_ELF_RISCV_RELAX_ADDRESS_OVERFLOW:
            return "RISC-V relaxed section address overflows";
        case LD_ELF_RISCV_RELAX_ALIGNMENT_IMPOSSIBLE:
            return "R_RISCV_ALIGN cannot align the following instruction";
        case LD_ELF_RISCV_RELAX_NOT_LAID_OUT:
            return "RISC-V alignment plan has no final section address";
        case LD_ELF_RISCV_RELAX_OUTPUT_SIZE_MISMATCH:
            return "RISC-V relaxed section output size is inconsistent";
    }
    return "unknown RISC-V relaxation error";
}

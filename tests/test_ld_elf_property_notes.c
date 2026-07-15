#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_property.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t type;
    uint32_t data_size;
    uint64_t value;
} test_property_note_entry_t;

static size_t append_property_note(
        uint8_t *bytes, size_t capacity, size_t offset,
        const test_property_note_entry_t *entries, size_t entry_count) {
    size_t descriptor_size = 0U;
    for (size_t i = 0; i < entry_count; i++) {
        size_t entry_size = test_elf_align(
                8U + (size_t) entries[i].data_size, 8U);
        assert(descriptor_size <= SIZE_MAX - entry_size);
        descriptor_size += entry_size;
    }
    assert(descriptor_size <= UINT32_MAX &&
           descriptor_size <= SIZE_MAX - 16U);
    size_t note_size = 16U + descriptor_size;
    assert(offset <= capacity && note_size <= capacity - offset);

    uint8_t *note = bytes + offset;
    memset(note, 0, note_size);
    test_elf_write_u32(note, 4U);
    test_elf_write_u32(note + 4U, (uint32_t) descriptor_size);
    test_elf_write_u32(note + 8U, LD_ELF_NT_GNU_PROPERTY_TYPE_0);
    memcpy(note + 12U, "GNU\0", 4U);

    size_t cursor = 16U;
    for (size_t i = 0; i < entry_count; i++) {
        const test_property_note_entry_t *entry = &entries[i];
        test_elf_write_u32(note + cursor, entry->type);
        test_elf_write_u32(note + cursor + 4U, entry->data_size);
        if (entry->data_size == 4U) {
            test_elf_write_u32(note + cursor + 8U,
                               (uint32_t) entry->value);
        } else if (entry->data_size == 8U) {
            test_elf_write_u64(note + cursor + 8U, entry->value);
        }
        cursor = test_elf_align(
                cursor + 8U + (size_t) entry->data_size, 8U);
    }
    assert(cursor == note_size);
    return note_size;
}

static uint64_t property_output_value(
        const ld_elf_property_plan_t *plan, uint32_t expected_type) {
    assert(plan->finalized && plan->output && plan->output_size >= 16U);
    assert(test_elf_read_u32(plan->output) == 4U);
    assert(test_elf_read_u32(plan->output + 8U) ==
           LD_ELF_NT_GNU_PROPERTY_TYPE_0);
    assert(memcmp(plan->output + 12U, "GNU\0", 4U) == 0);
    size_t descriptor_size = test_elf_read_u32(plan->output + 4U);
    assert(descriptor_size == plan->output_size - 16U);

    size_t cursor = 16U;
    while (cursor < plan->output_size) {
        assert(plan->output_size - cursor >= 8U);
        uint32_t type = test_elf_read_u32(plan->output + cursor);
        uint32_t data_size = test_elf_read_u32(
                plan->output + cursor + 4U);
        assert((size_t) data_size <= plan->output_size - cursor - 8U);
        if (type == expected_type) {
            assert(data_size == 4U || data_size == 8U);
            return data_size == 4U
                           ? test_elf_read_u32(plan->output + cursor + 8U)
                           : test_elf_read_u64(plan->output + cursor + 8U);
        }
        cursor = test_elf_align(
                cursor + 8U + (size_t) data_size, 8U);
    }
    assert(false);
    return 0U;
}

static void test_glibc_concatenated_property_notes(void) {
    /* Exact .note.gnu.property bytes from glibc's x86_64 errlist-data.o. */
    static const uint8_t notes[] = {
            0x04U,
            0x00U,
            0x00U,
            0x00U,
            0x10U,
            0x00U,
            0x00U,
            0x00U,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x47U,
            0x4eU,
            0x55U,
            0x00U,
            0x02U,
            0x00U,
            0x00U,
            0xc0U,
            0x04U,
            0x00U,
            0x00U,
            0x00U,
            0x03U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x04U,
            0x00U,
            0x00U,
            0x00U,
            0x10U,
            0x00U,
            0x00U,
            0x00U,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x47U,
            0x4eU,
            0x55U,
            0x00U,
            0x02U,
            0x00U,
            0x00U,
            0xc0U,
            0x04U,
            0x00U,
            0x00U,
            0x00U,
            0x03U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    ld_elf_property_input_t input = {
            .data = notes,
            .size = sizeof(notes),
            .section_index = 4U,
    };
    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(
                   &plan, LD_ARCH_AMD64, &input, 1U) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output_size == 32U);
    assert(property_output_value(
                   &plan, LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND) == 3U);
    ld_elf_property_plan_deinit(&plan);
}

static void test_property_first_note_wins_within_object(void) {
    uint8_t first_note[96], second_note[96];
    test_property_note_entry_t first = {
            .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
            .data_size = 4U,
            .value = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_IBT |
                     LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK,
    };
    test_property_note_entry_t second[2] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
                    .data_size = 4U,
                    .value = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_IBT,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED,
                    .data_size = 4U,
                    .value = 2U,
            },
    };
    size_t first_size = append_property_note(
            first_note, sizeof(first_note), 0U, &first, 1U);
    size_t second_size = append_property_note(
            second_note, sizeof(second_note), 0U, second, 2U);
    ld_elf_property_input_t inputs[2] = {
            {
                    .data = first_note,
                    .size = first_size,
                    .section_index = 4U,
            },
            {
                    .data = second_note,
                    .size = second_size,
                    .section_index = 7U,
            },
    };

    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(
                   &plan, LD_ARCH_AMD64, inputs, 2U) ==
           LD_ELF_PROPERTY_OK);

    first.value = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK;
    second[0].value = first.value;
    second[0].type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND;
    second[1].value = 1U;
    size_t other_size = append_property_note(
            second_note, sizeof(second_note), 0U, second, 2U);
    ld_elf_property_input_t other = {
            .data = second_note,
            .size = other_size,
            .section_index = 4U,
    };
    assert(ld_elf_property_add_object(
                   &plan, LD_ARCH_AMD64, &other, 1U) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);

    assert(property_output_value(
                   &plan, LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND) ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK);
    assert(property_output_value(
                   &plan, LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED) == 3U);
    ld_elf_property_plan_deinit(&plan);
}

static void test_property_descriptor_duplicate_is_rejected(void) {
    uint8_t note[96];
    test_property_note_entry_t entries[2] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
                    .data_size = 4U,
                    .value = 3U,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
                    .data_size = 4U,
                    .value = 1U,
            },
    };
    size_t size = append_property_note(
            note, sizeof(note), 0U, entries, 2U);
    ld_elf_property_input_t input = {
            .data = note,
            .size = size,
            .section_index = 4U,
    };
    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(
                   &plan, LD_ARCH_AMD64, &input, 1U) ==
           LD_ELF_PROPERTY_DUPLICATE_PROPERTY);
    assert(plan.error_input_index == 0U);
    assert(plan.error_offset == 32U);
    assert(plan.error_property_type ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND);
    ld_elf_property_plan_deinit(&plan);
}

static void test_later_property_note_is_still_validated(void) {
    uint8_t notes[96];
    test_property_note_entry_t entry = {
            .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
            .data_size = 4U,
            .value = 3U,
    };
    size_t first_size = append_property_note(
            notes, sizeof(notes), 0U, &entry, 1U);
    entry.data_size = 8U;
    size_t second_size = append_property_note(
            notes, sizeof(notes), first_size, &entry, 1U);
    ld_elf_property_input_t input = {
            .data = notes,
            .size = first_size + second_size,
            .section_index = 4U,
    };
    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(
                   &plan, LD_ARCH_AMD64, &input, 1U) ==
           LD_ELF_PROPERTY_INVALID_PROPERTY);
    assert(plan.error_input_index == 0U);
    assert(plan.error_offset == first_size + 16U);
    assert(plan.error_property_type ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND);
    ld_elf_property_plan_deinit(&plan);
}

void test_ld_elf_property_notes(void) {
    test_glibc_concatenated_property_notes();
    test_property_first_note_wins_within_object();
    test_property_descriptor_duplicate_is_rejected();
    test_later_property_note_is_still_validated();
}

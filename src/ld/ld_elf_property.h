#ifndef NATURE_LD_ELF_PROPERTY_H
#define NATURE_LD_ELF_PROPERTY_H

#include "ld.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LD_ELF_PROPERTY_OK = 0,
    LD_ELF_PROPERTY_INVALID_ARGUMENT,
    LD_ELF_PROPERTY_OUT_OF_MEMORY,
    LD_ELF_PROPERTY_TRUNCATED_NOTE,
    LD_ELF_PROPERTY_INVALID_NOTE,
    LD_ELF_PROPERTY_INVALID_OWNER,
    LD_ELF_PROPERTY_INVALID_PROPERTY,
    LD_ELF_PROPERTY_UNSORTED_PROPERTY,
    LD_ELF_PROPERTY_DUPLICATE_PROPERTY,
    LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY,
    LD_ELF_PROPERTY_OUTPUT_OVERFLOW,
} ld_elf_property_result_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t section_index;
} ld_elf_property_input_t;

typedef struct ld_elf_property_entry ld_elf_property_entry_t;

typedef struct {
    ld_elf_property_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t object_count;
    uint8_t *output;
    size_t output_size;
    size_t error_input_index;
    size_t error_offset;
    uint32_t error_property_type;
    bool finalized;
} ld_elf_property_plan_t;

void ld_elf_property_plan_init(ld_elf_property_plan_t *plan);
void ld_elf_property_plan_deinit(ld_elf_property_plan_t *plan);

ld_elf_property_result_t ld_elf_property_add_object(
        ld_elf_property_plan_t *plan, ld_arch_t arch,
        const ld_elf_property_input_t *inputs, size_t input_count);

ld_elf_property_result_t ld_elf_property_finalize(
        ld_elf_property_plan_t *plan);

const char *ld_elf_property_result_string(ld_elf_property_result_t result);

#endif

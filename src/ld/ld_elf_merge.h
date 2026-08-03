#ifndef NATURE_LD_ELF_MERGE_H
#define NATURE_LD_ELF_MERGE_H

#include "ld_elf_internal.h"

typedef struct ld_elf_merge_group ld_elf_merge_group_t;
typedef struct ld_elf_merge_input ld_elf_merge_input_t;

typedef struct {
    ld_elf_context_t *ctx;
    ld_elf_merge_group_t *first_group;
    ld_elf_merge_group_t *last_group;
    ld_elf_merge_input_t *inputs;
} ld_elf_merge_plan_t;

void ld_elf_merge_plan_init(ld_elf_merge_plan_t *plan,
                            ld_elf_context_t *ctx);
void ld_elf_merge_plan_deinit(ld_elf_merge_plan_t *plan);

bool ld_elf_merge_section_eligible(const ld_elf_section_t *section);
int ld_elf_merge_add_section(ld_elf_merge_plan_t *plan,
                             ld_elf_section_t *section,
                             ld_elf_output_section_t *output);
int ld_elf_merge_finalize(ld_elf_merge_plan_t *plan);
int ld_elf_merge_emit(const ld_elf_merge_plan_t *plan);

bool ld_elf_merge_map_input(const ld_elf_section_t *section,
                            uint64_t input_offset, uint64_t width,
                            uint64_t *output_offset, bool *alive,
                            uint64_t *available);

#endif

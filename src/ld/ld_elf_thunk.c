#include "ld_elf_thunk.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * The grouping scale and ADRP/ADD/BR sequence follow Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224, Elf.createThunks and
 * Elf/Thunk.zig. Nature keeps the relocation addend in the deduplication key
 * and makes the thunk itself target S + A; the referenced Zig revision
 * assumes the usual zero-addend CALL26/JUMP26 form.
 */

static void ld_elf_aarch64_write_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t) value;
    output[1] = (uint8_t) (value >> 8U);
    output[2] = (uint8_t) (value >> 16U);
    output[3] = (uint8_t) (value >> 24U);
}

void ld_elf_aarch64_thunk_plan_init(ld_elf_aarch64_thunk_plan_t *plan) {
    if (plan) memset(plan, 0, sizeof(*plan));
}

void ld_elf_aarch64_thunk_plan_deinit(ld_elf_aarch64_thunk_plan_t *plan) {
    if (!plan) return;
    for (size_t i = 0; i < plan->group_count; i++) {
        free(plan->groups[i].entries);
    }
    free(plan->groups);
    memset(plan, 0, sizeof(*plan));
}

bool ld_elf_aarch64_thunk_group_append(
        ld_elf_aarch64_thunk_plan_t *plan, uint64_t payload_start_offset,
        size_t placement_index, uint32_t *group_index) {
    if (!plan || !group_index || plan->group_count >= UINT32_MAX) return false;
    if (plan->group_count == plan->group_capacity) {
        if (plan->group_capacity > SIZE_MAX / 2U) return false;
        size_t next = plan->group_capacity ? plan->group_capacity * 2U : 8U;
        if (next > SIZE_MAX / sizeof(*plan->groups)) return false;
        void *groups = realloc(plan->groups, next * sizeof(*plan->groups));
        if (!groups) return false;
        plan->groups = groups;
        plan->group_capacity = next;
    }
    uint32_t index = (uint32_t) plan->group_count;
    ld_elf_aarch64_thunk_group_t *group = &plan->groups[plan->group_count++];
    memset(group, 0, sizeof(*group));
    group->payload_start_offset = payload_start_offset;
    group->payload_end_offset = payload_start_offset;
    group->thunk_output_offset = payload_start_offset;
    group->first_placement_index = placement_index;
    group->last_placement_index = placement_index;
    *group_index = index;
    return true;
}

static bool ld_elf_aarch64_thunk_key_equal(
        const ld_elf_aarch64_thunk_key_t *left,
        const ld_elf_aarch64_thunk_key_t *right) {
    return left->global == right->global && left->object == right->object &&
           left->symbol_index == right->symbol_index &&
           left->addend == right->addend;
}

bool ld_elf_aarch64_thunk_entry_find_or_add(
        ld_elf_aarch64_thunk_group_t *group,
        const ld_elf_aarch64_thunk_key_t *key, uint32_t *entry_index,
        bool *added) {
    if (!group || !key || !entry_index || !added) return false;
    for (size_t i = 0; i < group->entry_count; i++) {
        if (!ld_elf_aarch64_thunk_key_equal(&group->entries[i].key, key))
            continue;
        *entry_index = (uint32_t) i;
        *added = false;
        return true;
    }
    if (group->entry_count >= UINT32_MAX) return false;
    if (group->entry_count == group->entry_capacity) {
        if (group->entry_capacity > SIZE_MAX / 2U) return false;
        size_t next = group->entry_capacity ? group->entry_capacity * 2U : 8U;
        if (next > SIZE_MAX / sizeof(*group->entries)) return false;
        void *entries = realloc(group->entries,
                                next * sizeof(*group->entries));
        if (!entries) return false;
        group->entries = entries;
        group->entry_capacity = next;
    }
    uint32_t index = (uint32_t) group->entry_count;
    ld_elf_aarch64_thunk_entry_t *entry =
            &group->entries[group->entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->key = *key;
    *entry_index = index;
    *added = true;
    return true;
}

bool ld_elf_aarch64_branch26_fits(uint64_t target, uint64_t place,
                                  int64_t *displacement) {
    if (((target | place) & 3U) != 0U) return false;
    if (target >= place) {
        uint64_t magnitude = target - place;
        if (magnitude > UINT64_C(0x07fffffc)) return false;
        if (displacement) *displacement = (int64_t) magnitude;
        return true;
    }
    uint64_t magnitude = place - target;
    if (magnitude > UINT64_C(0x08000000)) return false;
    if (displacement) *displacement = -(int64_t) magnitude;
    return true;
}

ld_elf_aarch64_thunk_encode_result_t ld_elf_aarch64_thunk_encode(
        uint8_t output[LD_ELF_AARCH64_THUNK_SIZE], uint64_t thunk_address,
        uint64_t target_address) {
    if (!output || ((thunk_address | target_address) & 3U) != 0U)
        return LD_ELF_AARCH64_THUNK_ENCODE_UNALIGNED;

    uint64_t thunk_page = thunk_address >> 12U;
    uint64_t target_page = target_address >> 12U;
    int64_t page_delta;
    if (target_page >= thunk_page) {
        uint64_t magnitude = target_page - thunk_page;
        if (magnitude > UINT64_C(0xfffff))
            return LD_ELF_AARCH64_THUNK_ENCODE_RANGE;
        page_delta = (int64_t) magnitude;
    } else {
        uint64_t magnitude = thunk_page - target_page;
        if (magnitude > UINT64_C(0x100000))
            return LD_ELF_AARCH64_THUNK_ENCODE_RANGE;
        page_delta = -(int64_t) magnitude;
    }

    uint32_t encoded_pages = (uint32_t) ((uint64_t) page_delta & 0x1fffffU);
    uint32_t adrp = 0x90000010U |
                    ((encoded_pages & 0x3U) << 29U) |
                    (((encoded_pages >> 2U) & 0x7ffffU) << 5U);
    uint32_t add = 0x91000210U |
                   ((uint32_t) (target_address & 0xfffU) << 10U);
    uint32_t branch = 0xd61f0200U;

    uint8_t encoded[LD_ELF_AARCH64_THUNK_SIZE];
    ld_elf_aarch64_write_u32(encoded, adrp);
    ld_elf_aarch64_write_u32(encoded + 4U, add);
    ld_elf_aarch64_write_u32(encoded + 8U, branch);
    memcpy(output, encoded, sizeof(encoded));
    return LD_ELF_AARCH64_THUNK_ENCODE_OK;
}

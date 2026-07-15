#ifndef NATURE_LD_MACHO_EH_FRAME_H
#define NATURE_LD_MACHO_EH_FRAME_H

#include "ld_internal.h"

/*
 * A live input FDE and the information needed to superimpose it on the
 * compact-unwind records for the same function.  The raw __eh_frame section
 * is still merged by ld_macho.c, so output_offset is the final FDE record
 * offset within that merged section and can be used as the 24-bit DWARF hint.
 */
typedef struct {
    ld_object_t *object;
    uint32_t section_index;
    uint32_t record_offset;
    uint32_t record_size;
    uint64_t output_offset;
    uint64_t function_address;
    uint64_t function_size;
    uint32_t personality_symbol_index;
    uint64_t lsda_address;
    bool has_personality;
    bool has_lsda;
} ld_macho_fde_t;

typedef struct {
    ld_macho_fde_t *items;
    size_t count;
    size_t capacity;
} ld_macho_fde_list_t;

/*
 * Parses all selected arm64 Mach-O __TEXT,__eh_frame input sections.  This is
 * deliberately a record parser rather than a DWARF instruction interpreter:
 * it validates CIE/FDE structure and augmentation data, resolves the fields
 * required by compact-unwind superposition, and leaves CFI opcodes untouched.
 *
 * The implementation follows Zig's MachO/eh_frame.zig and
 * Object.initEhFrameRecords/parseUnwindRecords at commit 738d2be9.  Zig is MIT
 * licensed; see src/ld/ZIG-LICENSE.txt.
 */
int ld_macho_eh_frame_collect(ld_context_t *ctx, ld_macho_fde_list_t *list);
void ld_macho_eh_frame_deinit(ld_macho_fde_list_t *list);

#endif

#ifndef NATURE_LD_ELF_RELOC_H
#define NATURE_LD_ELF_RELOC_H

#include "ld_elf_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Result of the allocation-time relocation scan.  The ELF backend uses these
 * flags before layout to reserve synthetic entries.  symbol_is_import only
 * affects relocations which may have to branch through a PLT.
 */
typedef struct {
    bool needs_got;
    bool needs_got_base;
    bool needs_plt;
    bool pc_relative;
    size_t write_width;
} ld_elf_reloc_scan_result_t;

typedef enum {
    LD_ELF_RELOC_GOT_NONE = 0,
    LD_ELF_RELOC_GOT_ORDINARY,
    LD_ELF_RELOC_GOT_TP,
    LD_ELF_RELOC_GOT_TLSGD,
} ld_elf_reloc_got_kind_t;

/*
 * Values used by the ELF relocation formulae:
 *
 *   P  place_address
 *   S  symbol_address
 *   G  got_entry_address (the final address of this symbol's GOT entry)
 *   DTP tls_block_address (the start of the executable's TLS block)
 *   TP thread_pointer_address
 *
 * The relocation's explicit addend supplies A.  For an imported branch,
 * symbol_address must already name the selected PLT entry.
 */
typedef struct {
    uint64_t place_address;
    uint64_t symbol_address;
    uint64_t got_entry_address;
    uint64_t got_base_address;
    uint64_t symbol_size;
    uint64_t tls_block_address;
    uint64_t thread_pointer_address;
    uint64_t image_base_address;
    const char *symbol_name;

    /*
     * R_RISCV_PCREL_LO12_* names the local label at its matching HI20
     * relocation, rather than the final target. The layout owner resolves
     * that HI20 symbol and supplies the complete pair here.
     */
    struct {
        bool present;
        uint32_t type;
        uint64_t place_address;
        uint64_t symbol_address;
        uint64_t got_entry_address;
        int64_t addend;
        const char *symbol_name;
    } paired_hi;
} ld_elf_reloc_values_t;

const char *ld_elf_relocation_name(ld_arch_t arch, uint32_t type);
bool ld_elf_relocation_supported(ld_arch_t arch, uint32_t type);
/*
 * Returns whether Zig 738d2be9 routes this allocated relocation through the
 * architecture scanReloc path used by a static PIE.  ET_EXEC deliberately
 * supports a wider compatibility set; callers must apply this predicate only
 * to allocated relocations while options.pie is enabled.
 */
bool ld_elf_relocation_supported_in_static_pie(ld_arch_t arch,
                                               uint32_t type);
bool ld_elf_relocation_needs_got(ld_arch_t arch, uint32_t type);
bool ld_elf_relocation_needs_got_base(ld_arch_t arch, uint32_t type);
ld_elf_reloc_got_kind_t ld_elf_relocation_got_kind(ld_arch_t arch,
                                                   uint32_t type);

/* Returns SIZE_MAX for an unsupported relocation and zero for R_*_NONE. */
size_t ld_elf_relocation_write_width(ld_arch_t arch, uint32_t type);

int ld_elf_relocation_scan(ld_elf_context_t *ctx,
                           const ld_elf_object_t *object,
                           const ld_elf_section_t *section,
                           const ld_elf_relocation_t *relocation,
                           bool symbol_is_import,
                           ld_elf_reloc_scan_result_t *result);

int ld_elf_relocation_check_write(ld_elf_context_t *ctx,
                                  const ld_elf_object_t *object,
                                  const ld_elf_section_t *section,
                                  const ld_elf_relocation_t *relocation,
                                  size_t place_size,
                                  const char *symbol_name);

/*
 * Static x86-64 ELF executables do not have a dynamic TLS resolver.  Validate
 * compiler-emitted TLSGD/TLSLD + __tls_get_addr pairs before symbol
 * resolution and mark the resolver relocation as consumed.  This mirrors
 * Zig's scanRelocs iterator skip for static TLS relaxation.
 */
int ld_elf_relocation_prepare_x86_tls_sequences(
        ld_elf_context_t *ctx, ld_elf_object_t *object);

bool ld_elf_relocation_is_x86_tls_pair_start(uint32_t type);

/* section_data is the beginning of this input section's output placement. */
int ld_elf_relocation_apply_x86_tls_pair(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation,
        const ld_elf_relocation_t *helper, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values);

int ld_elf_relocation_apply_x86_gottpoff(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values);

/*
 * Zig-style static GOTPCRELX relaxation. The predicate validates one of the
 * supported compiler instruction forms in the immutable input section. The
 * apply function revalidates and rewrites that form in the output section.
 */
bool ld_elf_relocation_can_relax_x86_gotpcrelx(
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation);

int ld_elf_relocation_apply_x86_gotpcrelx(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values);

/* Apply a validated RISC-V SET/SUB ULEB128 pair as one expression. */
int ld_elf_relocation_apply_riscv_uleb_pair(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *set_relocation,
        const ld_elf_relocation_t *sub_relocation, uint8_t *place,
        size_t place_size, const ld_elf_reloc_values_t *set_values,
        const ld_elf_reloc_values_t *sub_values);

/* place points at the relocation site and place_size is the remaining size. */
int ld_elf_relocation_apply(ld_elf_context_t *ctx,
                            const ld_elf_object_t *object,
                            const ld_elf_section_t *section,
                            const ld_elf_relocation_t *relocation,
                            uint8_t *place,
                            size_t place_size,
                            const ld_elf_reloc_values_t *values);

#endif

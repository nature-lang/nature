#ifndef NATURE_LD_ELF_IFUNC_H
#define NATURE_LD_ELF_IFUNC_H

#include "ld.h"

#include <stddef.h>
#include <stdint.h>

#define LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE 16U
#define LD_ELF_IFUNC_RELA_SIZE 24U

typedef enum {
    LD_ELF_IFUNC_OK = 0,
    LD_ELF_IFUNC_INVALID_ARGUMENT,
    LD_ELF_IFUNC_UNSUPPORTED_ARCH,
    LD_ELF_IFUNC_UNALIGNED,
    LD_ELF_IFUNC_RANGE,
} ld_elf_ifunc_result_t;

/*
 * Encodes Zig Elf/PltGotSection's non-lazy IFUNC trampoline. The destination
 * remains unchanged on failure. x86_64 uses ENDBR64/JMP [RIP+disp32],
 * AArch64 uses ADRP/LDR/BR/NOP, and RISC-V64 uses
 * AUIPC/LD/JALR/NOP through the symbol's ordinary GOT slot.
 */
ld_elf_ifunc_result_t ld_elf_ifunc_encode_pltgot(
        ld_arch_t arch, uint8_t output[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE],
        uint64_t entry_address, uint64_t got_address);

/* Encodes one little-endian Elf64_Rela with r_sym=0 and R_*_IRELATIVE. */
ld_elf_ifunc_result_t ld_elf_ifunc_encode_irelative(
        ld_arch_t arch, uint8_t output[LD_ELF_IFUNC_RELA_SIZE],
        uint64_t target_address, int64_t resolver_addend);

const char *ld_elf_ifunc_result_string(ld_elf_ifunc_result_t result);

#endif

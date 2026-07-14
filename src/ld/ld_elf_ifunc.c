#include "ld_elf_ifunc.h"

#include "elf_format.h"

#include <limits.h>
#include <string.h>

/*
 * Direct translation of the x86_64 and AArch64 PltGotSection encoders plus
 * relocation.encode(.irel) from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. That revision leaves the
 * RISC-V PltGotSection encoder as a TODO, so the RISC-V64 sequence follows
 * the psABI and GNU ld 2.42 output. Nature validates every operand before
 * copying the temporary encoding to the caller's output buffer.
 */

static void ld_elf_ifunc_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_ifunc_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_ifunc_write_u32(bytes, (uint32_t) value);
    ld_elf_ifunc_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static ld_elf_ifunc_result_t ld_elf_ifunc_encode_x86_64(
        uint8_t output[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE],
        uint64_t entry_address, uint64_t got_address) {
    if (entry_address > UINT64_MAX - 10U)
        return LD_ELF_IFUNC_RANGE;
    uint64_t next_instruction = entry_address + 10U;
    int64_t displacement;
    if (got_address >= next_instruction) {
        uint64_t magnitude = got_address - next_instruction;
        if (magnitude > INT32_MAX) return LD_ELF_IFUNC_RANGE;
        displacement = (int64_t) magnitude;
    } else {
        uint64_t magnitude = next_instruction - got_address;
        if (magnitude > UINT64_C(0x80000000))
            return LD_ELF_IFUNC_RANGE;
        displacement = -(int64_t) magnitude;
    }

    uint8_t encoded[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE] = {
            0xf3U,
            0x0fU,
            0x1eU,
            0xfaU, /* endbr64 */
            0xffU,
            0x25U,
            0U,
            0U,
            0U,
            0U, /* jmp [rip+disp32] */
            0xccU,
            0xccU,
            0xccU,
            0xccU,
            0xccU,
            0xccU,
    };
    ld_elf_ifunc_write_u32(encoded + 6U, (uint32_t) displacement);
    memcpy(output, encoded, sizeof(encoded));
    return LD_ELF_IFUNC_OK;
}

static ld_elf_ifunc_result_t ld_elf_ifunc_encode_aarch64(
        uint8_t output[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE],
        uint64_t entry_address, uint64_t got_address) {
    if ((entry_address & 3U) != 0U || (got_address & 7U) != 0U)
        return LD_ELF_IFUNC_UNALIGNED;

    uint64_t entry_page = entry_address >> 12U;
    uint64_t got_page = got_address >> 12U;
    int64_t page_delta;
    if (got_page >= entry_page) {
        uint64_t magnitude = got_page - entry_page;
        if (magnitude > UINT64_C(0xfffff))
            return LD_ELF_IFUNC_RANGE;
        page_delta = (int64_t) magnitude;
    } else {
        uint64_t magnitude = entry_page - got_page;
        if (magnitude > UINT64_C(0x100000))
            return LD_ELF_IFUNC_RANGE;
        page_delta = -(int64_t) magnitude;
    }

    uint32_t encoded_pages =
            (uint32_t) ((uint64_t) page_delta & UINT32_C(0x1fffff));
    uint32_t page_offset = (uint32_t) (got_address & UINT64_C(0xfff));
    uint32_t adrp = UINT32_C(0x90000010) |
                    ((encoded_pages & 3U) << 29U) |
                    (((encoded_pages >> 2U) & UINT32_C(0x7ffff)) << 5U);
    uint32_t ldr = UINT32_C(0xf9400211) |
                   ((page_offset >> 3U) << 10U);
    uint8_t encoded[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE];
    ld_elf_ifunc_write_u32(encoded, adrp);
    ld_elf_ifunc_write_u32(encoded + 4U, ldr);
    ld_elf_ifunc_write_u32(encoded + 8U, UINT32_C(0xd61f0220));
    ld_elf_ifunc_write_u32(encoded + 12U, UINT32_C(0xd503201f));
    memcpy(output, encoded, sizeof(encoded));
    return LD_ELF_IFUNC_OK;
}

static ld_elf_ifunc_result_t ld_elf_ifunc_encode_riscv64(
        uint8_t output[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE],
        uint64_t entry_address, uint64_t got_address) {
    if ((entry_address & 3U) != 0U || (got_address & 7U) != 0U)
        return LD_ELF_IFUNC_UNALIGNED;

    int64_t delta;
    if (got_address >= entry_address) {
        uint64_t magnitude = got_address - entry_address;
        /*
         * Rounding PCREL_HI20 at 0x7ffff800 would produce +0x80000,
         * which is outside AUIPC's signed 20-bit immediate.
         */
        if (magnitude > UINT64_C(0x7ffff7ff))
            return LD_ELF_IFUNC_RANGE;
        delta = (int64_t) magnitude;
    } else {
        uint64_t magnitude = entry_address - got_address;
        if (magnitude > UINT64_C(0x80000000))
            return LD_ELF_IFUNC_RANGE;
        delta = -(int64_t) magnitude;
    }

    /* Portable floor((delta + 0x800) / 0x1000), without shifting negative. */
    int64_t rounded = delta + INT64_C(0x800);
    int64_t high = rounded >= 0
                           ? rounded / INT64_C(0x1000)
                           : -((-rounded + INT64_C(0xfff)) /
                               INT64_C(0x1000));
    int64_t low = delta - high * INT64_C(0x1000);
    if (high < -INT64_C(0x80000) || high > INT64_C(0x7ffff) ||
        low < -INT64_C(0x800) || low > INT64_C(0x7ff)) {
        return LD_ELF_IFUNC_RANGE;
    }

    uint32_t auipc = UINT32_C(0x00000e17) |
                     (((uint32_t) high & UINT32_C(0xfffff)) << 12U);
    uint32_t load = UINT32_C(0x000e3e03) |
                    (((uint32_t) low & UINT32_C(0xfff)) << 20U);
    uint8_t encoded[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE];
    ld_elf_ifunc_write_u32(encoded, auipc);
    ld_elf_ifunc_write_u32(encoded + 4U, load);
    ld_elf_ifunc_write_u32(encoded + 8U, UINT32_C(0x000e0367));
    ld_elf_ifunc_write_u32(encoded + 12U, UINT32_C(0x00000013));
    memcpy(output, encoded, sizeof(encoded));
    return LD_ELF_IFUNC_OK;
}

ld_elf_ifunc_result_t ld_elf_ifunc_encode_pltgot(
        ld_arch_t arch, uint8_t output[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE],
        uint64_t entry_address, uint64_t got_address) {
    if (!output) return LD_ELF_IFUNC_INVALID_ARGUMENT;
    switch (arch) {
        case LD_ARCH_AMD64:
            return ld_elf_ifunc_encode_x86_64(output, entry_address,
                                              got_address);
        case LD_ARCH_ARM64:
            return ld_elf_ifunc_encode_aarch64(output, entry_address,
                                               got_address);
        case LD_ARCH_RISCV64:
            return ld_elf_ifunc_encode_riscv64(output, entry_address,
                                               got_address);
        default:
            return LD_ELF_IFUNC_UNSUPPORTED_ARCH;
    }
}

ld_elf_ifunc_result_t ld_elf_ifunc_encode_irelative(
        ld_arch_t arch, uint8_t output[LD_ELF_IFUNC_RELA_SIZE],
        uint64_t target_address, int64_t resolver_addend) {
    if (!output) return LD_ELF_IFUNC_INVALID_ARGUMENT;
    uint32_t type;
    switch (arch) {
        case LD_ARCH_AMD64:
            type = LD_ELF_R_X86_64_IRELATIVE;
            break;
        case LD_ARCH_ARM64:
            type = LD_ELF_R_AARCH64_IRELATIVE;
            break;
        case LD_ARCH_RISCV64:
            type = LD_ELF_R_RISCV_IRELATIVE;
            break;
        default:
            return LD_ELF_IFUNC_UNSUPPORTED_ARCH;
    }
    uint8_t encoded[LD_ELF_IFUNC_RELA_SIZE] = {0};
    ld_elf_ifunc_write_u64(encoded, target_address);
    ld_elf_ifunc_write_u64(encoded + 8U, type);
    ld_elf_ifunc_write_u64(encoded + 16U, (uint64_t) resolver_addend);
    memcpy(output, encoded, sizeof(encoded));
    return LD_ELF_IFUNC_OK;
}

const char *ld_elf_ifunc_result_string(ld_elf_ifunc_result_t result) {
    switch (result) {
        case LD_ELF_IFUNC_OK:
            return "ok";
        case LD_ELF_IFUNC_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_IFUNC_UNSUPPORTED_ARCH:
            return "unsupported architecture";
        case LD_ELF_IFUNC_UNALIGNED:
            return "unaligned PLT/GOT address";
        case LD_ELF_IFUNC_RANGE:
            return "PLT/GOT displacement is out of range";
    }
    return "unknown IFUNC encoding error";
}

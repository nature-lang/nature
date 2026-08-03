#include "ld_elf_reloc.h"

#include "ld_elf_riscv_uleb.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * The scan/apply split and AArch64/RISC-V formulae follow Zig's ELF Atom
 * relocation implementation at src/link/Elf/Atom.zig:1483-2015 and
 * src/link/riscv.zig, commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224.  This is an independent C
 * translation; Zig's MIT license is reproduced in ZIG-LICENSE.txt.
 *
 * Relocation numbers stay private so this module remains independent of host
 * ELF headers and can be compiled on Darwin while producing Linux binaries.
 */
enum {
    LD_R_AARCH64_NONE = 0,
    LD_R_AARCH64_ABS64 = 257,
    LD_R_AARCH64_ABS32 = 258,
    LD_R_AARCH64_ABS16 = 259,
    LD_R_AARCH64_PREL64 = 260,
    LD_R_AARCH64_PREL32 = 261,
    LD_R_AARCH64_PREL16 = 262,
    LD_R_AARCH64_MOVW_UABS_G0 = 263,
    LD_R_AARCH64_MOVW_UABS_G0_NC = 264,
    LD_R_AARCH64_MOVW_UABS_G1 = 265,
    LD_R_AARCH64_MOVW_UABS_G1_NC = 266,
    LD_R_AARCH64_MOVW_UABS_G2 = 267,
    LD_R_AARCH64_MOVW_UABS_G2_NC = 268,
    LD_R_AARCH64_MOVW_UABS_G3 = 269,
    LD_R_AARCH64_LD_PREL_LO19 = 273,
    LD_R_AARCH64_ADR_PREL_LO21 = 274,
    LD_R_AARCH64_ADR_PREL_PG_HI21 = 275,
    LD_R_AARCH64_ADD_ABS_LO12_NC = 277,
    LD_R_AARCH64_LDST8_ABS_LO12_NC = 278,
    LD_R_AARCH64_TSTBR14 = 279,
    LD_R_AARCH64_CONDBR19 = 280,
    LD_R_AARCH64_JUMP26 = 282,
    LD_R_AARCH64_CALL26 = 283,
    LD_R_AARCH64_LDST16_ABS_LO12_NC = 284,
    LD_R_AARCH64_LDST32_ABS_LO12_NC = 285,
    LD_R_AARCH64_LDST64_ABS_LO12_NC = 286,
    LD_R_AARCH64_LDST128_ABS_LO12_NC = 299,
    LD_R_AARCH64_GOT_LD_PREL19 = 309,
    LD_R_AARCH64_ADR_GOT_PAGE = 311,
    LD_R_AARCH64_LD64_GOT_LO12_NC = 312,
    LD_R_AARCH64_LD64_GOTPAGE_LO15 = 313,
    LD_R_AARCH64_TLSGD_ADR_PAGE21 = 513,
    LD_R_AARCH64_TLSGD_ADD_LO12_NC = 514,
    LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 = 541,
    LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC = 542,
    LD_R_AARCH64_TLSLE_ADD_TPREL_HI12 = 549,
    LD_R_AARCH64_TLSLE_ADD_TPREL_LO12 = 550,
    LD_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC = 551,
    LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12 = 552,
    LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC = 553,
    LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12 = 554,
    LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC = 555,
    LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12 = 556,
    LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC = 557,
    LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12 = 558,
    LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC = 559,
    LD_R_AARCH64_TLSDESC_ADR_PAGE21 = 562,
    LD_R_AARCH64_TLSDESC_LD64_LO12 = 563,
    LD_R_AARCH64_TLSDESC_ADD_LO12 = 564,
    LD_R_AARCH64_TLSDESC_CALL = 569,
    LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12 = 570,
    LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC = 571,
};

enum {
    LD_R_X86_64_NONE = 0,
    LD_R_X86_64_64 = 1,
    LD_R_X86_64_PC32 = 2,
    LD_R_X86_64_GOT32 = 3,
    LD_R_X86_64_PLT32 = 4,
    LD_R_X86_64_GOTPCREL = 9,
    LD_R_X86_64_32 = 10,
    LD_R_X86_64_32S = 11,
    LD_R_X86_64_16 = 12,
    LD_R_X86_64_PC16 = 13,
    LD_R_X86_64_8 = 14,
    LD_R_X86_64_PC8 = 15,
    LD_R_X86_64_DTPOFF64 = 17,
    LD_R_X86_64_TPOFF64 = 18,
    LD_R_X86_64_TLSGD = 19,
    LD_R_X86_64_TLSLD = 20,
    LD_R_X86_64_DTPOFF32 = 21,
    LD_R_X86_64_GOTTPOFF = 22,
    LD_R_X86_64_TPOFF32 = 23,
    LD_R_X86_64_PC64 = 24,
    LD_R_X86_64_GOTOFF64 = 25,
    LD_R_X86_64_GOTPC32 = 26,
    LD_R_X86_64_GOT64 = 27,
    LD_R_X86_64_GOTPCREL64 = 28,
    LD_R_X86_64_GOTPC64 = 29,
    LD_R_X86_64_GOTPLT64 = 30,
    LD_R_X86_64_PLTOFF64 = 31,
    LD_R_X86_64_SIZE32 = 32,
    LD_R_X86_64_SIZE64 = 33,
    LD_R_X86_64_GOTPC32_TLSDESC = 34,
    LD_R_X86_64_TLSDESC_CALL = 35,
    LD_R_X86_64_TLSDESC = 36,
    LD_R_X86_64_GOTPCRELX = 41,
    LD_R_X86_64_REX_GOTPCRELX = 42,
};

enum {
    LD_R_RISCV_NONE = 0,
    LD_R_RISCV_32 = 1,
    LD_R_RISCV_64 = 2,
    LD_R_RISCV_RELATIVE = 3,
    LD_R_RISCV_TLS_DTPREL32 = 8,
    LD_R_RISCV_TLS_DTPREL64 = 9,
    LD_R_RISCV_TLS_TPREL32 = 10,
    LD_R_RISCV_TLS_TPREL64 = 11,
    LD_R_RISCV_BRANCH = 16,
    LD_R_RISCV_JAL = 17,
    LD_R_RISCV_CALL = 18,
    LD_R_RISCV_CALL_PLT = 19,
    LD_R_RISCV_GOT_HI20 = 20,
    LD_R_RISCV_TLS_GOT_HI20 = 21,
    LD_R_RISCV_TLS_GD_HI20 = 22,
    LD_R_RISCV_PCREL_HI20 = 23,
    LD_R_RISCV_PCREL_LO12_I = 24,
    LD_R_RISCV_PCREL_LO12_S = 25,
    LD_R_RISCV_HI20 = 26,
    LD_R_RISCV_LO12_I = 27,
    LD_R_RISCV_LO12_S = 28,
    LD_R_RISCV_TPREL_HI20 = 29,
    LD_R_RISCV_TPREL_LO12_I = 30,
    LD_R_RISCV_TPREL_LO12_S = 31,
    LD_R_RISCV_TPREL_ADD = 32,
    LD_R_RISCV_ADD8 = 33,
    LD_R_RISCV_ADD16 = 34,
    LD_R_RISCV_ADD32 = 35,
    LD_R_RISCV_ADD64 = 36,
    LD_R_RISCV_SUB8 = 37,
    LD_R_RISCV_SUB16 = 38,
    LD_R_RISCV_SUB32 = 39,
    LD_R_RISCV_SUB64 = 40,
    LD_R_RISCV_ALIGN = 43,
    LD_R_RISCV_RVC_BRANCH = 44,
    LD_R_RISCV_RVC_JUMP = 45,
    LD_R_RISCV_RELAX = 51,
    LD_R_RISCV_SUB6 = 52,
    LD_R_RISCV_SET6 = 53,
    LD_R_RISCV_SET8 = 54,
    LD_R_RISCV_SET16 = 55,
    LD_R_RISCV_SET32 = 56,
    LD_R_RISCV_32_PCREL = 57,
    LD_R_RISCV_SET_ULEB128 = 60,
    LD_R_RISCV_SUB_ULEB128 = 61,
};

/*
 * A relocation expression can be wider than int64_t even though every input
 * is 64-bit.  Keep one extra magnitude bit so range checks describe the
 * mathematical value instead of a wrapped intermediate.  This is enough for
 * uint64 + int64 - uint64, whose magnitude is always less than 2^65.
 */
typedef struct {
    uint64_t magnitude;
    bool magnitude_high;
    bool negative;
} ld_elf_reloc_integer_t;

static uint32_t ld_elf_reloc_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) | ((uint32_t) bytes[3] << 24U);
}

static uint16_t ld_elf_reloc_read_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] |
                       ((uint16_t) bytes[1] << 8U));
}

static uint64_t ld_elf_reloc_read_u64(const uint8_t *bytes) {
    return (uint64_t) ld_elf_reloc_read_u32(bytes) |
           ((uint64_t) ld_elf_reloc_read_u32(bytes + 4U) << 32U);
}

static void ld_elf_reloc_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void ld_elf_reloc_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_reloc_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_reloc_write_u32(bytes, (uint32_t) value);
    ld_elf_reloc_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static uint64_t ld_elf_reloc_addend_magnitude(int64_t addend) {
    return (uint64_t) (-(addend + 1)) + 1U;
}

static uint64_t ld_elf_reloc_add_modulo(uint64_t base, int64_t addend) {
    return base + (uint64_t) addend;
}

static void ld_elf_reloc_integer_normalize(ld_elf_reloc_integer_t *value) {
    if (!value->magnitude_high && value->magnitude == 0U) {
        value->negative = false;
    }
}

static void ld_elf_reloc_integer_add(uint64_t base, int64_t addend,
                                     ld_elf_reloc_integer_t *result) {
    memset(result, 0, sizeof(*result));
    if (addend >= 0) {
        uint64_t amount = (uint64_t) addend;
        result->magnitude = base + amount;
        result->magnitude_high = result->magnitude < base;
        return;
    }

    uint64_t amount = ld_elf_reloc_addend_magnitude(addend);
    if (base >= amount) {
        result->magnitude = base - amount;
    } else {
        result->magnitude = amount - base;
        result->negative = true;
    }
    ld_elf_reloc_integer_normalize(result);
}

static void ld_elf_reloc_integer_subtract_unsigned(
        ld_elf_reloc_integer_t *value, uint64_t subtrahend) {
    if (subtrahend == 0U) return;

    if (value->negative) {
        uint64_t previous = value->magnitude;
        value->magnitude += subtrahend;
        if (value->magnitude < previous) value->magnitude_high = true;
        return;
    }

    if (value->magnitude_high) {
        if (value->magnitude >= subtrahend) {
            value->magnitude -= subtrahend;
        } else {
            value->magnitude -= subtrahend;
            value->magnitude_high = false;
        }
    } else if (value->magnitude >= subtrahend) {
        value->magnitude -= subtrahend;
    } else {
        value->magnitude = subtrahend - value->magnitude;
        value->negative = true;
    }
    ld_elf_reloc_integer_normalize(value);
}

static void ld_elf_reloc_integer_expression(
        uint64_t base, int64_t addend, uint64_t subtrahend,
        ld_elf_reloc_integer_t *result) {
    ld_elf_reloc_integer_add(base, addend, result);
    ld_elf_reloc_integer_subtract_unsigned(result, subtrahend);
}

static bool ld_elf_reloc_integer_to_signed(
        const ld_elf_reloc_integer_t *value, unsigned bits, int64_t *result) {
    if (bits == 0U || bits > 64U || value->magnitude_high) return false;

    uint64_t negative_limit = UINT64_C(1) << (bits - 1U);
    uint64_t positive_limit = negative_limit - 1U;
    if (value->negative) {
        if (value->magnitude > negative_limit) return false;
        if (value->magnitude == (UINT64_C(1) << 63U)) {
            *result = INT64_MIN;
        } else {
            *result = -(int64_t) value->magnitude;
        }
    } else {
        if (value->magnitude > positive_limit) return false;
        *result = (int64_t) value->magnitude;
    }
    return true;
}

static bool ld_elf_reloc_integer_to_unsigned(
        const ld_elf_reloc_integer_t *value, unsigned bits,
        uint64_t *result) {
    if (bits == 0U || bits > 64U || value->negative ||
        value->magnitude_high) {
        return false;
    }
    if (bits < 64U && value->magnitude > ((UINT64_C(1) << bits) - 1U)) {
        return false;
    }
    *result = value->magnitude;
    return true;
}

static void ld_elf_reloc_integer_floor_page(
        ld_elf_reloc_integer_t *value) {
    if (value->magnitude_high) {
        value->magnitude = (UINT64_C(1) << 52U) +
                           (value->magnitude >> 12U);
        value->magnitude_high = false;
    } else if (value->negative) {
        uint64_t quotient = value->magnitude >> 12U;
        if ((value->magnitude & 0xfffU) != 0U) quotient++;
        value->magnitude = quotient;
    } else {
        value->magnitude >>= 12U;
    }
    ld_elf_reloc_integer_normalize(value);
}

static bool ld_elf_reloc_expression_signed(uint64_t base, int64_t addend,
                                           uint64_t subtrahend,
                                           unsigned bits, int64_t *result) {
    ld_elf_reloc_integer_t value;
    ld_elf_reloc_integer_expression(base, addend, subtrahend, &value);
    return ld_elf_reloc_integer_to_signed(&value, bits, result);
}

static bool ld_elf_reloc_page_delta(uint64_t base, int64_t addend,
                                    uint64_t place, int64_t *result) {
    ld_elf_reloc_integer_t value;
    ld_elf_reloc_integer_add(base, addend, &value);
    ld_elf_reloc_integer_floor_page(&value);
    ld_elf_reloc_integer_subtract_unsigned(&value, place >> 12U);
    return ld_elf_reloc_integer_to_signed(&value, 21U, result);
}

static bool ld_elf_reloc_signed_fits(int64_t value, unsigned bits) {
    if (bits == 0U || bits > 64U) return false;
    if (bits == 64U) return true;
    int64_t minimum = -((int64_t) 1 << (bits - 1U));
    int64_t maximum = ((int64_t) 1 << (bits - 1U)) - 1;
    return value >= minimum && value <= maximum;
}

static int64_t ld_elf_reloc_floor_shift_12(int64_t value) {
    if (value >= 0) return value / 4096;
    uint64_t magnitude = (uint64_t) (-(value + 1)) + 1U;
    uint64_t quotient = (magnitude + 4095U) / 4096U;
    if (quotient == (uint64_t) INT64_MAX + 1U) return INT64_MIN;
    return -(int64_t) quotient;
}

static int64_t ld_elf_reloc_floor_shift_16(int64_t value) {
    if (value >= 0) return value / 65536;
    uint64_t magnitude = (uint64_t) (-(value + 1)) + 1U;
    uint64_t quotient = (magnitude + 65535U) / 65536U;
    if (quotient == (uint64_t) INT64_MAX + 1U) return INT64_MIN;
    return -(int64_t) quotient;
}

static ld_arch_t ld_elf_reloc_arch(const ld_elf_context_t *ctx,
                                   const ld_elf_object_t *object) {
    if (object) {
        if (object->header.e_machine == LD_ELF_EM_AARCH64) return LD_ARCH_ARM64;
        if (object->header.e_machine == LD_ELF_EM_X86_64) return LD_ARCH_AMD64;
        if (object->header.e_machine == LD_ELF_EM_RISCV) return LD_ARCH_RISCV64;
    }
    if (ctx && ctx->options) return ctx->options->arch;
    return 0;
}

const char *ld_elf_relocation_name(ld_arch_t arch, uint32_t type) {
    if (arch == LD_ARCH_ARM64) {
        switch (type) {
            case LD_R_AARCH64_NONE:
                return "R_AARCH64_NONE";
            case LD_R_AARCH64_ABS64:
                return "R_AARCH64_ABS64";
            case LD_R_AARCH64_ABS32:
                return "R_AARCH64_ABS32";
            case LD_R_AARCH64_ABS16:
                return "R_AARCH64_ABS16";
            case LD_R_AARCH64_PREL64:
                return "R_AARCH64_PREL64";
            case LD_R_AARCH64_PREL32:
                return "R_AARCH64_PREL32";
            case LD_R_AARCH64_PREL16:
                return "R_AARCH64_PREL16";
            case LD_R_AARCH64_MOVW_UABS_G0:
                return "R_AARCH64_MOVW_UABS_G0";
            case LD_R_AARCH64_MOVW_UABS_G0_NC:
                return "R_AARCH64_MOVW_UABS_G0_NC";
            case LD_R_AARCH64_MOVW_UABS_G1:
                return "R_AARCH64_MOVW_UABS_G1";
            case LD_R_AARCH64_MOVW_UABS_G1_NC:
                return "R_AARCH64_MOVW_UABS_G1_NC";
            case LD_R_AARCH64_MOVW_UABS_G2:
                return "R_AARCH64_MOVW_UABS_G2";
            case LD_R_AARCH64_MOVW_UABS_G2_NC:
                return "R_AARCH64_MOVW_UABS_G2_NC";
            case LD_R_AARCH64_MOVW_UABS_G3:
                return "R_AARCH64_MOVW_UABS_G3";
            case LD_R_AARCH64_LD_PREL_LO19:
                return "R_AARCH64_LD_PREL_LO19";
            case LD_R_AARCH64_ADR_PREL_LO21:
                return "R_AARCH64_ADR_PREL_LO21";
            case LD_R_AARCH64_ADR_PREL_PG_HI21:
                return "R_AARCH64_ADR_PREL_PG_HI21";
            case LD_R_AARCH64_ADD_ABS_LO12_NC:
                return "R_AARCH64_ADD_ABS_LO12_NC";
            case LD_R_AARCH64_LDST8_ABS_LO12_NC:
                return "R_AARCH64_LDST8_ABS_LO12_NC";
            case LD_R_AARCH64_TSTBR14:
                return "R_AARCH64_TSTBR14";
            case LD_R_AARCH64_CONDBR19:
                return "R_AARCH64_CONDBR19";
            case LD_R_AARCH64_JUMP26:
                return "R_AARCH64_JUMP26";
            case LD_R_AARCH64_CALL26:
                return "R_AARCH64_CALL26";
            case LD_R_AARCH64_LDST16_ABS_LO12_NC:
                return "R_AARCH64_LDST16_ABS_LO12_NC";
            case LD_R_AARCH64_LDST32_ABS_LO12_NC:
                return "R_AARCH64_LDST32_ABS_LO12_NC";
            case LD_R_AARCH64_LDST64_ABS_LO12_NC:
                return "R_AARCH64_LDST64_ABS_LO12_NC";
            case LD_R_AARCH64_LDST128_ABS_LO12_NC:
                return "R_AARCH64_LDST128_ABS_LO12_NC";
            case LD_R_AARCH64_GOT_LD_PREL19:
                return "R_AARCH64_GOT_LD_PREL19";
            case LD_R_AARCH64_ADR_GOT_PAGE:
                return "R_AARCH64_ADR_GOT_PAGE";
            case LD_R_AARCH64_LD64_GOT_LO12_NC:
                return "R_AARCH64_LD64_GOT_LO12_NC";
            case LD_R_AARCH64_LD64_GOTPAGE_LO15:
                return "R_AARCH64_LD64_GOTPAGE_LO15";
            case LD_R_AARCH64_TLSGD_ADR_PAGE21:
                return "R_AARCH64_TLSGD_ADR_PAGE21";
            case LD_R_AARCH64_TLSGD_ADD_LO12_NC:
                return "R_AARCH64_TLSGD_ADD_LO12_NC";
            case LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
                return "R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21";
            case LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
                return "R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC";
            case LD_R_AARCH64_TLSLE_ADD_TPREL_HI12:
                return "R_AARCH64_TLSLE_ADD_TPREL_HI12";
            case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12:
                return "R_AARCH64_TLSLE_ADD_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_ADD_TPREL_LO12_NC";
            case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12:
                return "R_AARCH64_TLSLE_LDST8_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC";
            case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
                return "R_AARCH64_TLSLE_LDST16_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC";
            case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
                return "R_AARCH64_TLSLE_LDST32_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC";
            case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
                return "R_AARCH64_TLSLE_LDST64_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC";
            case LD_R_AARCH64_TLSDESC_ADR_PAGE21:
                return "R_AARCH64_TLSDESC_ADR_PAGE21";
            case LD_R_AARCH64_TLSDESC_LD64_LO12:
                return "R_AARCH64_TLSDESC_LD64_LO12";
            case LD_R_AARCH64_TLSDESC_ADD_LO12:
                return "R_AARCH64_TLSDESC_ADD_LO12";
            case LD_R_AARCH64_TLSDESC_CALL:
                return "R_AARCH64_TLSDESC_CALL";
            case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12:
                return "R_AARCH64_TLSLE_LDST128_TPREL_LO12";
            case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC:
                return "R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC";
            default:
                return "R_AARCH64_UNKNOWN";
        }
    }

    if (arch == LD_ARCH_AMD64) {
        switch (type) {
            case LD_R_X86_64_NONE:
                return "R_X86_64_NONE";
            case LD_R_X86_64_64:
                return "R_X86_64_64";
            case LD_R_X86_64_PC32:
                return "R_X86_64_PC32";
            case LD_R_X86_64_GOT32:
                return "R_X86_64_GOT32";
            case LD_R_X86_64_PLT32:
                return "R_X86_64_PLT32";
            case LD_R_X86_64_GOTPCREL:
                return "R_X86_64_GOTPCREL";
            case LD_R_X86_64_32:
                return "R_X86_64_32";
            case LD_R_X86_64_32S:
                return "R_X86_64_32S";
            case LD_R_X86_64_16:
                return "R_X86_64_16";
            case LD_R_X86_64_PC16:
                return "R_X86_64_PC16";
            case LD_R_X86_64_8:
                return "R_X86_64_8";
            case LD_R_X86_64_PC8:
                return "R_X86_64_PC8";
            case LD_R_X86_64_DTPOFF64:
                return "R_X86_64_DTPOFF64";
            case LD_R_X86_64_TPOFF64:
                return "R_X86_64_TPOFF64";
            case LD_R_X86_64_TLSGD:
                return "R_X86_64_TLSGD";
            case LD_R_X86_64_TLSLD:
                return "R_X86_64_TLSLD";
            case LD_R_X86_64_DTPOFF32:
                return "R_X86_64_DTPOFF32";
            case LD_R_X86_64_GOTTPOFF:
                return "R_X86_64_GOTTPOFF";
            case LD_R_X86_64_TPOFF32:
                return "R_X86_64_TPOFF32";
            case LD_R_X86_64_PC64:
                return "R_X86_64_PC64";
            case LD_R_X86_64_GOTOFF64:
                return "R_X86_64_GOTOFF64";
            case LD_R_X86_64_GOTPC32:
                return "R_X86_64_GOTPC32";
            case LD_R_X86_64_GOT64:
                return "R_X86_64_GOT64";
            case LD_R_X86_64_GOTPCREL64:
                return "R_X86_64_GOTPCREL64";
            case LD_R_X86_64_GOTPC64:
                return "R_X86_64_GOTPC64";
            case LD_R_X86_64_GOTPLT64:
                return "R_X86_64_GOTPLT64";
            case LD_R_X86_64_PLTOFF64:
                return "R_X86_64_PLTOFF64";
            case LD_R_X86_64_SIZE32:
                return "R_X86_64_SIZE32";
            case LD_R_X86_64_SIZE64:
                return "R_X86_64_SIZE64";
            case LD_R_X86_64_GOTPC32_TLSDESC:
                return "R_X86_64_GOTPC32_TLSDESC";
            case LD_R_X86_64_TLSDESC_CALL:
                return "R_X86_64_TLSDESC_CALL";
            case LD_R_X86_64_TLSDESC:
                return "R_X86_64_TLSDESC";
            case LD_R_X86_64_GOTPCRELX:
                return "R_X86_64_GOTPCRELX";
            case LD_R_X86_64_REX_GOTPCRELX:
                return "R_X86_64_REX_GOTPCRELX";
            default:
                return "R_X86_64_UNKNOWN";
        }
    }

    if (arch == LD_ARCH_RISCV64) {
        switch (type) {
            case LD_R_RISCV_NONE:
                return "R_RISCV_NONE";
            case LD_R_RISCV_32:
                return "R_RISCV_32";
            case LD_R_RISCV_64:
                return "R_RISCV_64";
            case LD_R_RISCV_RELATIVE:
                return "R_RISCV_RELATIVE";
            case LD_R_RISCV_TLS_DTPREL32:
                return "R_RISCV_TLS_DTPREL32";
            case LD_R_RISCV_TLS_DTPREL64:
                return "R_RISCV_TLS_DTPREL64";
            case LD_R_RISCV_TLS_TPREL32:
                return "R_RISCV_TLS_TPREL32";
            case LD_R_RISCV_TLS_TPREL64:
                return "R_RISCV_TLS_TPREL64";
            case LD_R_RISCV_BRANCH:
                return "R_RISCV_BRANCH";
            case LD_R_RISCV_JAL:
                return "R_RISCV_JAL";
            case LD_R_RISCV_CALL:
                return "R_RISCV_CALL";
            case LD_R_RISCV_CALL_PLT:
                return "R_RISCV_CALL_PLT";
            case LD_R_RISCV_GOT_HI20:
                return "R_RISCV_GOT_HI20";
            case LD_R_RISCV_TLS_GOT_HI20:
                return "R_RISCV_TLS_GOT_HI20";
            case LD_R_RISCV_TLS_GD_HI20:
                return "R_RISCV_TLS_GD_HI20";
            case LD_R_RISCV_PCREL_HI20:
                return "R_RISCV_PCREL_HI20";
            case LD_R_RISCV_PCREL_LO12_I:
                return "R_RISCV_PCREL_LO12_I";
            case LD_R_RISCV_PCREL_LO12_S:
                return "R_RISCV_PCREL_LO12_S";
            case LD_R_RISCV_HI20:
                return "R_RISCV_HI20";
            case LD_R_RISCV_LO12_I:
                return "R_RISCV_LO12_I";
            case LD_R_RISCV_LO12_S:
                return "R_RISCV_LO12_S";
            case LD_R_RISCV_TPREL_HI20:
                return "R_RISCV_TPREL_HI20";
            case LD_R_RISCV_TPREL_LO12_I:
                return "R_RISCV_TPREL_LO12_I";
            case LD_R_RISCV_TPREL_LO12_S:
                return "R_RISCV_TPREL_LO12_S";
            case LD_R_RISCV_TPREL_ADD:
                return "R_RISCV_TPREL_ADD";
            case LD_R_RISCV_ADD8:
                return "R_RISCV_ADD8";
            case LD_R_RISCV_ADD16:
                return "R_RISCV_ADD16";
            case LD_R_RISCV_ADD32:
                return "R_RISCV_ADD32";
            case LD_R_RISCV_ADD64:
                return "R_RISCV_ADD64";
            case LD_R_RISCV_SUB8:
                return "R_RISCV_SUB8";
            case LD_R_RISCV_SUB16:
                return "R_RISCV_SUB16";
            case LD_R_RISCV_SUB32:
                return "R_RISCV_SUB32";
            case LD_R_RISCV_SUB64:
                return "R_RISCV_SUB64";
            case LD_R_RISCV_ALIGN:
                return "R_RISCV_ALIGN";
            case LD_R_RISCV_RVC_BRANCH:
                return "R_RISCV_RVC_BRANCH";
            case LD_R_RISCV_RVC_JUMP:
                return "R_RISCV_RVC_JUMP";
            case LD_R_RISCV_RELAX:
                return "R_RISCV_RELAX";
            case LD_R_RISCV_SUB6:
                return "R_RISCV_SUB6";
            case LD_R_RISCV_SET6:
                return "R_RISCV_SET6";
            case LD_R_RISCV_SET8:
                return "R_RISCV_SET8";
            case LD_R_RISCV_SET16:
                return "R_RISCV_SET16";
            case LD_R_RISCV_SET32:
                return "R_RISCV_SET32";
            case LD_R_RISCV_32_PCREL:
                return "R_RISCV_32_PCREL";
            case LD_R_RISCV_SET_ULEB128:
                return "R_RISCV_SET_ULEB128";
            case LD_R_RISCV_SUB_ULEB128:
                return "R_RISCV_SUB_ULEB128";
            default:
                return "R_RISCV_UNKNOWN";
        }
    }

    return "R_UNKNOWN";
}

bool ld_elf_relocation_supported(ld_arch_t arch, uint32_t type) {
    if (arch == LD_ARCH_ARM64) {
        switch (type) {
            case LD_R_AARCH64_NONE:
            case LD_R_AARCH64_ABS64:
            case LD_R_AARCH64_ABS32:
            case LD_R_AARCH64_ABS16:
            case LD_R_AARCH64_PREL64:
            case LD_R_AARCH64_PREL32:
            case LD_R_AARCH64_PREL16:
            case LD_R_AARCH64_MOVW_UABS_G0:
            case LD_R_AARCH64_MOVW_UABS_G0_NC:
            case LD_R_AARCH64_MOVW_UABS_G1:
            case LD_R_AARCH64_MOVW_UABS_G1_NC:
            case LD_R_AARCH64_MOVW_UABS_G2:
            case LD_R_AARCH64_MOVW_UABS_G2_NC:
            case LD_R_AARCH64_MOVW_UABS_G3:
            case LD_R_AARCH64_LD_PREL_LO19:
            case LD_R_AARCH64_ADR_PREL_LO21:
            case LD_R_AARCH64_ADR_PREL_PG_HI21:
            case LD_R_AARCH64_ADD_ABS_LO12_NC:
            case LD_R_AARCH64_LDST8_ABS_LO12_NC:
            case LD_R_AARCH64_TSTBR14:
            case LD_R_AARCH64_CONDBR19:
            case LD_R_AARCH64_JUMP26:
            case LD_R_AARCH64_CALL26:
            case LD_R_AARCH64_LDST16_ABS_LO12_NC:
            case LD_R_AARCH64_LDST32_ABS_LO12_NC:
            case LD_R_AARCH64_LDST64_ABS_LO12_NC:
            case LD_R_AARCH64_LDST128_ABS_LO12_NC:
            case LD_R_AARCH64_GOT_LD_PREL19:
            case LD_R_AARCH64_ADR_GOT_PAGE:
            case LD_R_AARCH64_LD64_GOT_LO12_NC:
            case LD_R_AARCH64_LD64_GOTPAGE_LO15:
            case LD_R_AARCH64_TLSGD_ADR_PAGE21:
            case LD_R_AARCH64_TLSGD_ADD_LO12_NC:
            case LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
            case LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
            case LD_R_AARCH64_TLSLE_ADD_TPREL_HI12:
            case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
            case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
            case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
            case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
            case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
            case LD_R_AARCH64_TLSDESC_ADR_PAGE21:
            case LD_R_AARCH64_TLSDESC_LD64_LO12:
            case LD_R_AARCH64_TLSDESC_ADD_LO12:
            case LD_R_AARCH64_TLSDESC_CALL:
            case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12:
            case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC:
                return true;
            default:
                return false;
        }
    }

    if (arch == LD_ARCH_AMD64) {
        switch (type) {
            case LD_R_X86_64_NONE:
            case LD_R_X86_64_64:
            case LD_R_X86_64_PC32:
            case LD_R_X86_64_GOT32:
            case LD_R_X86_64_PLT32:
            case LD_R_X86_64_GOTPCREL:
            case LD_R_X86_64_32:
            case LD_R_X86_64_32S:
            case LD_R_X86_64_16:
            case LD_R_X86_64_PC16:
            case LD_R_X86_64_8:
            case LD_R_X86_64_PC8:
            case LD_R_X86_64_DTPOFF64:
            case LD_R_X86_64_TPOFF64:
            case LD_R_X86_64_TLSGD:
            case LD_R_X86_64_TLSLD:
            case LD_R_X86_64_DTPOFF32:
            case LD_R_X86_64_GOTTPOFF:
            case LD_R_X86_64_TPOFF32:
            case LD_R_X86_64_PC64:
            case LD_R_X86_64_GOTOFF64:
            case LD_R_X86_64_GOTPC32:
            case LD_R_X86_64_GOT64:
            case LD_R_X86_64_GOTPCREL64:
            case LD_R_X86_64_GOTPC64:
            case LD_R_X86_64_PLTOFF64:
            case LD_R_X86_64_SIZE32:
            case LD_R_X86_64_SIZE64:
            case LD_R_X86_64_GOTPC32_TLSDESC:
            case LD_R_X86_64_TLSDESC_CALL:
            case LD_R_X86_64_GOTPCRELX:
            case LD_R_X86_64_REX_GOTPCRELX:
                return true;
            default:
                return false;
        }
    }

    if (arch == LD_ARCH_RISCV64) {
        switch (type) {
            case LD_R_RISCV_NONE:
            case LD_R_RISCV_32:
            case LD_R_RISCV_64:
            case LD_R_RISCV_RELATIVE:
            case LD_R_RISCV_TLS_DTPREL32:
            case LD_R_RISCV_TLS_DTPREL64:
            case LD_R_RISCV_TLS_TPREL32:
            case LD_R_RISCV_TLS_TPREL64:
            case LD_R_RISCV_BRANCH:
            case LD_R_RISCV_JAL:
            case LD_R_RISCV_CALL:
            case LD_R_RISCV_CALL_PLT:
            case LD_R_RISCV_GOT_HI20:
            case LD_R_RISCV_TLS_GOT_HI20:
            case LD_R_RISCV_TLS_GD_HI20:
            case LD_R_RISCV_PCREL_HI20:
            case LD_R_RISCV_PCREL_LO12_I:
            case LD_R_RISCV_PCREL_LO12_S:
            case LD_R_RISCV_HI20:
            case LD_R_RISCV_LO12_I:
            case LD_R_RISCV_LO12_S:
            case LD_R_RISCV_TPREL_HI20:
            case LD_R_RISCV_TPREL_LO12_I:
            case LD_R_RISCV_TPREL_LO12_S:
            case LD_R_RISCV_TPREL_ADD:
            case LD_R_RISCV_ADD8:
            case LD_R_RISCV_ADD16:
            case LD_R_RISCV_ADD32:
            case LD_R_RISCV_ADD64:
            case LD_R_RISCV_SUB8:
            case LD_R_RISCV_SUB16:
            case LD_R_RISCV_SUB32:
            case LD_R_RISCV_SUB64:
            case LD_R_RISCV_ALIGN:
            case LD_R_RISCV_RVC_BRANCH:
            case LD_R_RISCV_RVC_JUMP:
            case LD_R_RISCV_RELAX:
            case LD_R_RISCV_SUB6:
            case LD_R_RISCV_SET6:
            case LD_R_RISCV_SET8:
            case LD_R_RISCV_SET16:
            case LD_R_RISCV_SET32:
            case LD_R_RISCV_32_PCREL:
            case LD_R_RISCV_SET_ULEB128:
            case LD_R_RISCV_SUB_ULEB128:
                return true;
            default:
                return false;
        }
    }

    return false;
}

bool ld_elf_relocation_supported_in_static_pie(ld_arch_t arch,
                                               uint32_t type) {
    /*
     * This is the exact allocated-relocation scanReloc switch from Zig
     * 738d2be9d6b6ef3ff3559130c05159ef53336224.  R_*_NONE is skipped by
     * Zig's common Atom scanner before architecture dispatch, so it is also
     * accepted here.  Nature's ET_EXEC backend intentionally handles a wider
     * set and must not leak those extensions into the Zig-compatible PIE
     * policy.
     */
    switch (arch) {
        case LD_ARCH_AMD64:
            switch (type) {
                case LD_R_X86_64_NONE:
                case LD_R_X86_64_64:
                case LD_R_X86_64_32:
                case LD_R_X86_64_32S:
                case LD_R_X86_64_GOT32:
                case LD_R_X86_64_GOTPC32:
                case LD_R_X86_64_GOTPC64:
                case LD_R_X86_64_GOTPCREL:
                case LD_R_X86_64_GOTPCREL64:
                case LD_R_X86_64_GOTPCRELX:
                case LD_R_X86_64_REX_GOTPCRELX:
                case LD_R_X86_64_PLT32:
                case LD_R_X86_64_PLTOFF64:
                case LD_R_X86_64_PC32:
                case LD_R_X86_64_TLSGD:
                case LD_R_X86_64_TLSLD:
                case LD_R_X86_64_GOTTPOFF:
                case LD_R_X86_64_GOTPC32_TLSDESC:
                case LD_R_X86_64_TPOFF32:
                case LD_R_X86_64_TPOFF64:
                case LD_R_X86_64_GOTOFF64:
                case LD_R_X86_64_DTPOFF32:
                case LD_R_X86_64_DTPOFF64:
                case LD_R_X86_64_SIZE32:
                case LD_R_X86_64_SIZE64:
                case LD_R_X86_64_TLSDESC_CALL:
                    return true;
                default:
                    return false;
            }
        case LD_ARCH_ARM64:
            switch (type) {
                case LD_R_AARCH64_NONE:
                case LD_R_AARCH64_ABS64:
                case LD_R_AARCH64_PREL64:
                case LD_R_AARCH64_PREL32:
                case LD_R_AARCH64_ADR_PREL_LO21:
                case LD_R_AARCH64_ADR_PREL_PG_HI21:
                case LD_R_AARCH64_ADD_ABS_LO12_NC:
                case LD_R_AARCH64_JUMP26:
                case LD_R_AARCH64_CALL26:
                case LD_R_AARCH64_LDST8_ABS_LO12_NC:
                case LD_R_AARCH64_LDST16_ABS_LO12_NC:
                case LD_R_AARCH64_LDST32_ABS_LO12_NC:
                case LD_R_AARCH64_LDST64_ABS_LO12_NC:
                case LD_R_AARCH64_LDST128_ABS_LO12_NC:
                case LD_R_AARCH64_ADR_GOT_PAGE:
                case LD_R_AARCH64_LD64_GOT_LO12_NC:
                case LD_R_AARCH64_LD64_GOTPAGE_LO15:
                case LD_R_AARCH64_TLSGD_ADR_PAGE21:
                case LD_R_AARCH64_TLSGD_ADD_LO12_NC:
                case LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
                case LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
                case LD_R_AARCH64_TLSLE_ADD_TPREL_HI12:
                case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
                case LD_R_AARCH64_TLSDESC_ADR_PAGE21:
                case LD_R_AARCH64_TLSDESC_LD64_LO12:
                case LD_R_AARCH64_TLSDESC_ADD_LO12:
                case LD_R_AARCH64_TLSDESC_CALL:
                    return true;
                default:
                    return false;
            }
        case LD_ARCH_RISCV64:
            switch (type) {
                case LD_R_RISCV_NONE:
                case LD_R_RISCV_32:
                case LD_R_RISCV_64:
                case LD_R_RISCV_CALL_PLT:
                case LD_R_RISCV_GOT_HI20:
                case LD_R_RISCV_PCREL_HI20:
                case LD_R_RISCV_PCREL_LO12_I:
                case LD_R_RISCV_PCREL_LO12_S:
                case LD_R_RISCV_HI20:
                case LD_R_RISCV_LO12_I:
                case LD_R_RISCV_LO12_S:
                case LD_R_RISCV_TPREL_HI20:
                case LD_R_RISCV_TPREL_LO12_I:
                case LD_R_RISCV_TPREL_LO12_S:
                case LD_R_RISCV_TPREL_ADD:
                case LD_R_RISCV_ADD32:
                case LD_R_RISCV_SUB32:
                case LD_R_RISCV_SET_ULEB128:
                case LD_R_RISCV_SUB_ULEB128:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

ld_elf_reloc_got_kind_t ld_elf_relocation_got_kind(ld_arch_t arch,
                                                   uint32_t type) {
    if (arch == LD_ARCH_ARM64) {
        if (type == LD_R_AARCH64_GOT_LD_PREL19 ||
            type == LD_R_AARCH64_ADR_GOT_PAGE ||
            type == LD_R_AARCH64_LD64_GOT_LO12_NC ||
            type == LD_R_AARCH64_LD64_GOTPAGE_LO15) {
            return LD_ELF_RELOC_GOT_ORDINARY;
        }
        if (type == LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 ||
            type == LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC) {
            return LD_ELF_RELOC_GOT_TP;
        }
        if (type == LD_R_AARCH64_TLSGD_ADR_PAGE21 ||
            type == LD_R_AARCH64_TLSGD_ADD_LO12_NC) {
            return LD_ELF_RELOC_GOT_TLSGD;
        }
    }
    if (arch == LD_ARCH_AMD64) {
        if (type == LD_R_X86_64_GOTTPOFF) return LD_ELF_RELOC_GOT_TP;
        if (type == LD_R_X86_64_GOT32 ||
            type == LD_R_X86_64_GOT64 ||
            type == LD_R_X86_64_GOTPCREL64 ||
            type == LD_R_X86_64_GOTPCREL ||
            type == LD_R_X86_64_GOTPCRELX ||
            type == LD_R_X86_64_REX_GOTPCRELX) {
            return LD_ELF_RELOC_GOT_ORDINARY;
        }
    }
    if (arch == LD_ARCH_RISCV64) {
        if (type == LD_R_RISCV_GOT_HI20)
            return LD_ELF_RELOC_GOT_ORDINARY;
        if (type == LD_R_RISCV_TLS_GOT_HI20)
            return LD_ELF_RELOC_GOT_TP;
        if (type == LD_R_RISCV_TLS_GD_HI20)
            return LD_ELF_RELOC_GOT_TLSGD;
    }
    return LD_ELF_RELOC_GOT_NONE;
}

bool ld_elf_relocation_needs_got(ld_arch_t arch, uint32_t type) {
    return ld_elf_relocation_got_kind(arch, type) !=
           LD_ELF_RELOC_GOT_NONE;
}

bool ld_elf_relocation_needs_got_base(ld_arch_t arch, uint32_t type) {
    if (arch != LD_ARCH_AMD64) return false;
    switch (type) {
        case LD_R_X86_64_GOT32:
        case LD_R_X86_64_GOT64:
        case LD_R_X86_64_GOTOFF64:
        case LD_R_X86_64_GOTPC32:
        case LD_R_X86_64_GOTPC64:
        case LD_R_X86_64_PLTOFF64:
            return true;
        default:
            return false;
    }
}

size_t ld_elf_relocation_write_width(ld_arch_t arch, uint32_t type) {
    if (!ld_elf_relocation_supported(arch, type)) return SIZE_MAX;
    if (type == 0U) return 0U;

    if (arch == LD_ARCH_ARM64) {
        if (type == LD_R_AARCH64_ABS64 || type == LD_R_AARCH64_PREL64) {
            return 8U;
        }
        if (type == LD_R_AARCH64_ABS16 || type == LD_R_AARCH64_PREL16) {
            return 2U;
        }
        return 4U;
    }

    if (arch == LD_ARCH_AMD64) {
        switch (type) {
            case LD_R_X86_64_8:
            case LD_R_X86_64_PC8:
                return 1U;
            case LD_R_X86_64_16:
            case LD_R_X86_64_PC16:
                return 2U;
            case LD_R_X86_64_64:
            case LD_R_X86_64_DTPOFF64:
            case LD_R_X86_64_TPOFF64:
            case LD_R_X86_64_PC64:
            case LD_R_X86_64_GOTOFF64:
            case LD_R_X86_64_GOT64:
            case LD_R_X86_64_GOTPCREL64:
            case LD_R_X86_64_GOTPC64:
            case LD_R_X86_64_PLTOFF64:
            case LD_R_X86_64_SIZE64:
                return 8U;
            default:
                break;
        }
    }
    if (arch == LD_ARCH_AMD64 && type == LD_R_X86_64_TLSDESC_CALL) {
        return 0U;
    }
    if (arch == LD_ARCH_RISCV64) {
        switch (type) {
            case LD_R_RISCV_NONE:
            case LD_R_RISCV_ALIGN:
            case LD_R_RISCV_RELAX:
                return 0U;
            case LD_R_RISCV_ADD8:
            case LD_R_RISCV_SUB8:
            case LD_R_RISCV_SUB6:
            case LD_R_RISCV_SET6:
            case LD_R_RISCV_SET8:
            case LD_R_RISCV_SET_ULEB128:
            case LD_R_RISCV_SUB_ULEB128:
                return 1U;
            case LD_R_RISCV_ADD16:
            case LD_R_RISCV_SUB16:
            case LD_R_RISCV_SET16:
            case LD_R_RISCV_RVC_BRANCH:
            case LD_R_RISCV_RVC_JUMP:
                return 2U;
            case LD_R_RISCV_64:
            case LD_R_RISCV_RELATIVE:
            case LD_R_RISCV_TLS_DTPREL64:
            case LD_R_RISCV_TLS_TPREL64:
            case LD_R_RISCV_ADD64:
            case LD_R_RISCV_SUB64:
            case LD_R_RISCV_CALL:
            case LD_R_RISCV_CALL_PLT:
                return 8U;
            default:
                return 4U;
        }
    }
    return 4U;
}

static int ld_elf_reloc_fail(ld_elf_context_t *ctx,
                             const ld_elf_object_t *object,
                             const ld_elf_section_t *section,
                             const ld_elf_relocation_t *relocation,
                             ld_arch_t arch, const char *symbol_name,
                             const char *format, ...) {
    char reason[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(reason, sizeof(reason), format, arguments);
    va_end(arguments);

    const char *input = "<unknown input>";
    if (object && object->display_name) {
        input = object->display_name;
    } else if (object && object->file && object->file->path) {
        input = object->file->path;
    }
    const char *section_name = section && section->name ? section->name
                                                        : "<unknown section>";
    const char *name = symbol_name ? symbol_name : "<unknown symbol>";
    uint64_t offset = relocation ? relocation->offset : 0U;
    uint32_t type = relocation ? relocation->type : 0U;

    return ld_elf_fail(
            ctx, LD_RELOCATION_ERROR,
            "ELF relocation %s (%u) failed in '%s', section '%s', offset "
            "0x%llx, symbol '%s': %s",
            ld_elf_relocation_name(arch, type), type, input, section_name,
            (unsigned long long) offset, name, reason);
}

bool ld_elf_relocation_is_x86_tls_pair_start(uint32_t type) {
    return type == LD_R_X86_64_TLSGD || type == LD_R_X86_64_TLSLD ||
           type == LD_R_X86_64_GOTPC32_TLSDESC;
}

static bool ld_elf_reloc_x86_tls_helper_type(uint32_t type) {
    switch (type) {
        case LD_R_X86_64_PC32:
        case LD_R_X86_64_PLT32:
        case LD_R_X86_64_GOTPCREL:
        case LD_R_X86_64_GOTPCRELX:
            return true;
        default:
            return false;
    }
}

static bool ld_elf_reloc_x86_tls_helper_indirect(uint32_t type) {
    return type == LD_R_X86_64_GOTPCREL ||
           type == LD_R_X86_64_GOTPCRELX;
}

static const char *ld_elf_reloc_symbol_name(
        const ld_elf_object_t *object,
        const ld_elf_relocation_t *relocation) {
    if (!object || !relocation ||
        relocation->symbol_index >= object->symbol_count) {
        return NULL;
    }
    return object->symbols[relocation->symbol_index].name;
}

static bool ld_elf_reloc_x86_can_relax_gottpoff(
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation) {
    if (!section || !relocation || relocation->addend != -4 ||
        relocation->offset < 3U || !section->data ||
        relocation->offset > SIZE_MAX) {
        return false;
    }
    size_t offset = (size_t) relocation->offset;
    if (offset > section->data_size ||
        section->data_size - offset < sizeof(uint32_t)) {
        return false;
    }
    const uint8_t *instruction = section->data + offset - 3U;
    uint8_t rex = instruction[0];
    uint8_t modrm = instruction[2];
    return (rex == 0x48U || rex == 0x4cU) && instruction[1] == 0x8bU &&
           (modrm & 0xc7U) == 0x05U;
}

static int ld_elf_reloc_validate_x86_tls_pair(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation,
        const ld_elf_relocation_t *helper, const uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset) {
    const char *symbol_name =
            ld_elf_reloc_symbol_name(object, relocation);
    if (!relocation || !helper ||
        !ld_elf_relocation_is_x86_tls_pair_start(relocation->type)) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "missing or invalid static TLS relocation pair");
    }
    if (!object || relocation->symbol_index >= object->symbol_count) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "TLS relocation uses an invalid symbol index");
    }
    if (helper->symbol_index >= object->symbol_count) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "paired __tls_get_addr relocation uses an invalid symbol "
                "index");
    }
    if (relocation->relocation_section_index !=
        helper->relocation_section_index) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "TLS relocation and resolver call come from different "
                "relocation sections");
    }
    bool tlsdesc = relocation->type == LD_R_X86_64_GOTPC32_TLSDESC;
    const ld_elf_symbol_t *helper_symbol =
            &object->symbols[helper->symbol_index];
    if (tlsdesc) {
        const ld_elf_symbol_t *target =
                &object->symbols[relocation->symbol_index];
        if (helper->type != LD_R_X86_64_TLSDESC_CALL) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "expected R_X86_64_TLSDESC_CALL, found %s (%u)",
                    ld_elf_relocation_name(LD_ARCH_AMD64, helper->type),
                    helper->type);
        }
        if (helper->symbol_index != relocation->symbol_index ||
            target->type != LD_ELF_STT_TLS) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "TLSDESC pair must reference the same STT_TLS symbol");
        }
        if (relocation->addend != -4 || helper->addend != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "TLSDESC pair requires addends -4 and 0 (found %lld "
                    "and %lld)",
                    (long long) relocation->addend,
                    (long long) helper->addend);
        }
    } else {
        if (!ld_elf_reloc_x86_tls_helper_type(helper->type)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "expected a PC32/PLT32/GOTPCREL resolver relocation, "
                    "found %s (%u)",
                    ld_elf_relocation_name(LD_ARCH_AMD64, helper->type),
                    helper->type);
        }
        if (!helper_symbol->name ||
            strcmp(helper_symbol->name, "__tls_get_addr") != 0 ||
            !ld_elf_symbol_is_undefined(helper_symbol)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "paired resolver relocation must reference undefined "
                    "symbol '__tls_get_addr', found '%s'",
                    helper_symbol->name ? helper_symbol->name : "<unnamed>");
        }
        if (relocation->addend != -4 || helper->addend != -4) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "static TLS pair requires -4 addends (found %lld and "
                    "%lld)",
                    (long long) relocation->addend,
                    (long long) helper->addend);
        }
    }

    bool indirect = !tlsdesc &&
                    ld_elf_reloc_x86_tls_helper_indirect(helper->type);
    uint64_t helper_delta = tlsdesc
                                    ? 4U
                                    : (relocation->type == LD_R_X86_64_TLSGD
                                               ? 8U
                                               : (indirect ? 6U : 5U));
    if (relocation->offset > UINT64_MAX - helper_delta) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "resolver relocation offset overflows");
    }
    uint64_t expected_helper_offset = relocation->offset + helper_delta;
    if (helper->offset != expected_helper_offset) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "resolver relocation is at offset 0x%llx; expected 0x%llx",
                (unsigned long long) helper->offset,
                (unsigned long long) expected_helper_offset);
    }

    size_t prefix = relocation->type == LD_R_X86_64_TLSGD ? 4U : 3U;
    size_t rewrite_size = tlsdesc
                                  ? 9U
                                  : (relocation->type == LD_R_X86_64_TLSGD
                                             ? 16U
                                             : (indirect ? 13U : 12U));
    if (!section_data || mapped_offset < prefix || mapped_offset > SIZE_MAX) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "static TLS instruction sequence is truncated");
    }
    size_t offset = (size_t) mapped_offset;
    size_t start = offset - prefix;
    if (start > section_size || rewrite_size > section_size - start) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "%zu-byte static TLS rewrite exceeds the %zu-byte section",
                rewrite_size, section_size);
    }

    const uint8_t *instruction = section_data + start;
    if (tlsdesc) {
        static const uint8_t lea_prefix[] = {0x48U, 0x8dU, 0x05U};
        if (memcmp(instruction, lea_prefix, sizeof(lea_prefix)) != 0 ||
            instruction[7] != 0xffU || instruction[8] != 0x10U) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "expected TLSDESC LEA plus CALL *(%%rax) instruction "
                    "sequence");
        }
    } else if (relocation->type == LD_R_X86_64_TLSGD) {
        static const uint8_t lea_prefix[] = {0x66U, 0x48U, 0x8dU, 0x3dU};
        static const uint8_t direct_call[] = {0x66U, 0x66U, 0x48U, 0xe8U};
        static const uint8_t indirect_call[] = {0x66U, 0x48U, 0xffU, 0x15U};
        const uint8_t *expected_call =
                indirect ? indirect_call : direct_call;
        if (memcmp(instruction, lea_prefix, sizeof(lea_prefix)) != 0 ||
            memcmp(instruction + 8U, expected_call,
                   sizeof(direct_call)) != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "expected TLSGD data16 LEA plus matching %s CALL "
                    "instruction sequence",
                    indirect ? "GOT-indirect" : "direct");
        }
    } else {
        static const uint8_t lea_prefix[] = {0x48U, 0x8dU, 0x3dU};
        bool call_matches = indirect
                                    ? instruction[7] == 0xffU &&
                                              instruction[8] == 0x15U
                                    : instruction[7] == 0xe8U;
        if (memcmp(instruction, lea_prefix, sizeof(lea_prefix)) != 0 ||
            !call_matches) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "expected TLSLD LEA plus matching %s CALL instruction "
                    "sequence",
                    indirect ? "GOT-indirect" : "direct");
        }
    }
    return LD_OK;
}

int ld_elf_relocation_prepare_x86_tls_sequences(
        ld_elf_context_t *ctx, ld_elf_object_t *object) {
    if (!object || ld_elf_reloc_arch(ctx, object) != LD_ARCH_AMD64) {
        return LD_OK;
    }
    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        for (size_t j = 0; j < section->relocation_count; j++) {
            section->relocations[j].x86_tls_pair_index = SIZE_MAX;
            section->relocations[j].x86_tls_pair_follower = false;
            section->relocations[j].x86_gottpoff_relax = false;
        }
    }

    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if (section->discarded) continue;
        for (size_t j = 0; j < section->relocation_count; j++) {
            ld_elf_relocation_t *relocation = &section->relocations[j];
            if (relocation->type == LD_R_X86_64_GOTTPOFF) {
                relocation->x86_gottpoff_relax =
                        ld_elf_reloc_x86_can_relax_gottpoff(section,
                                                            relocation);
                continue;
            }
            if (!ld_elf_relocation_is_x86_tls_pair_start(relocation->type)) {
                continue;
            }

            bool tlsdesc =
                    relocation->type == LD_R_X86_64_GOTPC32_TLSDESC;
            uint64_t maximum_delta = tlsdesc
                                             ? 4U
                                             : (relocation->type ==
                                                                LD_R_X86_64_TLSGD
                                                        ? 8U
                                                        : 6U);
            if (relocation->offset > UINT64_MAX - maximum_delta) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, LD_ARCH_AMD64,
                        ld_elf_reloc_symbol_name(object, relocation),
                        "resolver relocation offset overflows");
            }
            uint64_t direct_offset =
                    relocation->offset +
                    (tlsdesc ? 4U
                             : (relocation->type == LD_R_X86_64_TLSGD ? 8U
                                                                      : 5U));
            uint64_t indirect_offset =
                    relocation->offset +
                    (tlsdesc ? 4U
                             : (relocation->type == LD_R_X86_64_TLSGD ? 8U
                                                                      : 6U));
            size_t helper_index = SIZE_MAX;
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *candidate = &section->relocations[k];
                if ((candidate->offset != direct_offset &&
                     candidate->offset != indirect_offset) ||
                    candidate->relocation_section_index !=
                            relocation->relocation_section_index) {
                    continue;
                }
                if (helper_index != SIZE_MAX) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation,
                            LD_ARCH_AMD64,
                            ld_elf_reloc_symbol_name(object, relocation),
                            "multiple resolver relocations occur at expected "
                            "direct/indirect call offsets");
                }
                helper_index = k;
            }
            if (helper_index == SIZE_MAX) {
                if (tlsdesc) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation,
                            LD_ARCH_AMD64,
                            ld_elf_reloc_symbol_name(object, relocation),
                            "missing paired R_X86_64_TLSDESC_CALL at the "
                            "expected offset");
                }
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, LD_ARCH_AMD64,
                        ld_elf_reloc_symbol_name(object, relocation),
                        "missing __tls_get_addr resolver relocation at the "
                        "expected direct/indirect call offset");
            }
            ld_elf_relocation_t *helper =
                    &section->relocations[helper_index];
            if (helper->x86_tls_pair_follower) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, LD_ARCH_AMD64,
                        ld_elf_reloc_symbol_name(object, relocation),
                        "resolver relocation is paired with multiple TLS "
                        "sequences");
            }
            int status = ld_elf_reloc_validate_x86_tls_pair(
                    ctx, object, section, relocation, helper, section->data,
                    section->data_size, relocation->offset);
            if (status != LD_OK) return status;
            relocation->x86_tls_pair_index = helper_index;
            helper->x86_tls_pair_follower = true;
        }
    }

    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if (section->discarded) continue;
        for (size_t j = 0; j < section->relocation_count; j++) {
            ld_elf_relocation_t *relocation = &section->relocations[j];
            if (relocation->type == LD_R_X86_64_TLSDESC_CALL &&
                !relocation->x86_tls_pair_follower) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, LD_ARCH_AMD64,
                        ld_elf_reloc_symbol_name(object, relocation),
                        "orphan R_X86_64_TLSDESC_CALL has no matching "
                        "R_X86_64_GOTPC32_TLSDESC");
            }
            const char *name =
                    ld_elf_reloc_symbol_name(object, relocation);
            if (!name || strcmp(name, "_TLS_MODULE_BASE_") != 0) continue;
            bool valid_start =
                    relocation->type == LD_R_X86_64_GOTPC32_TLSDESC &&
                    relocation->x86_tls_pair_index <
                            section->relocation_count;
            bool valid_follower =
                    relocation->type == LD_R_X86_64_TLSDESC_CALL &&
                    relocation->x86_tls_pair_follower;
            if (valid_start || valid_follower) continue;
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64, name,
                    "_TLS_MODULE_BASE_ is only valid in a complete TLSDESC "
                    "pair");
        }
    }
    return LD_OK;
}

int ld_elf_relocation_apply_x86_tls_pair(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation,
        const ld_elf_relocation_t *helper, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values) {
    const char *symbol_name = values ? values->symbol_name : NULL;
    if (!values) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "missing static TLS relocation values");
    }
    int status = ld_elf_reloc_validate_x86_tls_pair(
            ctx, object, section, relocation, helper, section_data,
            section_size, mapped_offset);
    if (status != LD_OK) return status;

    int64_t value;
    if (relocation->type == LD_R_X86_64_GOTPC32_TLSDESC) {
        if (!ld_elf_reloc_expression_signed(
                    values->symbol_address, 0,
                    values->thread_pointer_address, 32U, &value)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "S - TP does not fit a signed 32-bit TLSDESC offset");
        }
        uint8_t replacement[9] = {
                0x48U,
                0xc7U,
                0xc0U,
                0x00U,
                0x00U,
                0x00U,
                0x00U,
                0x66U,
                0x90U,
        };
        ld_elf_reloc_write_u32(replacement + 3U, (uint32_t) value);
        memcpy(section_data + (size_t) mapped_offset - 3U, replacement,
               sizeof(replacement));
        return LD_OK;
    }

    if (relocation->type == LD_R_X86_64_TLSGD) {
        if (!ld_elf_reloc_expression_signed(
                    values->symbol_address, 0,
                    values->thread_pointer_address, 32U, &value)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_AMD64,
                    symbol_name,
                    "S - TP does not fit a signed 32-bit TLS offset");
        }
        uint8_t replacement[16] = {
                0x64U,
                0x48U,
                0x8bU,
                0x04U,
                0x25U,
                0x00U,
                0x00U,
                0x00U,
                0x00U,
                0x48U,
                0x81U,
                0xc0U,
                0x00U,
                0x00U,
                0x00U,
                0x00U,
        };
        ld_elf_reloc_write_u32(replacement + 12U, (uint32_t) value);
        memcpy(section_data + (size_t) mapped_offset - 4U, replacement,
               sizeof(replacement));
        return LD_OK;
    }

    if (!ld_elf_reloc_expression_signed(
                values->thread_pointer_address, 0,
                values->tls_block_address, 32U, &value)) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "TP - DTP does not fit a signed 32-bit TLS block size");
    }
    bool indirect = ld_elf_reloc_x86_tls_helper_indirect(helper->type);
    uint8_t replacement[13] = {
            0x31U,
            0xc0U,
            0x64U,
            0x48U,
            0x8bU,
            0x00U,
            0x48U,
            0x2dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x90U,
    };
    ld_elf_reloc_write_u32(replacement + 8U, (uint32_t) value);
    memcpy(section_data + (size_t) mapped_offset - 3U, replacement,
           indirect ? sizeof(replacement) : sizeof(replacement) - 1U);
    return LD_OK;
}

int ld_elf_relocation_apply_x86_gottpoff(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values) {
    const char *symbol_name = values ? values->symbol_name : NULL;
    if (!relocation || relocation->type != LD_R_X86_64_GOTTPOFF ||
        !values || !section_data || relocation->addend != -4 ||
        mapped_offset < 3U || mapped_offset > SIZE_MAX) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "invalid GOTTPOFF local-exec relaxation");
    }
    size_t offset = (size_t) mapped_offset;
    size_t start = offset - 3U;
    if (start > section_size || 7U > section_size - start) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name, "7-byte GOTTPOFF instruction is truncated");
    }
    const uint8_t *instruction = section_data + start;
    uint8_t rex = instruction[0];
    uint8_t modrm = instruction[2];
    if ((rex != 0x48U && rex != 0x4cU) || instruction[1] != 0x8bU ||
        (modrm & 0xc7U) != 0x05U) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "expected MOV from a RIP-relative GOTTPOFF operand");
    }

    int64_t value;
    if (!ld_elf_reloc_expression_signed(
                values->symbol_address, 0,
                values->thread_pointer_address, 32U, &value)) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                symbol_name,
                "S - TP does not fit a signed 32-bit TLS offset");
    }
    uint8_t target_register = (uint8_t) ((modrm >> 3U) & 7U);
    uint8_t replacement[7] = {
            (uint8_t) (0x48U | ((rex & 0x04U) >> 2U)),
            0xc7U,
            (uint8_t) (0xc0U | target_register),
            0U,
            0U,
            0U,
            0U,
    };
    ld_elf_reloc_write_u32(replacement + 3U, (uint32_t) value);
    memcpy(section_data + start, replacement, sizeof(replacement));
    return LD_OK;
}

typedef enum {
    LD_ELF_X86_GOTPCRELX_NONE = 0,
    LD_ELF_X86_GOTPCRELX_LEA,
    LD_ELF_X86_GOTPCRELX_CALL,
    LD_ELF_X86_GOTPCRELX_JUMP,
} ld_elf_x86_gotpcrelx_form_t;

static ld_elf_x86_gotpcrelx_form_t ld_elf_reloc_x86_gotpcrelx_form(
        const uint8_t *data, size_t size, uint64_t offset, uint32_t type) {
    if (!data || offset > size || size - (size_t) offset < 4U) {
        return LD_ELF_X86_GOTPCRELX_NONE;
    }
    if (type == LD_R_X86_64_REX_GOTPCRELX) {
        if (offset < 3U) return LD_ELF_X86_GOTPCRELX_NONE;
        const uint8_t *instruction = data + (size_t) offset - 3U;
        bool rex = (instruction[0] & 0xf0U) == 0x40U;
        bool rip_relative = (instruction[2] & 0xc7U) == 0x05U;
        return rex && instruction[1] == 0x8bU && rip_relative
                       ? LD_ELF_X86_GOTPCRELX_LEA
                       : LD_ELF_X86_GOTPCRELX_NONE;
    }
    if (type != LD_R_X86_64_GOTPCRELX || offset < 2U) {
        return LD_ELF_X86_GOTPCRELX_NONE;
    }
    const uint8_t *instruction = data + (size_t) offset - 2U;
    if (instruction[0] == 0x8bU &&
        (instruction[1] & 0xc7U) == 0x05U) {
        return LD_ELF_X86_GOTPCRELX_LEA;
    }
    if (instruction[0] == 0xffU && instruction[1] == 0x15U) {
        return LD_ELF_X86_GOTPCRELX_CALL;
    }
    if (instruction[0] == 0xffU && instruction[1] == 0x25U) {
        return LD_ELF_X86_GOTPCRELX_JUMP;
    }
    return LD_ELF_X86_GOTPCRELX_NONE;
}

bool ld_elf_relocation_can_relax_x86_gotpcrelx(
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation) {
    if (!section || !relocation || section->data_size != section->header.sh_size) {
        return false;
    }
    return ld_elf_reloc_x86_gotpcrelx_form(
                   section->data, section->data_size, relocation->offset,
                   relocation->type) != LD_ELF_X86_GOTPCRELX_NONE;
}

int ld_elf_relocation_apply_x86_gotpcrelx(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *section_data,
        size_t section_size, uint64_t mapped_offset,
        const ld_elf_reloc_values_t *values) {
    if (!relocation || !values || !section_data || mapped_offset > SIZE_MAX) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                values ? values->symbol_name : NULL,
                "missing x86-64 GOTPCRELX relaxation argument");
    }
    ld_elf_x86_gotpcrelx_form_t form = ld_elf_reloc_x86_gotpcrelx_form(
            section_data, section_size, mapped_offset, relocation->type);
    if (form == LD_ELF_X86_GOTPCRELX_NONE) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                values->symbol_name,
                "instruction no longer matches a relaxable GOTPCRELX form");
    }

    int64_t displacement;
    if (!ld_elf_reloc_expression_signed(
                values->symbol_address, relocation->addend,
                values->place_address, 32U, &displacement)) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_AMD64,
                values->symbol_name,
                "relaxed S + A - P does not fit a signed 32-bit value");
    }

    size_t offset = (size_t) mapped_offset;
    if (form == LD_ELF_X86_GOTPCRELX_LEA) {
        section_data[offset - 2U] = 0x8dU;
    } else {
        section_data[offset - 2U] = 0x90U;
        section_data[offset - 1U] =
                form == LD_ELF_X86_GOTPCRELX_CALL ? 0xe8U : 0xe9U;
    }
    ld_elf_reloc_write_u32(section_data + offset, (uint32_t) displacement);
    return LD_OK;
}

int ld_elf_relocation_scan(ld_elf_context_t *ctx,
                           const ld_elf_object_t *object,
                           const ld_elf_section_t *section,
                           const ld_elf_relocation_t *relocation,
                           bool symbol_is_import,
                           ld_elf_reloc_scan_result_t *result) {
    ld_arch_t arch = ld_elf_reloc_arch(ctx, object);
    if (!relocation || !result) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch, NULL,
                                 "missing relocation scan argument");
    }
    memset(result, 0, sizeof(*result));
    if (!ld_elf_relocation_supported(arch, relocation->type)) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch, NULL,
                                 "unsupported relocation");
    }

    result->write_width =
            ld_elf_relocation_write_width(arch, relocation->type);
    result->needs_got = ld_elf_relocation_needs_got(arch, relocation->type);
    result->needs_got_base =
            ld_elf_relocation_needs_got_base(arch, relocation->type);
    if (arch == LD_ARCH_ARM64) {
        result->needs_plt =
                symbol_is_import &&
                (relocation->type == LD_R_AARCH64_CALL26 ||
                 relocation->type == LD_R_AARCH64_JUMP26);
        switch (relocation->type) {
            case LD_R_AARCH64_TLSDESC_ADR_PAGE21:
            case LD_R_AARCH64_TLSDESC_LD64_LO12:
            case LD_R_AARCH64_TLSDESC_ADD_LO12:
            case LD_R_AARCH64_TLSDESC_CALL:
                /*
                 * This backend only emits static ET_EXEC files, so the
                 * descriptor sequence is always relaxed to local-exec TLS.
                 * It therefore needs no GOT, PLT, or PC-relative allocation.
                 */
                break;
            case LD_R_AARCH64_PREL64:
            case LD_R_AARCH64_PREL32:
            case LD_R_AARCH64_PREL16:
            case LD_R_AARCH64_LD_PREL_LO19:
            case LD_R_AARCH64_ADR_PREL_LO21:
            case LD_R_AARCH64_ADR_PREL_PG_HI21:
            case LD_R_AARCH64_TSTBR14:
            case LD_R_AARCH64_CONDBR19:
            case LD_R_AARCH64_JUMP26:
            case LD_R_AARCH64_CALL26:
            case LD_R_AARCH64_GOT_LD_PREL19:
            case LD_R_AARCH64_ADR_GOT_PAGE:
            case LD_R_AARCH64_TLSGD_ADR_PAGE21:
            case LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
                result->pc_relative = true;
                break;
            default:
                break;
        }
    } else if (arch == LD_ARCH_AMD64) {
        result->needs_plt = symbol_is_import &&
                            relocation->type == LD_R_X86_64_PLT32;
        switch (relocation->type) {
            case LD_R_X86_64_PC16:
            case LD_R_X86_64_PC8:
            case LD_R_X86_64_PC32:
            case LD_R_X86_64_PC64:
            case LD_R_X86_64_PLT32:
            case LD_R_X86_64_GOTPCREL:
            case LD_R_X86_64_GOTPC32:
            case LD_R_X86_64_GOTPCREL64:
            case LD_R_X86_64_GOTPC64:
            case LD_R_X86_64_GOTPCRELX:
            case LD_R_X86_64_REX_GOTPCRELX:
            case LD_R_X86_64_TLSGD:
            case LD_R_X86_64_TLSLD:
            case LD_R_X86_64_GOTTPOFF:
            case LD_R_X86_64_GOTPC32_TLSDESC:
                result->pc_relative = true;
                break;
            default:
                break;
        }
    } else if (arch == LD_ARCH_RISCV64) {
        result->needs_plt =
                symbol_is_import &&
                (relocation->type == LD_R_RISCV_CALL ||
                 relocation->type == LD_R_RISCV_CALL_PLT);
        switch (relocation->type) {
            case LD_R_RISCV_BRANCH:
            case LD_R_RISCV_JAL:
            case LD_R_RISCV_CALL:
            case LD_R_RISCV_CALL_PLT:
            case LD_R_RISCV_GOT_HI20:
            case LD_R_RISCV_TLS_GOT_HI20:
            case LD_R_RISCV_TLS_GD_HI20:
            case LD_R_RISCV_PCREL_HI20:
            case LD_R_RISCV_PCREL_LO12_I:
            case LD_R_RISCV_PCREL_LO12_S:
            case LD_R_RISCV_RVC_BRANCH:
            case LD_R_RISCV_RVC_JUMP:
            case LD_R_RISCV_32_PCREL:
                result->pc_relative = true;
                break;
            default:
                break;
        }
    }
    return LD_OK;
}

int ld_elf_relocation_check_write(ld_elf_context_t *ctx,
                                  const ld_elf_object_t *object,
                                  const ld_elf_section_t *section,
                                  const ld_elf_relocation_t *relocation,
                                  size_t place_size,
                                  const char *symbol_name) {
    ld_arch_t arch = ld_elf_reloc_arch(ctx, object);
    if (!relocation) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                 symbol_name, "missing relocation");
    }
    size_t width = ld_elf_relocation_write_width(arch, relocation->type);
    if (width == SIZE_MAX) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                 symbol_name, "unsupported relocation");
    }
    if (width > place_size) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, arch, symbol_name,
                "write of %zu bytes exceeds the %zu-byte relocation buffer",
                width, place_size);
    }
    return LD_OK;
}

static void ld_elf_reloc_write_adr(uint8_t *place, int64_t immediate) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t encoded = (uint32_t) ((uint64_t) immediate & 0x1fffffU);
    instruction &= ~0x60ffffe0U;
    instruction |= (encoded & 3U) << 29U;
    instruction |= ((encoded >> 2U) & 0x7ffffU) << 5U;
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_write_branch26(uint8_t *place, int64_t displacement) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate = (uint32_t) ((uint64_t) (displacement / 4) &
                                     0x03ffffffU);
    instruction = (instruction & 0xfc000000U) | immediate;
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_write_imm19(uint8_t *place,
                                     int64_t displacement) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate =
            (uint32_t) ((uint64_t) (displacement / 4) & 0x7ffffU);
    instruction = (instruction & ~0x00ffffe0U) | (immediate << 5U);
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_write_imm14(uint8_t *place,
                                     int64_t displacement) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate =
            (uint32_t) ((uint64_t) (displacement / 4) & 0x3fffU);
    instruction = (instruction & ~0x0007ffe0U) | (immediate << 5U);
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_write_movw(uint8_t *place, uint16_t immediate) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    instruction = (instruction & ~0x001fffe0U) |
                  ((uint32_t) immediate << 5U);
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_write_imm12(uint8_t *place, uint16_t immediate) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    instruction = (instruction & ~0x003ffc00U) |
                  (((uint32_t) immediate & 0xfffU) << 10U);
    ld_elf_reloc_write_u32(place, instruction);
}

static int ld_elf_reloc_aarch64_require_instruction(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, const uint8_t *place,
        const ld_elf_reloc_values_t *values, uint32_t mask,
        uint32_t expected, const char *description) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    if ((instruction & mask) == expected) return LD_OK;
    return ld_elf_reloc_fail(
            ctx, object, section, relocation, LD_ARCH_ARM64,
            values->symbol_name,
            "expected %s instruction at relocation site, found 0x%08x",
            description, instruction);
}

static int ld_elf_reloc_apply_aarch64(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *place,
        const ld_elf_reloc_values_t *values) {
    const ld_arch_t arch = LD_ARCH_ARM64;
    uint64_t target;
    int64_t delta;

    switch (relocation->type) {
        case LD_R_AARCH64_NONE:
            return LD_OK;
        case LD_R_AARCH64_ABS64:
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            ld_elf_reloc_write_u64(place, target);
            return LD_OK;
        case LD_R_AARCH64_ABS32: {
            ld_elf_reloc_integer_t value;
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_unsigned(&value, 32U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit an unsigned 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) target);
            return LD_OK;
        }
        case LD_R_AARCH64_ABS16: {
            ld_elf_reloc_integer_t value;
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_unsigned(&value, 16U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit an unsigned 16-bit value");
            }
            ld_elf_reloc_write_u16(place, (uint16_t) target);
            return LD_OK;
        }
        case LD_R_AARCH64_MOVW_UABS_G0:
        case LD_R_AARCH64_MOVW_UABS_G0_NC:
        case LD_R_AARCH64_MOVW_UABS_G1:
        case LD_R_AARCH64_MOVW_UABS_G1_NC:
        case LD_R_AARCH64_MOVW_UABS_G2:
        case LD_R_AARCH64_MOVW_UABS_G2_NC:
        case LD_R_AARCH64_MOVW_UABS_G3: {
            /*
             * Zig 738d2be9 declares these relocation numbers but leaves
             * them unhandled.  The group checks and MOVZ/MOVK imm16 field
             * below follow the AArch64 ELF ABI and instruction encoding.
             */
            unsigned shift;
            bool no_check;
            switch (relocation->type) {
                case LD_R_AARCH64_MOVW_UABS_G0:
                    shift = 0U;
                    no_check = false;
                    break;
                case LD_R_AARCH64_MOVW_UABS_G0_NC:
                    shift = 0U;
                    no_check = true;
                    break;
                case LD_R_AARCH64_MOVW_UABS_G1:
                    shift = 16U;
                    no_check = false;
                    break;
                case LD_R_AARCH64_MOVW_UABS_G1_NC:
                    shift = 16U;
                    no_check = true;
                    break;
                case LD_R_AARCH64_MOVW_UABS_G2:
                    shift = 32U;
                    no_check = false;
                    break;
                case LD_R_AARCH64_MOVW_UABS_G2_NC:
                    shift = 32U;
                    no_check = true;
                    break;
                default:
                    shift = 48U;
                    no_check = false;
                    break;
            }
            if (no_check) {
                target = ld_elf_reloc_add_modulo(values->symbol_address,
                                                 relocation->addend);
            } else {
                ld_elf_reloc_integer_t value;
                ld_elf_reloc_integer_add(values->symbol_address,
                                         relocation->addend, &value);
                if (!ld_elf_reloc_integer_to_unsigned(
                            &value, shift + 16U, &target)) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "S + A does not fit the checked unsigned "
                            "%u-bit MOVW group",
                            shift + 16U);
                }
            }
            ld_elf_reloc_write_movw(
                    place, (uint16_t) ((target >> shift) & 0xffffU));
            return LD_OK;
        }
        case LD_R_AARCH64_PREL64:
        case LD_R_AARCH64_PREL32:
        case LD_R_AARCH64_PREL16:
        case LD_R_AARCH64_LD_PREL_LO19:
        case LD_R_AARCH64_ADR_PREL_LO21:
        case LD_R_AARCH64_TSTBR14:
        case LD_R_AARCH64_CONDBR19:
        case LD_R_AARCH64_JUMP26:
        case LD_R_AARCH64_CALL26:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P is outside the signed 64-bit range");
            }
            break;
        case LD_R_AARCH64_GOT_LD_PREL19:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT(S) + A - P is outside the signed 64-bit "
                        "range");
            }
            break;
        case LD_R_AARCH64_ADR_PREL_PG_HI21:
            if (!ld_elf_reloc_page_delta(
                        values->symbol_address, relocation->addend,
                        values->place_address, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "page displacement does not fit signed 21 bits");
            }
            ld_elf_reloc_write_adr(place, delta);
            return LD_OK;
        case LD_R_AARCH64_ADR_GOT_PAGE:
        case LD_R_AARCH64_TLSGD_ADR_PAGE21:
        case LD_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
            if (!ld_elf_reloc_page_delta(
                        values->got_entry_address, relocation->addend,
                        values->place_address, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT page displacement does not fit signed 21 bits");
            }
            ld_elf_reloc_write_adr(place, delta);
            return LD_OK;
        case LD_R_AARCH64_ADD_ABS_LO12_NC:
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            ld_elf_reloc_write_imm12(place, (uint16_t) (target & 0xfffU));
            return LD_OK;
        case LD_R_AARCH64_LD64_GOT_LO12_NC:
        case LD_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
            target = ld_elf_reloc_add_modulo(values->got_entry_address,
                                             relocation->addend);
            if ((target & 7U) != 0U) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT address is not aligned to 8 bytes");
            }
            ld_elf_reloc_write_imm12(
                    place, (uint16_t) ((target & 0xfffU) / 8U));
            return LD_OK;
        case LD_R_AARCH64_LD64_GOTPAGE_LO15: {
            /*
             * G(GDAT(S + A)) - Page(GOT), encoded as an unsigned
             * 15-bit byte offset in the load/store imm12 field (bits 14:3).
             * Zig 738d2be9 scans this relocation but omits its resolve arm;
             * the checked expression below follows the AArch64 ELF ABI.
             */
            ld_elf_reloc_integer_t value;
            uint64_t got_page = values->got_base_address & ~UINT64_C(0xfff);
            ld_elf_reloc_integer_expression(
                    values->got_entry_address, relocation->addend,
                    got_page, &value);
            if (!ld_elf_reloc_integer_to_unsigned(&value, 15U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT(S) + A - Page(GOT) does not fit an unsigned "
                        "15-bit value");
            }
            if ((target & 7U) != 0U) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT page-relative offset is not aligned to 8 "
                        "bytes");
            }
            ld_elf_reloc_write_imm12(place, (uint16_t) (target / 8U));
            return LD_OK;
        }
        case LD_R_AARCH64_TLSGD_ADD_LO12_NC:
            target = ld_elf_reloc_add_modulo(values->got_entry_address,
                                             relocation->addend);
            ld_elf_reloc_write_imm12(place,
                                     (uint16_t) (target & 0xfffU));
            return LD_OK;
        case LD_R_AARCH64_LDST8_ABS_LO12_NC:
        case LD_R_AARCH64_LDST16_ABS_LO12_NC:
        case LD_R_AARCH64_LDST32_ABS_LO12_NC:
        case LD_R_AARCH64_LDST64_ABS_LO12_NC:
        case LD_R_AARCH64_LDST128_ABS_LO12_NC: {
            uint32_t scale = 1U;
            if (relocation->type == LD_R_AARCH64_LDST16_ABS_LO12_NC) {
                scale = 2U;
            } else if (relocation->type ==
                       LD_R_AARCH64_LDST32_ABS_LO12_NC) {
                scale = 4U;
            } else if (relocation->type ==
                       LD_R_AARCH64_LDST64_ABS_LO12_NC) {
                scale = 8U;
            } else if (relocation->type ==
                       LD_R_AARCH64_LDST128_ABS_LO12_NC) {
                scale = 16U;
            }
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            if ((target & (scale - 1U)) != 0U) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "address 0x%llx is not aligned to the relocation "
                        "scale %u",
                        (unsigned long long) target, scale);
            }
            ld_elf_reloc_write_imm12(
                    place, (uint16_t) ((target & 0xfffU) / scale));
            return LD_OK;
        }
        case LD_R_AARCH64_TLSDESC_ADR_PAGE21:
            if (ld_elf_reloc_aarch64_require_instruction(
                        ctx, object, section, relocation, place, values,
                        0x9f00001fU, 0x90000000U, "ADRP X0") != LD_OK) {
                return LD_RELOCATION_ERROR;
            }
            ld_elf_reloc_write_u32(place, 0xd503201fU);
            return LD_OK;
        case LD_R_AARCH64_TLSDESC_LD64_LO12:
            if (ld_elf_reloc_aarch64_require_instruction(
                        ctx, object, section, relocation, place, values,
                        0xffc003e0U, 0xf9400000U,
                        "LDR Xn, [X0, #imm]") != LD_OK) {
                return LD_RELOCATION_ERROR;
            }
            ld_elf_reloc_write_u32(place, 0xd503201fU);
            return LD_OK;
        case LD_R_AARCH64_TLSDESC_ADD_LO12: {
            if (ld_elf_reloc_aarch64_require_instruction(
                        ctx, object, section, relocation, place, values,
                        0xffc003ffU, 0x91000000U,
                        "ADD X0, X0, #imm") != LD_OK) {
                return LD_RELOCATION_ERROR;
            }
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "TLSDESC local-exec offset S + A - TP is outside "
                        "the signed 64-bit range");
            }
            int64_t high = ld_elf_reloc_floor_shift_16(delta);
            if (!ld_elf_reloc_signed_fits(high, 16U)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "TLSDESC local-exec high-16 value does not fit a "
                        "signed 16-bit value; S + A - TP must fit a signed "
                        "32-bit value");
            }
            uint32_t instruction =
                    0xd2a00000U | ((uint32_t) (uint16_t) high << 5U);
            ld_elf_reloc_write_u32(place, instruction);
            return LD_OK;
        }
        case LD_R_AARCH64_TLSDESC_CALL: {
            if (ld_elf_reloc_aarch64_require_instruction(
                        ctx, object, section, relocation, place, values,
                        0xfffffc1fU, 0xd63f0000U, "BLR Xn") != LD_OK) {
                return LD_RELOCATION_ERROR;
            }
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "TLSDESC local-exec offset S + A - TP does not fit "
                        "a signed 32-bit value");
            }
            uint32_t instruction =
                    0xf2800000U | ((uint32_t) (uint16_t) delta << 5U);
            ld_elf_reloc_write_u32(place, instruction);
            return LD_OK;
        }
        case LD_R_AARCH64_TLSLE_ADD_TPREL_HI12:
        case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - TP is outside the signed 64-bit range");
            }
            if (relocation->type == LD_R_AARCH64_TLSLE_ADD_TPREL_HI12) {
                int64_t high = ld_elf_reloc_floor_shift_12(delta);
                if (!ld_elf_reloc_signed_fits(high, 12U)) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "TLS high-12 value does not fit signed 12 bits");
                }
                ld_elf_reloc_write_imm12(
                        place, (uint16_t) ((uint64_t) high & 0xfffU));
            } else {
                if (relocation->type ==
                            LD_R_AARCH64_TLSLE_ADD_TPREL_LO12 &&
                    (delta < 0 || delta > 0xfff)) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "TLS low-12 value is outside the checked "
                            "unsigned 12-bit range");
                }
                ld_elf_reloc_write_imm12(
                        place, (uint16_t) ((uint64_t) delta & 0xfffU));
            }
            return LD_OK;
        case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
        case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
        case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
        case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
        case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12:
        case LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC: {
            /*
             * Zig 738d2be9 defines these relocation numbers but does not
             * implement them.  This follows the AArch64 ELF ABI's TPREL
             * formulas while reusing Zig's imm12 load/store field algorithm.
             */
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - TP is outside the signed 64-bit range");
            }
            uint32_t scale;
            switch (relocation->type) {
                case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12:
                case LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
                    scale = 1U;
                    break;
                case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
                case LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
                    scale = 2U;
                    break;
                case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
                case LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
                    scale = 4U;
                    break;
                case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
                case LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
                    scale = 8U;
                    break;
                default:
                    scale = 16U;
                    break;
            }
            bool no_check = relocation->type ==
                                    LD_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC ||
                            relocation->type ==
                                    LD_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC ||
                            relocation->type ==
                                    LD_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC ||
                            relocation->type ==
                                    LD_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC ||
                            relocation->type ==
                                    LD_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC;
            if (!no_check && (delta < 0 || delta > 0xfff)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "TLS load/store offset is outside the checked "
                        "unsigned 12-bit range");
            }
            uint64_t low = (uint64_t) delta & 0xfffU;
            if ((low & (scale - 1U)) != 0U) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "TLS load/store offset is not aligned to scale %u",
                        scale);
            }
            ld_elf_reloc_write_imm12(place,
                                     (uint16_t) (low / scale));
            return LD_OK;
        }
        default:
            return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                     values->symbol_name,
                                     "unsupported relocation");
    }

    if (relocation->type == LD_R_AARCH64_PREL64) {
        ld_elf_reloc_write_u64(place, (uint64_t) delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_PREL32) {
        if (!ld_elf_reloc_signed_fits(delta, 32U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "PC-relative value does not fit signed 32 bits");
        }
        ld_elf_reloc_write_u32(place, (uint32_t) delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_PREL16) {
        if (!ld_elf_reloc_signed_fits(delta, 16U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "PC-relative value does not fit signed 16 bits");
        }
        ld_elf_reloc_write_u16(place, (uint16_t) delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_LD_PREL_LO19 ||
        relocation->type == LD_R_AARCH64_GOT_LD_PREL19) {
        if ((delta & 3) != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "literal-load displacement is not 4-byte aligned");
        }
        if (!ld_elf_reloc_signed_fits(delta, 21U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "literal-load displacement is out of range");
        }
        ld_elf_reloc_write_imm19(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_ADR_PREL_LO21) {
        if (!ld_elf_reloc_signed_fits(delta, 21U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "ADR displacement does not fit signed 21 bits");
        }
        ld_elf_reloc_write_adr(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_TSTBR14) {
        if ((delta & 3) != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "test-branch displacement is not 4-byte aligned");
        }
        if (!ld_elf_reloc_signed_fits(delta, 16U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "test-branch displacement is out of range");
        }
        ld_elf_reloc_write_imm14(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_AARCH64_CONDBR19) {
        if ((delta & 3) != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "conditional branch displacement is not 4-byte aligned");
        }
        if (!ld_elf_reloc_signed_fits(delta, 21U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "conditional branch displacement is out of range");
        }
        ld_elf_reloc_write_imm19(place, delta);
        return LD_OK;
    }

    if ((delta & 3) != 0) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, arch, values->symbol_name,
                "branch displacement is not 4-byte aligned");
    }
    if (!ld_elf_reloc_signed_fits(delta, 28U)) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                 values->symbol_name,
                                 "branch displacement is out of range");
    }
    ld_elf_reloc_write_branch26(place, delta);
    return LD_OK;
}

static int ld_elf_reloc_apply_x86_64(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *place,
        const ld_elf_reloc_values_t *values) {
    const ld_arch_t arch = LD_ARCH_AMD64;
    uint64_t target;
    int64_t delta;
    ld_elf_reloc_integer_t value;

    switch (relocation->type) {
        case LD_R_X86_64_NONE:
            return LD_OK;
        case LD_R_X86_64_64:
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            ld_elf_reloc_write_u64(place, target);
            return LD_OK;
        case LD_R_X86_64_32:
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_unsigned(&value, 32U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit an unsigned 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) target);
            return LD_OK;
        case LD_R_X86_64_32S:
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_signed(&value, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_16:
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_unsigned(&value, 16U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit an unsigned 16-bit value");
            }
            ld_elf_reloc_write_u16(place, (uint16_t) target);
            return LD_OK;
        case LD_R_X86_64_8:
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_signed(&value, 8U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit a signed 8-bit value");
            }
            place[0] = (uint8_t) delta;
            return LD_OK;
        case LD_R_X86_64_PC16:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 16U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P does not fit a signed 16-bit value");
            }
            ld_elf_reloc_write_u16(place, (uint16_t) delta);
            return LD_OK;
        case LD_R_X86_64_PC8:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 8U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P does not fit a signed 8-bit value");
            }
            place[0] = (uint8_t) delta;
            return LD_OK;
        case LD_R_X86_64_PC32:
        case LD_R_X86_64_PLT32:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_PC64:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOT32:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->got_base_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT(S) + A - GOT does not fit a signed 32-bit "
                        "value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOT64:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->got_base_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT(S) + A - GOT is outside the signed 64-bit "
                        "range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOTPCREL:
        case LD_R_X86_64_GOTPCRELX:
        case LD_R_X86_64_REX_GOTPCRELX:
        case LD_R_X86_64_GOTTPOFF:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->place_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "G + A - P does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOTPCREL64:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT(S) + A - P is outside the signed 64-bit "
                        "range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOTPC32:
            if (!ld_elf_reloc_expression_signed(
                        values->got_base_address, relocation->addend,
                        values->place_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT + A - P does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOTPC64:
            if (!ld_elf_reloc_expression_signed(
                        values->got_base_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT + A - P is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_GOTOFF64:
        case LD_R_X86_64_PLTOFF64:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->got_base_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - GOT is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_SIZE32:
            ld_elf_reloc_integer_add(values->symbol_size,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_signed(&value, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "Z + A does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_SIZE64:
            ld_elf_reloc_integer_add(values->symbol_size,
                                     relocation->addend, &value);
            if (!ld_elf_reloc_integer_to_signed(&value, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "Z + A is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_TLSGD:
        case LD_R_X86_64_TLSLD:
        case LD_R_X86_64_GOTPC32_TLSDESC:
        case LD_R_X86_64_TLSDESC_CALL:
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "TLSGD/TLSLD/TLSDESC must be applied with its paired "
                    "resolver relocation");
        case LD_R_X86_64_TPOFF32:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - TP does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_TPOFF64:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - TP is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        case LD_R_X86_64_DTPOFF32:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->tls_block_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - DTP does not fit a signed 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) delta);
            return LD_OK;
        case LD_R_X86_64_DTPOFF64:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->tls_block_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - DTP is outside the signed 64-bit range");
            }
            ld_elf_reloc_write_u64(place, (uint64_t) delta);
            return LD_OK;
        default:
            return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                     values->symbol_name,
                                     "unsupported relocation");
    }
}

static bool ld_elf_reloc_riscv_i_type(uint32_t instruction) {
    switch (instruction & 0x7fU) {
        case 0x03U: /* LOAD */
        case 0x07U: /* LOAD-FP */
        case 0x13U: /* OP-IMM */
        case 0x1bU: /* OP-IMM-32 */
        case 0x67U: /* JALR */
            return true;
        default:
            return false;
    }
}

static bool ld_elf_reloc_riscv_s_type(uint32_t instruction) {
    uint32_t opcode = instruction & 0x7fU;
    return opcode == 0x23U || opcode == 0x27U;
}

static int ld_elf_reloc_riscv_require_u32(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, const uint8_t *place,
        const ld_elf_reloc_values_t *values, uint32_t mask,
        uint32_t expected, const char *description) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    if ((instruction & mask) == expected) return LD_OK;
    return ld_elf_reloc_fail(
            ctx, object, section, relocation, LD_ARCH_RISCV64,
            values->symbol_name,
            "expected %s instruction at relocation site, found 0x%08x",
            description, instruction);
}

static void ld_elf_reloc_riscv_write_u_type(uint8_t *place,
                                            int64_t value) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t compensated = (uint32_t) value + 0x800U;
    instruction = (instruction & 0x00000fffU) |
                  (compensated & 0xfffff000U);
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_riscv_write_i_type(uint8_t *place,
                                            int64_t value) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    instruction = (instruction & 0x000fffffU) |
                  (((uint32_t) value & 0xfffU) << 20U);
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_riscv_write_s_type(uint8_t *place,
                                            int64_t value) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate = (uint32_t) value & 0xfffU;
    instruction &= ~0xfe000f80U;
    instruction |= (immediate & 0x1fU) << 7U;
    instruction |= (immediate & 0xfe0U) << 20U;
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_riscv_write_b_type(uint8_t *place,
                                            int64_t value) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate = (uint32_t) value & 0x1fffU;
    instruction &= ~0xfe000f80U;
    instruction |= (immediate & 0x1000U) << 19U;
    instruction |= (immediate & 0x07e0U) << 20U;
    instruction |= (immediate & 0x001eU) << 7U;
    instruction |= (immediate & 0x0800U) >> 4U;
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_riscv_write_j_type(uint8_t *place,
                                            int64_t value) {
    uint32_t instruction = ld_elf_reloc_read_u32(place);
    uint32_t immediate = (uint32_t) value & 0x1fffffU;
    instruction &= 0x00000fffU;
    instruction |= (immediate & 0x100000U) << 11U;
    instruction |= (immediate & 0x0007feU) << 20U;
    instruction |= (immediate & 0x000800U) << 9U;
    instruction |= immediate & 0x0ff000U;
    ld_elf_reloc_write_u32(place, instruction);
}

static void ld_elf_reloc_riscv_write_rvc_branch(uint8_t *place,
                                                int64_t value) {
    uint16_t instruction = ld_elf_reloc_read_u16(place);
    uint16_t immediate = (uint16_t) value & 0x01ffU;
    instruction &= (uint16_t) ~0x1c7cU;
    instruction |= (uint16_t) ((immediate & 0x0100U) << 4U);
    instruction |= (uint16_t) ((immediate & 0x0018U) << 7U);
    instruction |= (uint16_t) ((immediate & 0x00c0U) >> 1U);
    instruction |= (uint16_t) ((immediate & 0x0006U) << 2U);
    instruction |= (uint16_t) ((immediate & 0x0020U) >> 3U);
    ld_elf_reloc_write_u16(place, instruction);
}

static void ld_elf_reloc_riscv_write_rvc_jump(uint8_t *place,
                                              int64_t value) {
    uint16_t instruction = ld_elf_reloc_read_u16(place);
    uint16_t immediate = (uint16_t) value & 0x0fffU;
    instruction &= (uint16_t) ~0x1ffcU;
    instruction |= (uint16_t) ((immediate & 0x0800U) << 1U);
    instruction |= (uint16_t) ((immediate & 0x0010U) << 7U);
    instruction |= (uint16_t) ((immediate & 0x0300U) << 1U);
    instruction |= (uint16_t) ((immediate & 0x0400U) >> 2U);
    instruction |= (uint16_t) ((immediate & 0x0040U) << 1U);
    instruction |= (uint16_t) ((immediate & 0x0080U) >> 1U);
    instruction |= (uint16_t) ((immediate & 0x000eU) << 2U);
    instruction |= (uint16_t) ((immediate & 0x0020U) >> 3U);
    ld_elf_reloc_write_u16(place, instruction);
}

int ld_elf_relocation_apply_riscv_uleb_pair(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *set_relocation,
        const ld_elf_relocation_t *sub_relocation, uint8_t *place,
        size_t place_size, const ld_elf_reloc_values_t *set_values,
        const ld_elf_reloc_values_t *sub_values) {
    const char *set_name = set_values ? set_values->symbol_name : NULL;
    const char *sub_name = sub_values ? sub_values->symbol_name : NULL;
    if (!set_relocation || !sub_relocation || !set_values || !sub_values ||
        set_relocation->type != LD_R_RISCV_SET_ULEB128 ||
        sub_relocation->type != LD_R_RISCV_SUB_ULEB128 ||
        set_relocation->offset != sub_relocation->offset ||
        set_relocation->relocation_section_index !=
                sub_relocation->relocation_section_index) {
        return ld_elf_reloc_fail(
                ctx, object, section, set_relocation, LD_ARCH_RISCV64,
                set_name,
                "invalid R_RISCV_SET_ULEB128/R_RISCV_SUB_ULEB128 pair "
                "for SUB symbol '%s'",
                sub_name ? sub_name : "<unknown symbol>");
    }

    size_t encoded_size = 0U;
    ld_elf_riscv_uleb_result_t result = ld_elf_riscv_uleb_apply_pair(
            place, place_size, set_values->symbol_address,
            set_relocation->addend, sub_values->symbol_address,
            sub_relocation->addend, &encoded_size);
    if (result == LD_ELF_RISCV_ULEB_OK) return LD_OK;
    return ld_elf_reloc_fail(
            ctx, object, section, set_relocation, LD_ARCH_RISCV64, set_name,
            "SET/SUB expression against SUB symbol '%s' failed: %s",
            sub_name ? sub_name : "<unknown symbol>",
            ld_elf_riscv_uleb_result_string(result));
}

static int ld_elf_reloc_riscv_apply_align(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *place,
        size_t place_size, const ld_elf_reloc_values_t *values) {
    if (relocation->addend < 0) {
        return ld_elf_reloc_fail(ctx, object, section, relocation,
                                 LD_ARCH_RISCV64, values->symbol_name,
                                 "alignment padding addend is negative");
    }
    uint64_t padding = (uint64_t) relocation->addend;
    if (padding > place_size) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_RISCV64,
                values->symbol_name,
                "alignment padding of %llu bytes exceeds the %zu-byte "
                "relocation buffer",
                (unsigned long long) padding, place_size);
    }
    if (padding == 0U) return LD_OK;
    if (!place) {
        return ld_elf_reloc_fail(ctx, object, section, relocation,
                                 LD_ARCH_RISCV64, values->symbol_name,
                                 "alignment padding buffer is null");
    }
    if ((padding & 1U) != 0U) {
        return ld_elf_reloc_fail(ctx, object, section, relocation,
                                 LD_ARCH_RISCV64, values->symbol_name,
                                 "alignment padding is not 2-byte aligned");
    }

    uint64_t alignment = 1U;
    while (alignment <= padding) alignment <<= 1U;
    if (values->place_address > UINT64_MAX - padding ||
        ((values->place_address + padding) & (alignment - 1U)) != 0U) {
        return ld_elf_reloc_fail(
                ctx, object, section, relocation, LD_ARCH_RISCV64,
                values->symbol_name,
                "preserving %llu padding bytes does not align the following "
                "instruction to %llu bytes; RISC-V relaxation is required",
                (unsigned long long) padding,
                (unsigned long long) alignment);
    }

    size_t offset = 0U;
    while (offset < (size_t) padding) {
        size_t remaining = (size_t) padding - offset;
        if (remaining >= 4U &&
            ld_elf_reloc_read_u32(place + offset) == 0x00000013U) {
            offset += 4U;
        } else if (remaining >= 2U &&
                   ld_elf_reloc_read_u16(place + offset) == 0x0001U) {
            offset += 2U;
        } else {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, LD_ARCH_RISCV64,
                    values->symbol_name,
                    "alignment region contains a non-NOP instruction at "
                    "padding offset 0x%zx",
                    offset);
        }
    }
    return LD_OK;
}

static int ld_elf_reloc_apply_riscv64(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, uint8_t *place,
        size_t place_size, const ld_elf_reloc_values_t *values) {
    const ld_arch_t arch = LD_ARCH_RISCV64;
    ld_elf_reloc_integer_t integer;
    uint64_t target;
    int64_t delta;
    uint32_t instruction;

    switch (relocation->type) {
        case LD_R_RISCV_NONE:
        case LD_R_RISCV_RELAX:
            return LD_OK;
        case LD_R_RISCV_32:
            ld_elf_reloc_integer_add(values->symbol_address,
                                     relocation->addend, &integer);
            if (!ld_elf_reloc_integer_to_unsigned(&integer, 32U, &target)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit an unsigned 32-bit value");
            }
            ld_elf_reloc_write_u32(place, (uint32_t) target);
            return LD_OK;
        case LD_R_RISCV_64:
            ld_elf_reloc_write_u64(
                    place, ld_elf_reloc_add_modulo(values->symbol_address,
                                                   relocation->addend));
            return LD_OK;
        case LD_R_RISCV_RELATIVE:
            ld_elf_reloc_write_u64(
                    place, ld_elf_reloc_add_modulo(values->image_base_address,
                                                   relocation->addend));
            return LD_OK;
        case LD_R_RISCV_TLS_DTPREL32:
        case LD_R_RISCV_TLS_DTPREL64:
        case LD_R_RISCV_TLS_TPREL32:
        case LD_R_RISCV_TLS_TPREL64: {
            bool dynamic = relocation->type == LD_R_RISCV_TLS_DTPREL32 ||
                           relocation->type == LD_R_RISCV_TLS_DTPREL64;
            bool wide = relocation->type == LD_R_RISCV_TLS_DTPREL64 ||
                        relocation->type == LD_R_RISCV_TLS_TPREL64;
            uint64_t base = dynamic ? values->tls_block_address
                                    : values->thread_pointer_address;
            if (dynamic) {
                if (base > UINT64_MAX - LD_ELF_RISCV_DTP_OFFSET) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "DTP plus the RISC-V TLS_DTV_OFFSET overflows "
                            "the address space");
                }
                base += LD_ELF_RISCV_DTP_OFFSET;
            }
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend, base,
                        wide ? 64U : 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - %s does not fit a signed %u-bit value",
                        dynamic ? "DTP" : "TP", wide ? 64U : 32U);
            }
            if (wide) {
                ld_elf_reloc_write_u64(place, (uint64_t) delta);
            } else {
                ld_elf_reloc_write_u32(place, (uint32_t) delta);
            }
            return LD_OK;
        }
        case LD_R_RISCV_BRANCH:
        case LD_R_RISCV_JAL:
        case LD_R_RISCV_RVC_BRANCH:
        case LD_R_RISCV_RVC_JUMP:
        case LD_R_RISCV_CALL:
        case LD_R_RISCV_CALL_PLT:
        case LD_R_RISCV_PCREL_HI20:
        case LD_R_RISCV_32_PCREL:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->place_address, 64U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - P is outside the signed 64-bit range");
            }
            break;
        case LD_R_RISCV_GOT_HI20:
        case LD_R_RISCV_TLS_GOT_HI20:
        case LD_R_RISCV_TLS_GD_HI20:
            if (!ld_elf_reloc_expression_signed(
                        values->got_entry_address, relocation->addend,
                        values->place_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "GOT entry + A - P does not fit a signed 32-bit "
                        "value");
            }
            if (ld_elf_reloc_riscv_require_u32(
                        ctx, object, section, relocation, place, values,
                        0x7fU, 0x17U, "AUIPC") != LD_OK) {
                return LD_RELOCATION_ERROR;
            }
            ld_elf_reloc_riscv_write_u_type(place, delta);
            return LD_OK;
        case LD_R_RISCV_PCREL_LO12_I:
        case LD_R_RISCV_PCREL_LO12_S: {
            if (relocation->addend != 0) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "PCREL_LO12 relocation addend must be zero");
            }
            if (!values->paired_hi.present) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "missing paired R_RISCV_PCREL_HI20, "
                        "R_RISCV_GOT_HI20, R_RISCV_TLS_GOT_HI20, or "
                        "R_RISCV_TLS_GD_HI20 relocation");
            }
            if (values->paired_hi.type != LD_R_RISCV_PCREL_HI20 &&
                values->paired_hi.type != LD_R_RISCV_GOT_HI20 &&
                values->paired_hi.type != LD_R_RISCV_TLS_GOT_HI20 &&
                values->paired_hi.type != LD_R_RISCV_TLS_GD_HI20) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "paired relocation has invalid type %s (%u)",
                        ld_elf_relocation_name(arch,
                                               values->paired_hi.type),
                        values->paired_hi.type);
            }
            if (values->symbol_address != values->paired_hi.place_address) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "LO12 symbol does not identify its paired HI20 "
                        "relocation");
            }
            uint64_t pair_target =
                    values->paired_hi.type == LD_R_RISCV_PCREL_HI20
                            ? values->paired_hi.symbol_address
                            : values->paired_hi.got_entry_address;
            if (!ld_elf_reloc_expression_signed(
                        pair_target, values->paired_hi.addend,
                        values->paired_hi.place_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "paired %s displacement for symbol '%s' does not "
                        "fit a signed 32-bit value",
                        ld_elf_relocation_name(arch,
                                               values->paired_hi.type),
                        values->paired_hi.symbol_name
                                ? values->paired_hi.symbol_name
                                : "<unknown symbol>");
            }
            instruction = ld_elf_reloc_read_u32(place);
            if (relocation->type == LD_R_RISCV_PCREL_LO12_I) {
                if (!ld_elf_reloc_riscv_i_type(instruction)) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "expected I-type instruction at relocation "
                            "site, found 0x%08x",
                            instruction);
                }
                ld_elf_reloc_riscv_write_i_type(place, delta);
            } else {
                if (!ld_elf_reloc_riscv_s_type(instruction)) {
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "expected S-type instruction at relocation "
                            "site, found 0x%08x",
                            instruction);
                }
                ld_elf_reloc_riscv_write_s_type(place, delta);
            }
            return LD_OK;
        }
        case LD_R_RISCV_HI20:
        case LD_R_RISCV_LO12_I:
        case LD_R_RISCV_LO12_S:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend, 0U, 32U,
                        &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A does not fit a signed 32-bit value");
            }
            if (relocation->type == LD_R_RISCV_HI20) {
                if (ld_elf_reloc_riscv_require_u32(
                            ctx, object, section, relocation, place, values,
                            0x7fU, 0x37U, "LUI") != LD_OK) {
                    return LD_RELOCATION_ERROR;
                }
                ld_elf_reloc_riscv_write_u_type(place, delta);
            } else {
                instruction = ld_elf_reloc_read_u32(place);
                if (relocation->type == LD_R_RISCV_LO12_I) {
                    if (!ld_elf_reloc_riscv_i_type(instruction)) {
                        return ld_elf_reloc_fail(
                                ctx, object, section, relocation, arch,
                                values->symbol_name,
                                "expected I-type instruction at relocation "
                                "site, found 0x%08x",
                                instruction);
                    }
                    ld_elf_reloc_riscv_write_i_type(place, delta);
                } else {
                    if (!ld_elf_reloc_riscv_s_type(instruction)) {
                        return ld_elf_reloc_fail(
                                ctx, object, section, relocation, arch,
                                values->symbol_name,
                                "expected S-type instruction at relocation "
                                "site, found 0x%08x",
                                instruction);
                    }
                    ld_elf_reloc_riscv_write_s_type(place, delta);
                }
            }
            return LD_OK;
        case LD_R_RISCV_TPREL_HI20:
        case LD_R_RISCV_TPREL_LO12_I:
        case LD_R_RISCV_TPREL_LO12_S:
            if (!ld_elf_reloc_expression_signed(
                        values->symbol_address, relocation->addend,
                        values->thread_pointer_address, 32U, &delta)) {
                return ld_elf_reloc_fail(
                        ctx, object, section, relocation, arch,
                        values->symbol_name,
                        "S + A - TP does not fit a signed 32-bit value");
            }
            if (relocation->type == LD_R_RISCV_TPREL_HI20) {
                if (ld_elf_reloc_riscv_require_u32(
                            ctx, object, section, relocation, place, values,
                            0x7fU, 0x37U, "LUI") != LD_OK) {
                    return LD_RELOCATION_ERROR;
                }
                ld_elf_reloc_riscv_write_u_type(place, delta);
            } else {
                instruction = ld_elf_reloc_read_u32(place);
                if (relocation->type == LD_R_RISCV_TPREL_LO12_I) {
                    if (!ld_elf_reloc_riscv_i_type(instruction)) {
                        return ld_elf_reloc_fail(
                                ctx, object, section, relocation, arch,
                                values->symbol_name,
                                "expected I-type instruction at relocation "
                                "site, found 0x%08x",
                                instruction);
                    }
                    ld_elf_reloc_riscv_write_i_type(place, delta);
                } else {
                    if (!ld_elf_reloc_riscv_s_type(instruction)) {
                        return ld_elf_reloc_fail(
                                ctx, object, section, relocation, arch,
                                values->symbol_name,
                                "expected S-type instruction at relocation "
                                "site, found 0x%08x",
                                instruction);
                    }
                    ld_elf_reloc_riscv_write_s_type(place, delta);
                }
            }
            return LD_OK;
        case LD_R_RISCV_TPREL_ADD:
            return ld_elf_reloc_riscv_require_u32(
                    ctx, object, section, relocation, place, values,
                    0xfe00707fU, 0x00000033U, "ADD");
        case LD_R_RISCV_ADD8:
        case LD_R_RISCV_ADD16:
        case LD_R_RISCV_ADD32:
        case LD_R_RISCV_ADD64:
        case LD_R_RISCV_SUB8:
        case LD_R_RISCV_SUB16:
        case LD_R_RISCV_SUB32:
        case LD_R_RISCV_SUB64: {
            bool subtract = relocation->type >= LD_R_RISCV_SUB8;
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            switch (ld_elf_relocation_write_width(arch, relocation->type)) {
                case 1U:
                    place[0] = (uint8_t) (subtract ? place[0] - target
                                                   : place[0] + target);
                    break;
                case 2U: {
                    uint16_t value = ld_elf_reloc_read_u16(place);
                    value = (uint16_t) (subtract ? value - target
                                                 : value + target);
                    ld_elf_reloc_write_u16(place, value);
                    break;
                }
                case 4U: {
                    uint32_t value = ld_elf_reloc_read_u32(place);
                    value = (uint32_t) (subtract ? value - target
                                                 : value + target);
                    ld_elf_reloc_write_u32(place, value);
                    break;
                }
                case 8U: {
                    uint64_t value = ld_elf_reloc_read_u64(place);
                    value = subtract ? value - target : value + target;
                    ld_elf_reloc_write_u64(place, value);
                    break;
                }
                default:
                    return ld_elf_reloc_fail(
                            ctx, object, section, relocation, arch,
                            values->symbol_name,
                            "internal error selecting ADD/SUB write width");
            }
            return LD_OK;
        }
        case LD_R_RISCV_SET6:
        case LD_R_RISCV_SUB6:
            target = ld_elf_reloc_add_modulo(values->symbol_address,
                                             relocation->addend);
            if (relocation->type == LD_R_RISCV_SET6) {
                place[0] = (uint8_t) ((place[0] & 0xc0U) | (target & 0x3fU));
            } else {
                place[0] = (uint8_t) ((place[0] & 0xc0U) |
                                      ((place[0] - target) & 0x3fU));
            }
            return LD_OK;
        case LD_R_RISCV_SET8:
            place[0] = (uint8_t) ld_elf_reloc_add_modulo(
                    values->symbol_address, relocation->addend);
            return LD_OK;
        case LD_R_RISCV_SET16:
            ld_elf_reloc_write_u16(
                    place, (uint16_t) ld_elf_reloc_add_modulo(
                                   values->symbol_address,
                                   relocation->addend));
            return LD_OK;
        case LD_R_RISCV_SET32:
            ld_elf_reloc_write_u32(
                    place, (uint32_t) ld_elf_reloc_add_modulo(
                                   values->symbol_address,
                                   relocation->addend));
            return LD_OK;
        case LD_R_RISCV_SET_ULEB128:
        case LD_R_RISCV_SUB_ULEB128: {
            size_t encoded_size = 0U;
            ld_elf_riscv_uleb_result_t result =
                    ld_elf_riscv_uleb_apply(
                            place, place_size,
                            relocation->type == LD_R_RISCV_SUB_ULEB128,
                            values->symbol_address, relocation->addend,
                            &encoded_size);
            if (result == LD_ELF_RISCV_ULEB_OK) return LD_OK;
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "cannot apply RISC-V ULEB128 relocation: %s",
                    ld_elf_riscv_uleb_result_string(result));
        }
        case LD_R_RISCV_ALIGN:
            return ld_elf_reloc_riscv_apply_align(
                    ctx, object, section, relocation, place, place_size,
                    values);
        default:
            return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                     values->symbol_name,
                                     "unsupported relocation");
    }

    if (relocation->type == LD_R_RISCV_BRANCH) {
        if (ld_elf_reloc_riscv_require_u32(
                    ctx, object, section, relocation, place, values, 0x7fU,
                    0x63U, "BRANCH") != LD_OK) {
            return LD_RELOCATION_ERROR;
        }
        if ((delta & 1) != 0 || !ld_elf_reloc_signed_fits(delta, 13U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "branch displacement is not 2-byte aligned or is "
                    "outside the signed 13-bit range");
        }
        ld_elf_reloc_riscv_write_b_type(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_JAL) {
        if (ld_elf_reloc_riscv_require_u32(
                    ctx, object, section, relocation, place, values, 0x7fU,
                    0x6fU, "JAL") != LD_OK) {
            return LD_RELOCATION_ERROR;
        }
        if ((delta & 1) != 0 || !ld_elf_reloc_signed_fits(delta, 21U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "JAL displacement is not 2-byte aligned or is outside "
                    "the signed 21-bit range");
        }
        ld_elf_reloc_riscv_write_j_type(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_RVC_BRANCH) {
        uint16_t compressed = ld_elf_reloc_read_u16(place);
        uint16_t funct3 = compressed >> 13U;
        if ((compressed & 3U) != 1U || (funct3 != 6U && funct3 != 7U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "expected C.BEQZ or C.BNEZ instruction at relocation "
                    "site, found 0x%04x",
                    compressed);
        }
        if ((delta & 1) != 0 || !ld_elf_reloc_signed_fits(delta, 9U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "compressed branch displacement is not 2-byte aligned "
                    "or is outside the signed 9-bit range");
        }
        ld_elf_reloc_riscv_write_rvc_branch(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_RVC_JUMP) {
        uint16_t compressed = ld_elf_reloc_read_u16(place);
        if ((compressed & 0xe003U) != 0xa001U) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "expected C.J instruction at relocation site, found "
                    "0x%04x",
                    compressed);
        }
        if ((delta & 1) != 0 || !ld_elf_reloc_signed_fits(delta, 12U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "compressed jump displacement is not 2-byte aligned or "
                    "is outside the signed 12-bit range");
        }
        ld_elf_reloc_riscv_write_rvc_jump(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_CALL ||
        relocation->type == LD_R_RISCV_CALL_PLT) {
        if (ld_elf_reloc_riscv_require_u32(
                    ctx, object, section, relocation, place, values, 0x7fU,
                    0x17U, "AUIPC") != LD_OK) {
            return LD_RELOCATION_ERROR;
        }
        uint32_t auipc = ld_elf_reloc_read_u32(place);
        uint32_t low = ld_elf_reloc_read_u32(place + 4U);
        uint32_t auipc_rd = (auipc >> 7U) & 0x1fU;
        uint32_t low_rd = (low >> 7U) & 0x1fU;
        uint32_t low_rs1 = (low >> 15U) & 0x1fU;
        bool jalr = (low & 0x707fU) == 0x0067U &&
                    low_rs1 == auipc_rd;
        bool nature_address = relocation->type == LD_R_RISCV_CALL &&
                              (low & 0x707fU) == 0x0013U &&
                              low_rd == auipc_rd && low_rs1 == auipc_rd;
        if (!jalr && !nature_address) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "expected an AUIPC followed by JALR, or Nature's "
                    "AUIPC+ADDI address sequence, found second instruction "
                    "0x%08x",
                    low);
        }
        if (!ld_elf_reloc_signed_fits(delta, 32U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "call/address displacement is outside the signed 32-bit "
                    "range");
        }
        if (jalr && (delta & 1) != 0) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "call displacement is not 2-byte aligned");
        }
        ld_elf_reloc_riscv_write_u_type(place, delta);
        ld_elf_reloc_riscv_write_i_type(place + 4U, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_PCREL_HI20) {
        if (!ld_elf_reloc_signed_fits(delta, 32U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "PC-relative displacement does not fit a signed 32-bit "
                    "value");
        }
        if (ld_elf_reloc_riscv_require_u32(
                    ctx, object, section, relocation, place, values, 0x7fU,
                    0x17U, "AUIPC") != LD_OK) {
            return LD_RELOCATION_ERROR;
        }
        ld_elf_reloc_riscv_write_u_type(place, delta);
        return LD_OK;
    }
    if (relocation->type == LD_R_RISCV_32_PCREL) {
        if (!ld_elf_reloc_signed_fits(delta, 32U)) {
            return ld_elf_reloc_fail(
                    ctx, object, section, relocation, arch,
                    values->symbol_name,
                    "S + A - P does not fit a signed 32-bit value");
        }
        ld_elf_reloc_write_u32(place, (uint32_t) delta);
        return LD_OK;
    }

    return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                             values->symbol_name,
                             "internal error applying relocation");
}

int ld_elf_relocation_apply(ld_elf_context_t *ctx,
                            const ld_elf_object_t *object,
                            const ld_elf_section_t *section,
                            const ld_elf_relocation_t *relocation,
                            uint8_t *place, size_t place_size,
                            const ld_elf_reloc_values_t *values) {
    ld_arch_t arch = ld_elf_reloc_arch(ctx, object);
    const char *symbol_name = values ? values->symbol_name : NULL;
    if (!relocation || !values) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                 symbol_name,
                                 "missing relocation application argument");
    }
    if (ld_elf_relocation_check_write(ctx, object, section, relocation,
                                      place_size, symbol_name) != LD_OK) {
        return LD_RELOCATION_ERROR;
    }
    size_t width = ld_elf_relocation_write_width(arch, relocation->type);
    if (width != 0U && !place) {
        return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                                 symbol_name, "relocation buffer is null");
    }

    if (arch == LD_ARCH_ARM64) {
        return ld_elf_reloc_apply_aarch64(ctx, object, section, relocation,
                                          place, values);
    }
    if (arch == LD_ARCH_AMD64) {
        return ld_elf_reloc_apply_x86_64(ctx, object, section, relocation,
                                         place, values);
    }
    if (arch == LD_ARCH_RISCV64) {
        return ld_elf_reloc_apply_riscv64(ctx, object, section, relocation,
                                          place, place_size, values);
    }
    return ld_elf_reloc_fail(ctx, object, section, relocation, arch,
                             symbol_name, "unsupported target architecture");
}

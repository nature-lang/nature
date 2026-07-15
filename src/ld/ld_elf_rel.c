#include "ld_elf_rel.h"

#include "ld_elf_reloc.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

/*
 * Zig 738d2be9 recognizes SHT_REL while scanning Object sections, but its
 * preadRelocsAlloc function still divides the section by Elf64_Rela and reads
 * every entry as a 24-byte explicit-addend record.  Nature keeps the useful
 * unified-Rela internal model while decoding the standard 16-byte Elf64_Rel
 * wire record and recovering A from the relocation field here.
 */

static uint16_t ld_elf_rel_read_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] |
                       ((uint16_t) bytes[1] << 8U));
}

static uint32_t ld_elf_rel_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) |
           ((uint32_t) bytes[3] << 24U);
}

static uint64_t ld_elf_rel_read_u64(const uint8_t *bytes) {
    return (uint64_t) ld_elf_rel_read_u32(bytes) |
           ((uint64_t) ld_elf_rel_read_u32(bytes + 4U) << 32U);
}

static int64_t ld_elf_rel_sign_extend(uint64_t value, unsigned bits) {
    if (bits == 0U || bits > 64U) return 0;
    if (bits < 64U) {
        uint64_t mask = (UINT64_C(1) << bits) - 1U;
        uint64_t sign = UINT64_C(1) << (bits - 1U);
        value &= mask;
        if ((value & sign) == 0U) return (int64_t) value;
        uint64_t magnitude = ((~value) & mask) + 1U;
        return -(int64_t) magnitude;
    }
    if (value <= INT64_MAX) return (int64_t) value;
    uint64_t magnitude = (~value) + 1U;
    if (magnitude == UINT64_C(0x8000000000000000)) return INT64_MIN;
    return -(int64_t) magnitude;
}

static ld_arch_t ld_elf_rel_arch(const ld_elf_object_t *object) {
    if (!object) return 0;
    if (object->header.e_machine == LD_ELF_EM_X86_64) return LD_ARCH_AMD64;
    if (object->header.e_machine == LD_ELF_EM_AARCH64) return LD_ARCH_ARM64;
    if (object->header.e_machine == LD_ELF_EM_RISCV) return LD_ARCH_RISCV64;
    return 0;
}

static int ld_elf_rel_fail(ld_elf_context_t *ctx, int code,
                           const ld_elf_object_t *object,
                           const ld_elf_section_t *target,
                           const ld_elf_relocation_t *relocation,
                           const char *format, ...) {
    char detail[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);
    ld_arch_t arch = ld_elf_rel_arch(object);
    return ld_elf_fail(
            ctx, code,
            "cannot decode implicit addend for ELF relocation %s (%u) at "
            "offset 0x%llx in section '%s' of '%s': %s",
            ld_elf_relocation_name(arch, relocation ? relocation->type : 0U),
            relocation ? relocation->type : 0U,
            (unsigned long long) (relocation ? relocation->offset : 0U),
            target && target->name ? target->name : "<unknown>",
            object && object->display_name ? object->display_name : "<unknown>",
            detail);
}

static int ld_elf_rel_place(ld_elf_context_t *ctx,
                            const ld_elf_object_t *object,
                            const ld_elf_section_t *target,
                            const ld_elf_relocation_t *relocation,
                            size_t width, const uint8_t **place) {
    *place = NULL;
    if (!target || !relocation) {
        return ld_elf_rel_fail(ctx, LD_INVALID_INPUT, object, target,
                               relocation, "missing relocation target");
    }
    if (target->nobits || target->header.sh_type == LD_ELF_SHT_NOBITS) {
        return ld_elf_rel_fail(ctx, LD_INVALID_INPUT, object, target,
                               relocation,
                               "SHT_NOBITS has no bytes containing an addend");
    }
    if (relocation->offset > SIZE_MAX ||
        relocation->offset > target->data_size ||
        width > target->data_size - (size_t) relocation->offset) {
        return ld_elf_rel_fail(
                ctx, LD_INVALID_INPUT, object, target, relocation,
                "%zu-byte relocation field extends past the %zu-byte target",
                width, target->data_size);
    }
    if (width != 0U && !target->data) {
        return ld_elf_rel_fail(ctx, LD_INVALID_INPUT, object, target,
                               relocation, "target section has no data");
    }
    *place = target->data ? target->data + (size_t) relocation->offset : NULL;
    return LD_OK;
}

static int ld_elf_rel_require_u32(ld_elf_context_t *ctx,
                                  const ld_elf_object_t *object,
                                  const ld_elf_section_t *target,
                                  const ld_elf_relocation_t *relocation,
                                  uint32_t instruction, uint32_t mask,
                                  uint32_t expected,
                                  const char *description) {
    if ((instruction & mask) == expected) return LD_OK;
    return ld_elf_rel_fail(
            ctx, LD_INVALID_INPUT, object, target, relocation,
            "expected %s instruction, found 0x%08x", description,
            instruction);
}

static int ld_elf_rel_decode_x86(ld_elf_context_t *ctx,
                                 const ld_elf_object_t *object,
                                 const ld_elf_section_t *target,
                                 const ld_elf_relocation_t *relocation,
                                 int64_t *addend) {
    if (relocation->type == LD_ELF_R_X86_64_NONE ||
        relocation->type == LD_ELF_R_X86_64_TLSDESC_CALL) {
        *addend = 0;
        return LD_OK;
    }
    if (!ld_elf_relocation_supported(LD_ARCH_AMD64, relocation->type)) {
        return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                               relocation,
                               "relocation has no supported REL decoder");
    }
    size_t width = ld_elf_relocation_write_width(LD_ARCH_AMD64,
                                                 relocation->type);
    const uint8_t *place;
    int result = ld_elf_rel_place(ctx, object, target, relocation, width,
                                  &place);
    if (result != LD_OK) return result;
    uint64_t value;
    if (width == 1U) {
        value = place[0];
    } else if (width == 2U) {
        value = ld_elf_rel_read_u16(place);
    } else if (width == 4U) {
        value = ld_elf_rel_read_u32(place);
    } else if (width == 8U) {
        value = ld_elf_rel_read_u64(place);
    } else {
        return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                               relocation,
                               "unexpected x86-64 relocation width %zu",
                               width);
    }

    switch (relocation->type) {
        case LD_ELF_R_X86_64_32:
        case LD_ELF_R_X86_64_GOT32:
        case LD_ELF_R_X86_64_16:
        case LD_ELF_R_X86_64_8:
            *addend = (int64_t) value;
            break;
        default:
            *addend = ld_elf_rel_sign_extend(value, (unsigned) width * 8U);
            break;
    }
    return LD_OK;
}

static int ld_elf_rel_aarch64_instruction(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *target,
        const ld_elf_relocation_t *relocation, uint32_t *instruction) {
    const uint8_t *place;
    int result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                  &place);
    if (result != LD_OK) return result;
    *instruction = ld_elf_rel_read_u32(place);
    return LD_OK;
}

static int64_t ld_elf_rel_aarch64_adr(uint32_t instruction,
                                      bool page) {
    uint64_t encoded = ((uint64_t) instruction >> 29U) & 3U;
    encoded |= ((uint64_t) instruction >> 5U & 0x7ffffU) << 2U;
    if (page) encoded <<= 12U;
    return ld_elf_rel_sign_extend(encoded, page ? 33U : 21U);
}

static int ld_elf_rel_decode_aarch64(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *target,
        const ld_elf_relocation_t *relocation, int64_t *addend) {
    const uint8_t *place;
    uint32_t instruction;
    int result;
    uint64_t immediate;

    switch (relocation->type) {
        case LD_ELF_R_AARCH64_NONE:
            *addend = 0;
            return LD_OK;
        case LD_ELF_R_AARCH64_ABS64:
        case LD_ELF_R_AARCH64_PREL64:
            result = ld_elf_rel_place(ctx, object, target, relocation, 8U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(ld_elf_rel_read_u64(place), 64U);
            return LD_OK;
        case LD_ELF_R_AARCH64_ABS32:
            result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = (int64_t) ld_elf_rel_read_u32(place);
            return LD_OK;
        case LD_ELF_R_AARCH64_PREL32:
            result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(ld_elf_rel_read_u32(place), 32U);
            return LD_OK;
        case LD_ELF_R_AARCH64_ABS16:
            result = ld_elf_rel_place(ctx, object, target, relocation, 2U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = (int64_t) ld_elf_rel_read_u16(place);
            return LD_OK;
        case LD_ELF_R_AARCH64_PREL16:
            result = ld_elf_rel_place(ctx, object, target, relocation, 2U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(ld_elf_rel_read_u16(place), 16U);
            return LD_OK;
        default:
            break;
    }

    if (!ld_elf_relocation_supported(LD_ARCH_ARM64, relocation->type)) {
        return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                               relocation,
                               "relocation has no supported REL decoder");
    }
    result = ld_elf_rel_aarch64_instruction(ctx, object, target,
                                            relocation, &instruction);
    if (result != LD_OK) return result;

    switch (relocation->type) {
        case LD_ELF_R_AARCH64_MOVW_UABS_G0:
        case LD_ELF_R_AARCH64_MOVW_UABS_G0_NC:
        case LD_ELF_R_AARCH64_MOVW_UABS_G1:
        case LD_ELF_R_AARCH64_MOVW_UABS_G1_NC:
        case LD_ELF_R_AARCH64_MOVW_UABS_G2:
        case LD_ELF_R_AARCH64_MOVW_UABS_G2_NC:
        case LD_ELF_R_AARCH64_MOVW_UABS_G3: {
            uint32_t opcode = instruction & 0x7f800000U;
            if (opcode != 0x52800000U && opcode != 0x72800000U) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "expected MOVZ or MOVK instruction, found 0x%08x",
                        instruction);
            }
            unsigned shift;
            if (relocation->type == LD_ELF_R_AARCH64_MOVW_UABS_G0 ||
                relocation->type == LD_ELF_R_AARCH64_MOVW_UABS_G0_NC) {
                shift = 0U;
            } else if (relocation->type ==
                               LD_ELF_R_AARCH64_MOVW_UABS_G1 ||
                       relocation->type ==
                               LD_ELF_R_AARCH64_MOVW_UABS_G1_NC) {
                shift = 16U;
            } else if (relocation->type ==
                               LD_ELF_R_AARCH64_MOVW_UABS_G2 ||
                       relocation->type ==
                               LD_ELF_R_AARCH64_MOVW_UABS_G2_NC) {
                shift = 32U;
            } else {
                shift = 48U;
            }
            if (((instruction >> 21U) & 3U) != shift / 16U) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "MOVW shift does not match relocation group");
            }
            immediate = (instruction >> 5U) & 0xffffU;
            *addend = ld_elf_rel_sign_extend(immediate << shift, 64U);
            return LD_OK;
        }
        case LD_ELF_R_AARCH64_LD_PREL_LO19:
        case LD_ELF_R_AARCH64_GOT_LD_PREL19:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x3b000000U, 0x18000000U, "literal load");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 5U) & 0x7ffffU;
            *addend = ld_elf_rel_sign_extend(immediate << 2U, 21U);
            return LD_OK;
        case LD_ELF_R_AARCH64_ADR_PREL_LO21:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x9f000000U, 0x10000000U, "ADR");
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_aarch64_adr(instruction, false);
            return LD_OK;
        case LD_ELF_R_AARCH64_ADR_PREL_PG_HI21:
        case LD_ELF_R_AARCH64_ADR_GOT_PAGE:
        case LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21:
        case LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
        case LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x9f000000U, 0x90000000U, "ADRP");
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_aarch64_adr(instruction, true);
            return LD_OK;
        case LD_ELF_R_AARCH64_TSTBR14:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x7e000000U, 0x36000000U, "TBZ or TBNZ");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 5U) & 0x3fffU;
            *addend = ld_elf_rel_sign_extend(immediate << 2U, 16U);
            return LD_OK;
        case LD_ELF_R_AARCH64_CONDBR19:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0xff000010U, 0x54000000U, "conditional branch");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 5U) & 0x7ffffU;
            *addend = ld_elf_rel_sign_extend(immediate << 2U, 21U);
            return LD_OK;
        case LD_ELF_R_AARCH64_JUMP26:
        case LD_ELF_R_AARCH64_CALL26:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0xfc000000U,
                    relocation->type == LD_ELF_R_AARCH64_CALL26
                            ? 0x94000000U
                            : 0x14000000U,
                    relocation->type == LD_ELF_R_AARCH64_CALL26 ? "BL"
                                                                : "B");
            if (result != LD_OK) return result;
            immediate = instruction & 0x03ffffffU;
            *addend = ld_elf_rel_sign_extend(immediate << 2U, 28U);
            return LD_OK;
        case LD_ELF_R_AARCH64_ADD_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_TLSGD_ADD_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12:
        case LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSDESC_ADD_LO12: {
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x7f000000U, 0x11000000U, "ADD immediate");
            if (result != LD_OK) return result;
            bool high = relocation->type ==
                        LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12;
            bool encoded_high = (instruction & 0x00400000U) != 0U;
            if (high != encoded_high) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "ADD immediate shift does not match relocation");
            }
            immediate = ((uint64_t) instruction >> 10U) & 0xfffU;
            *addend = (int64_t) (high ? immediate << 12U : immediate);
            return LD_OK;
        }
        case LD_ELF_R_AARCH64_LD64_GOT_LO12_NC:
        case LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSDESC_LD64_LO12:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0xffc00000U, 0xf9400000U, "64-bit LDR unsigned offset");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 10U) & 0xfffU;
            *addend = (int64_t) (immediate << 3U);
            return LD_OK;
        case LD_ELF_R_AARCH64_LDST8_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_LDST16_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_LDST32_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_LDST64_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_LDST128_ABS_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
        case LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12:
        case LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC: {
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0x3b000000U, 0x39000000U,
                    "load/store unsigned offset");
            if (result != LD_OK) return result;
            unsigned scale = 0U;
            switch (relocation->type) {
                case LD_ELF_R_AARCH64_LDST16_ABS_LO12_NC:
                case LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12:
                case LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
                    scale = 1U;
                    break;
                case LD_ELF_R_AARCH64_LDST32_ABS_LO12_NC:
                case LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12:
                case LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
                    scale = 2U;
                    break;
                case LD_ELF_R_AARCH64_LDST64_ABS_LO12_NC:
                case LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12:
                case LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
                    scale = 3U;
                    break;
                case LD_ELF_R_AARCH64_LDST128_ABS_LO12_NC:
                case LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12:
                case LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC:
                    scale = 4U;
                    break;
                default:
                    break;
            }
            immediate = ((uint64_t) instruction >> 10U) & 0xfffU;
            *addend = (int64_t) (immediate << scale);
            return LD_OK;
        }
        case LD_ELF_R_AARCH64_TLSDESC_CALL:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0xfffffc1fU, 0xd63f0000U, "BLR");
            if (result != LD_OK) return result;
            *addend = 0;
            return LD_OK;
        default:
            return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                                   relocation,
                                   "relocation has no supported REL decoder");
    }
}

static bool ld_elf_rel_riscv_i_type(uint32_t instruction) {
    switch (instruction & 0x7fU) {
        case 0x03U:
        case 0x07U:
        case 0x13U:
        case 0x1bU:
        case 0x67U:
            return true;
        default:
            return false;
    }
}

static bool ld_elf_rel_riscv_s_type(uint32_t instruction) {
    uint32_t opcode = instruction & 0x7fU;
    return opcode == 0x23U || opcode == 0x27U;
}

static int64_t ld_elf_rel_riscv_i_imm(uint32_t instruction) {
    return ld_elf_rel_sign_extend((uint64_t) instruction >> 20U, 12U);
}

static int64_t ld_elf_rel_riscv_s_imm(uint32_t instruction) {
    uint64_t immediate = ((uint64_t) instruction >> 25U) << 5U;
    immediate |= ((uint64_t) instruction >> 7U) & 0x1fU;
    return ld_elf_rel_sign_extend(immediate, 12U);
}

static int ld_elf_rel_riscv_uleb(const uint8_t *place, size_t size,
                                 uint64_t *value) {
    uint64_t result = 0U;
    size_t limit = size < 10U ? size : 10U;
    for (size_t i = 0; i < limit; i++) {
        uint8_t payload = place[i] & 0x7fU;
        if (i == 9U && payload > 1U) return LD_INVALID_INPUT;
        result |= (uint64_t) payload << (i * 7U);
        if ((place[i] & 0x80U) == 0U) {
            *value = result;
            return LD_OK;
        }
    }
    return LD_INVALID_INPUT;
}

static int ld_elf_rel_decode_riscv(
        ld_elf_context_t *ctx, const ld_elf_object_t *object,
        const ld_elf_section_t *target,
        const ld_elf_relocation_t *relocation, int64_t *addend) {
    const uint8_t *place;
    uint32_t instruction;
    uint64_t immediate;
    int result;

    switch (relocation->type) {
        case LD_ELF_R_RISCV_NONE:
        case LD_ELF_R_RISCV_RELAX:
            *addend = 0;
            return LD_OK;
        case LD_ELF_R_RISCV_32:
            result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = (int64_t) ld_elf_rel_read_u32(place);
            return LD_OK;
        case LD_ELF_R_RISCV_64:
        case LD_ELF_R_RISCV_RELATIVE:
        case LD_ELF_R_RISCV_TLS_DTPREL64:
        case LD_ELF_R_RISCV_TLS_TPREL64:
            result = ld_elf_rel_place(ctx, object, target, relocation, 8U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(ld_elf_rel_read_u64(place), 64U);
            return LD_OK;
        case LD_ELF_R_RISCV_TLS_DTPREL32:
        case LD_ELF_R_RISCV_TLS_TPREL32:
        case LD_ELF_R_RISCV_32_PCREL:
            result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(ld_elf_rel_read_u32(place), 32U);
            return LD_OK;
        case LD_ELF_R_RISCV_SET8:
            result = ld_elf_rel_place(ctx, object, target, relocation, 1U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = place[0];
            return LD_OK;
        case LD_ELF_R_RISCV_SET16:
            result = ld_elf_rel_place(ctx, object, target, relocation, 2U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_read_u16(place);
            return LD_OK;
        case LD_ELF_R_RISCV_SET32:
            result = ld_elf_rel_place(ctx, object, target, relocation, 4U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_read_u32(place);
            return LD_OK;
        case LD_ELF_R_RISCV_SET6:
            result = ld_elf_rel_place(ctx, object, target, relocation, 1U,
                                      &place);
            if (result != LD_OK) return result;
            *addend = place[0] & 0x3fU;
            return LD_OK;
        case LD_ELF_R_RISCV_SET_ULEB128: {
            result = ld_elf_rel_place(ctx, object, target, relocation, 1U,
                                      &place);
            if (result != LD_OK) return result;
            size_t remaining = target->data_size - (size_t) relocation->offset;
            uint64_t value;
            if (ld_elf_rel_riscv_uleb(place, remaining, &value) != LD_OK ||
                value > INT64_MAX) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "invalid or out-of-range ULEB128 addend");
            }
            *addend = (int64_t) value;
            return LD_OK;
        }
        case LD_ELF_R_RISCV_ADD8:
        case LD_ELF_R_RISCV_ADD16:
        case LD_ELF_R_RISCV_ADD32:
        case LD_ELF_R_RISCV_ADD64:
        case LD_ELF_R_RISCV_SUB8:
        case LD_ELF_R_RISCV_SUB16:
        case LD_ELF_R_RISCV_SUB32:
        case LD_ELF_R_RISCV_SUB64:
        case LD_ELF_R_RISCV_SUB6:
        case LD_ELF_R_RISCV_SUB_ULEB128:
        case LD_ELF_R_RISCV_ALIGN:
            return ld_elf_rel_fail(
                    ctx, LD_UNSUPPORTED, object, target, relocation,
                    "the relocation field is an independent operand and "
                    "does not contain a reversible implicit addend");
        default:
            break;
    }

    if (!ld_elf_relocation_supported(LD_ARCH_RISCV64,
                                     relocation->type)) {
        return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                               relocation,
                               "relocation has no supported REL decoder");
    }

    size_t width = relocation->type == LD_ELF_R_RISCV_RVC_BRANCH ||
                                   relocation->type ==
                                           LD_ELF_R_RISCV_RVC_JUMP
                           ? 2U
                           : (relocation->type == LD_ELF_R_RISCV_CALL ||
                                              relocation->type ==
                                                      LD_ELF_R_RISCV_CALL_PLT
                                      ? 8U
                                      : 4U);
    result = ld_elf_rel_place(ctx, object, target, relocation, width, &place);
    if (result != LD_OK) return result;

    if (relocation->type == LD_ELF_R_RISCV_RVC_BRANCH) {
        uint16_t compressed = ld_elf_rel_read_u16(place);
        uint16_t funct3 = compressed >> 13U;
        if ((compressed & 3U) != 1U || (funct3 != 6U && funct3 != 7U)) {
            return ld_elf_rel_fail(
                    ctx, LD_INVALID_INPUT, object, target, relocation,
                    "expected C.BEQZ or C.BNEZ instruction, found 0x%04x",
                    compressed);
        }
        immediate = ((uint64_t) compressed >> 4U) & 0x100U;
        immediate |= ((uint64_t) compressed >> 7U) & 0x18U;
        immediate |= ((uint64_t) compressed << 1U) & 0xc0U;
        immediate |= ((uint64_t) compressed >> 2U) & 0x06U;
        immediate |= ((uint64_t) compressed << 3U) & 0x20U;
        *addend = ld_elf_rel_sign_extend(immediate, 9U);
        return LD_OK;
    }
    if (relocation->type == LD_ELF_R_RISCV_RVC_JUMP) {
        uint16_t compressed = ld_elf_rel_read_u16(place);
        if ((compressed & 0xe003U) != 0xa001U) {
            return ld_elf_rel_fail(
                    ctx, LD_INVALID_INPUT, object, target, relocation,
                    "expected C.J instruction, found 0x%04x", compressed);
        }
        immediate = ((uint64_t) compressed >> 1U) & 0x800U;
        immediate |= ((uint64_t) compressed >> 7U) & 0x10U;
        immediate |= ((uint64_t) compressed >> 1U) & 0x300U;
        immediate |= ((uint64_t) compressed << 2U) & 0x400U;
        immediate |= ((uint64_t) compressed >> 1U) & 0x40U;
        immediate |= ((uint64_t) compressed << 1U) & 0x80U;
        immediate |= ((uint64_t) compressed >> 2U) & 0x0eU;
        immediate |= ((uint64_t) compressed << 3U) & 0x20U;
        *addend = ld_elf_rel_sign_extend(immediate, 12U);
        return LD_OK;
    }

    instruction = ld_elf_rel_read_u32(place);
    switch (relocation->type) {
        case LD_ELF_R_RISCV_BRANCH:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction, 0x7fU,
                    0x63U, "BRANCH");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 19U) & 0x1000U;
            immediate |= ((uint64_t) instruction >> 20U) & 0x7e0U;
            immediate |= ((uint64_t) instruction >> 7U) & 0x1eU;
            immediate |= ((uint64_t) instruction << 4U) & 0x800U;
            *addend = ld_elf_rel_sign_extend(immediate, 13U);
            return LD_OK;
        case LD_ELF_R_RISCV_JAL:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction, 0x7fU,
                    0x6fU, "JAL");
            if (result != LD_OK) return result;
            immediate = ((uint64_t) instruction >> 11U) & 0x100000U;
            immediate |= ((uint64_t) instruction >> 20U) & 0x7feU;
            immediate |= ((uint64_t) instruction >> 9U) & 0x800U;
            immediate |= instruction & 0xff000U;
            *addend = ld_elf_rel_sign_extend(immediate, 21U);
            return LD_OK;
        case LD_ELF_R_RISCV_CALL:
        case LD_ELF_R_RISCV_CALL_PLT: {
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction, 0x7fU,
                    0x17U, "AUIPC");
            if (result != LD_OK) return result;
            uint32_t low = ld_elf_rel_read_u32(place + 4U);
            uint32_t rd = (instruction >> 7U) & 0x1fU;
            uint32_t low_rd = (low >> 7U) & 0x1fU;
            uint32_t low_rs1 = (low >> 15U) & 0x1fU;
            bool jalr = (low & 0x707fU) == 0x0067U && low_rs1 == rd;
            bool nature_address =
                    relocation->type == LD_ELF_R_RISCV_CALL &&
                    (low & 0x707fU) == 0x0013U && low_rd == rd &&
                    low_rs1 == rd;
            if (!jalr && !nature_address) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "expected AUIPC followed by JALR or ADDI, found "
                        "0x%08x",
                        low);
            }
            int64_t high = ld_elf_rel_sign_extend(instruction & 0xfffff000U,
                                                  32U);
            int64_t low_immediate = ld_elf_rel_riscv_i_imm(low);
            *addend = high + low_immediate;
            return LD_OK;
        }
        case LD_ELF_R_RISCV_GOT_HI20:
        case LD_ELF_R_RISCV_TLS_GOT_HI20:
        case LD_ELF_R_RISCV_TLS_GD_HI20:
        case LD_ELF_R_RISCV_PCREL_HI20:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction, 0x7fU,
                    0x17U, "AUIPC");
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(instruction & 0xfffff000U, 32U);
            return LD_OK;
        case LD_ELF_R_RISCV_HI20:
        case LD_ELF_R_RISCV_TPREL_HI20:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction, 0x7fU,
                    0x37U, "LUI");
            if (result != LD_OK) return result;
            *addend = ld_elf_rel_sign_extend(instruction & 0xfffff000U, 32U);
            return LD_OK;
        case LD_ELF_R_RISCV_PCREL_LO12_I:
        case LD_ELF_R_RISCV_LO12_I:
        case LD_ELF_R_RISCV_TPREL_LO12_I:
            if (!ld_elf_rel_riscv_i_type(instruction)) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "expected I-type instruction, found 0x%08x",
                        instruction);
            }
            *addend = ld_elf_rel_riscv_i_imm(instruction);
            return LD_OK;
        case LD_ELF_R_RISCV_PCREL_LO12_S:
        case LD_ELF_R_RISCV_LO12_S:
        case LD_ELF_R_RISCV_TPREL_LO12_S:
            if (!ld_elf_rel_riscv_s_type(instruction)) {
                return ld_elf_rel_fail(
                        ctx, LD_INVALID_INPUT, object, target, relocation,
                        "expected S-type instruction, found 0x%08x",
                        instruction);
            }
            *addend = ld_elf_rel_riscv_s_imm(instruction);
            return LD_OK;
        case LD_ELF_R_RISCV_TPREL_ADD:
            result = ld_elf_rel_require_u32(
                    ctx, object, target, relocation, instruction,
                    0xfe00707fU, 0x00000033U, "ADD");
            if (result != LD_OK) return result;
            *addend = 0;
            return LD_OK;
        default:
            return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target,
                                   relocation,
                                   "relocation has no supported REL decoder");
    }
}

int ld_elf_rel_decode_addend(ld_elf_context_t *ctx,
                             const ld_elf_object_t *object,
                             const ld_elf_section_t *target,
                             const ld_elf_relocation_t *relocation,
                             int64_t *addend) {
    if (!addend) {
        return ld_elf_rel_fail(ctx, LD_INVALID_ARGUMENT, object, target,
                               relocation, "missing result pointer");
    }
    ld_arch_t arch = ld_elf_rel_arch(object);
    if (arch == LD_ARCH_AMD64) {
        return ld_elf_rel_decode_x86(ctx, object, target, relocation,
                                     addend);
    }
    if (arch == LD_ARCH_ARM64) {
        return ld_elf_rel_decode_aarch64(ctx, object, target, relocation,
                                         addend);
    }
    if (arch == LD_ARCH_RISCV64) {
        return ld_elf_rel_decode_riscv(ctx, object, target, relocation,
                                       addend);
    }
    return ld_elf_rel_fail(ctx, LD_UNSUPPORTED, object, target, relocation,
                           "unsupported ELF machine %u",
                           object ? object->header.e_machine : 0U);
}

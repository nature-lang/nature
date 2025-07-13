#ifndef NATURE_BINARY_ARCH_RISCV64_H
#define NATURE_BINARY_ARCH_RISCV64_H

#include "src/binary/encoding/riscv64/asm.h"
#include "src/binary/encoding/riscv64/opcode.h"
#include "src/binary/linker.h"
#include "src/debug/debug_asm.h"

#include <stdlib.h>

#define RISCV64_ELF_START_ADDR 0x10000
#define RISCV64_ELF_PAGE_SIZE 0x1000

typedef struct {
    uint8_t *data; // 汇编指令
    uint8_t data_count; // 汇编指令长度, 单位 byte(riscv64 默认长度 4byte)
    uint64_t *offset; // 指令的位置
    riscv64_asm_inst_t *operation; // 原始指令,指令改写与二次扫描时使用
    string rel_symbol; // 使用的符号,二次扫描时用于判断是否需要重定位，目前都只适用于 label
    riscv64_asm_operand_t *rel_operand; // 引用自 asm_operations
    uint64_t sym_index; // 指令引用的符号在符号表的索引，如果指令发生了 slot 变更，则相应的符号的 value 同样需要变更
    void *rel; // elf_rela
} riscv64_build_temp_t;

static inline uint64_t riscv64_create_plt_entry(elf_context_t *ctx, uint64_t got_offset, sym_attr_t *attr) {
    section_t *plt = ctx->plt;

    if (plt->data_count == 0) {
        section_ptr_add(plt, 32);
    }
    uint64_t plt_offset = plt->data_count;

    uint8_t *p = section_ptr_add(plt, 16);
    write64le(p, got_offset);

    return plt_offset;
}

static inline int riscv64_gotplt_entry_type(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_RISCV_NONE:
        case R_RISCV_ALIGN:
        case R_RISCV_RELAX:
        case R_RISCV_RVC_BRANCH:
        case R_RISCV_RVC_JUMP:
        case R_RISCV_JUMP_SLOT:
        case R_RISCV_TPREL_HI20:
        case R_RISCV_TPREL_LO12_I:
        case R_RISCV_TPREL_LO12_S:
        case R_RISCV_TPREL_ADD:
        case R_RISCV_ADD16:
        case R_RISCV_SUB6:
        case R_RISCV_SUB8:
        case R_RISCV_SUB16:
        case R_RISCV_SET6:
        case R_RISCV_SET8:
        case R_RISCV_SET16:
        case R_RISCV_SET_ULEB128:
        case R_RISCV_SUB_ULEB128:
        case R_RISCV_TLSDESC_HI20:
        case R_RISCV_TLSDESC_LOAD_LO12:
        case R_RISCV_TLSDESC_ADD_LO12:
        case R_RISCV_TLSDESC_CALL:
            return NO_GOTPLT_ENTRY;

        case R_RISCV_BRANCH:
        case R_RISCV_CALL:
        case R_RISCV_PCREL_HI20:
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_32_PCREL:
        case R_RISCV_ADD32:
        case R_RISCV_ADD64:
        case R_RISCV_SUB32:
        case R_RISCV_SUB64:
        case R_RISCV_32:
        case R_RISCV_64:
        case R_RISCV_JAL:
        case R_RISCV_CALL_PLT:
            return AUTO_GOTPLT_ENTRY;

        case R_RISCV_GOT_HI20:
        case R_RISCV_TLS_GOT_HI20:
        case R_RISCV_TLS_GD_HI20:
            return ALWAYS_GOTPLT_ENTRY;
    }
    return -1;
}

static inline int8_t riscv64_is_code_relocate(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_RISCV_BRANCH:
        case R_RISCV_CALL:
        case R_RISCV_JAL:
            return 1;

        case R_RISCV_GOT_HI20:
        case R_RISCV_PCREL_HI20:
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_32_PCREL:
        case R_RISCV_SET6:
        case R_RISCV_SET8:
        case R_RISCV_SET16:
        case R_RISCV_SUB6:
        case R_RISCV_ADD16:
        case R_RISCV_ADD32:
        case R_RISCV_ADD64:
        case R_RISCV_SUB8:
        case R_RISCV_SUB16:
        case R_RISCV_SUB32:
        case R_RISCV_SUB64:
        case R_RISCV_32:
        case R_RISCV_64:
            return 0;

        case R_RISCV_CALL_PLT:
            return 1;
    }
    return -1;
}

static inline bool riscv64_is_imm_operand(riscv64_asm_operand_t *operand) {
    return operand->type == RISCV64_ASM_OPERAND_IMMEDIATE;
}

static inline bool riscv64_is_call_op(riscv64_asm_inst_t *operation) {
    return operation->raw_opcode == RV_CALL || operation->raw_opcode == RV_JALR;
}

static inline bool riscv64_is_branch_op(riscv64_asm_inst_t *operation) {
    return operation->raw_opcode >= RV_J && operation->raw_opcode <= RV_BGEU;
}

static inline bool riscv64_is_bxx_op(riscv64_asm_inst_t *operation) {
    return operation->raw_opcode >= RV_BEQ && operation->raw_opcode <= RV_BGEU;
}

static inline riscv64_asm_operand_t *riscv64_extract_symbol_operand(riscv64_asm_inst_t *operation) {
    for (int k = 0; k < operation->count; k++) {
        if (operation->operands[k] && operation->operands[k]->type == RISCV64_ASM_OPERAND_SYMBOL) {
            return operation->operands[k];
        }
    }
    return NULL;
}

static inline riscv64_build_temp_t *riscv64_build_temp_new(riscv64_asm_inst_t *operation) {
    riscv64_build_temp_t *temp = NEW(riscv64_build_temp_t);
    temp->data = 0;
    temp->data_count = 0;
    temp->offset = NEW(uint64_t);
    temp->operation = operation;
    temp->rel_operand = NULL;
    temp->rel_symbol = NULL;
    temp->sym_index = 0;
    temp->rel = NULL;
    return temp;
}

static inline void
elf_riscv64_relocate(elf_context_t *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, uint64_t addr, uint64_t val) {
    uint64_t off64;
    uint32_t off32;
    int sym_index = ELF64_R_SYM(rel->r_info);
    Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
    char *sym_name = (char *) ctx->symtab_section->link->data + sym->st_name;
    //    log_debug("[elf_riscv64_relocate] sym %s, type %d", sym_name, type);

    switch (type) {
        case R_RISCV_ALIGN:
        case R_RISCV_RELAX:
            return;

        case R_RISCV_BRANCH:
            off64 = val - addr;
            if ((off64 + (1 << 12)) & ~(uint64_t) 0x1ffe) {
                assertf(false, "R_RISCV_BRANCH relocation failed (val=%lx, addr=%lx)", (long) val, (long) addr);
            }
            off32 = off64 >> 1;
            write32le(ptr, (read32le(ptr) & ~0xfe000f80) | ((off32 & 0x800) << 20) | ((off32 & 0x3f0) << 21) | ((off32 & 0x00f) << 8) | ((off32 & 0x400) >> 3));
            break;
        case R_RISCV_JAL:
            off64 = val - addr;
            if ((off64 + (1 << 21)) & ~(((uint64_t) 1 << 22) - 2)) {
                assertf(false, "R_RISCV_JAL relocation failed (val=%lx, addr=%lx)", (long) val, (long) addr);
            }
            off32 = off64;
            write32le(ptr, (read32le(ptr) & 0xfff) | (((off32 >> 12) & 0xff) << 12) | (((off32 >> 11) & 1) << 20) | (((off32 >> 1) & 0x3ff) << 21) | (((off32 >> 20) & 1) << 31));
            break;

        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
            write32le(ptr, (read32le(ptr) & 0xfff) | ((val - addr + 0x800) & ~0xfff));
            write32le(ptr + 4, (read32le(ptr + 4) & 0xfffff) | (((val - addr) & 0xfff) << 20));
            break;

        case R_RISCV_PCREL_HI20:
            off64 = (int64_t) (val - addr + 0x800) >> 12;
            if ((off64 + ((uint64_t) 1 << 20)) >> 21) {
                assertf(false, "R_RISCV_PCREL_HI20 relocation failed: off=%lx", (long) off64);
            }
            write32le(ptr, (read32le(ptr) & 0xfff) | ((off64 & 0xfffff) << 12));
            ctx->last_hi.addr = addr;
            ctx->last_hi.val = val;
            break;

        case R_RISCV_GOT_HI20:
            val = ctx->got->sh_addr + elf_get_sym_attr(ctx, sym_index, 0)->got_offset;
            off64 = (int64_t) (val - addr + 0x800) >> 12;
            if ((off64 + ((uint64_t) 1 << 20)) >> 21) {
                assertf(false, "R_RISCV_GOT_HI20 relocation failed");
            }
            ctx->last_hi.addr = addr;
            ctx->last_hi.val = val;
            write32le(ptr, (read32le(ptr) & 0xfff) | ((off64 & 0xfffff) << 12));
            break;

        case R_RISCV_PCREL_LO12_I:
            if (val != ctx->last_hi.addr) {
                assertf(false, "unsupported hi/lo pcrel reloc scheme");
            }
            val = ctx->last_hi.val;
            addr = ctx->last_hi.addr;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (((val - addr) & 0xfff) << 20));
            break;

        case R_RISCV_PCREL_LO12_S:
            if (val != ctx->last_hi.addr) {
                assertf(false, "unsupported hi/lo pcrel reloc scheme");
            }
            val = ctx->last_hi.val;
            addr = ctx->last_hi.addr;
            off32 = val - addr;
            write32le(ptr, (read32le(ptr) & ~0xfe000f80) | ((off32 & 0xfe0) << 20) | ((off32 & 0x01f) << 7));
            break;

        case R_RISCV_RVC_BRANCH:
            off64 = (val - addr);
            if ((off64 + (1 << 8)) & ~(uint64_t) 0x1fe) {
                assertf(false, "R_RISCV_RVC_BRANCH relocation failed (val=%lx, addr=%lx)", (long) val, (long) addr);
            }
            off32 = off64;
            write16le(ptr, (read16le(ptr) & 0xe383) | (((off32 >> 5) & 1) << 2) | (((off32 >> 1) & 3) << 3) | (((off32 >> 6) & 3) << 5) | (((off32 >> 3) & 3) << 10) | (((off32 >> 8) & 1) << 12));
            break;

        case R_RISCV_RVC_JUMP:
            off64 = (val - addr);
            if ((off64 + (1 << 11)) & ~(uint64_t) 0xffe) {
                assertf(false, "R_RISCV_RVC_JUMP relocation failed (val=%lx, addr=%lx)", (long) val, (long) addr);
            }
            off32 = off64;
            write16le(ptr, (read16le(ptr) & 0xe003) | (((off32 >> 5) & 1) << 2) | (((off32 >> 1) & 7) << 3) | (((off32 >> 7) & 1) << 6) | (((off32 >> 6) & 1) << 7) | (((off32 >> 10) & 1) << 8) | (((off32 >> 8) & 3) << 9) | (((off32 >> 4) & 1) << 11) | (((off32 >> 11) & 1) << 12));
            break;

        case R_RISCV_32:
            add32le(ptr, val);
            break;

        case R_RISCV_64:
        case R_RISCV_JUMP_SLOT:
            add64le(ptr, val);
            break;

        case R_RISCV_ADD64:
            write64le(ptr, read64le(ptr) + val);
            break;

        case R_RISCV_ADD32:
            write32le(ptr, read32le(ptr) + val);
            break;

        case R_RISCV_SUB64:
            write64le(ptr, read64le(ptr) - val);
            break;

        case R_RISCV_SUB32:
            write32le(ptr, read32le(ptr) - val);
            break;

        case R_RISCV_ADD16:
            write16le(ptr, read16le(ptr) + val);
            break;

        case R_RISCV_SUB8:
            *ptr -= val;
            break;

        case R_RISCV_SUB16:
            write16le(ptr, read16le(ptr) - val);
            break;

        case R_RISCV_SET6:
            *ptr = (*ptr & ~0x3f) | (val & 0x3f);
            break;

        case R_RISCV_SET8:
            *ptr = (*ptr & ~0xff) | (val & 0xff);
            break;

        case R_RISCV_SET16:
            *ptr = (*ptr & ~0xffff) | (val & 0xffff);
            break;

        case R_RISCV_SUB6:
            *ptr = (*ptr & ~0x3f) | ((*ptr - val) & 0x3f);
            break;

        case R_RISCV_32_PCREL:
            add32le(ptr, val - addr);
            break;

        case R_RISCV_COPY:
            break;

        case R_RISCV_RELATIVE:
            write64le(ptr, ctx->text_section->sh_addr + rel->r_addend);
            break;

        // 保留原有的其他重定位类型
        case R_RISCV_HI20: {
            uint32_t hi20 = (val + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_LO12_I: {
            uint32_t lo12 = val & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_LO12_S: {
            uint32_t lo12 = val & 0xfff;
            write32le(ptr, (read32le(ptr) & 0x1fff07f) | ((lo12 & 0x1f) << 7) | ((lo12 & 0xfe0) << 20));
            break;
        }
        case R_RISCV_TPREL_HI20: {
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            section_t *s = SEC_TACK(sym->st_shndx);
            uint64_t tprel = val - s->sh_addr; // 计算相对于TLS段的偏移
            uint32_t hi20 = (tprel + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_TPREL_LO12_I: {
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            section_t *s = SEC_TACK(sym->st_shndx);
            uint64_t tprel = val - s->sh_addr; // 计算相对于TLS段的偏移
            uint32_t lo12 = tprel & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_TPREL_LO12_S: {
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            section_t *s = SEC_TACK(sym->st_shndx);
            uint64_t tprel = val - s->sh_addr; // 计算相对于TLS段的偏移
            uint32_t lo12 = tprel & 0xfff;
            write32le(ptr, (read32le(ptr) & 0x1fff07f) |
                                   ((lo12 & 0x1f) << 7) | ((lo12 & 0xfe0) << 20));
            break;
        }
        case R_RISCV_TPREL_ADD:
            break;

        // 新增的重定位类型
        case R_RISCV_PLT32: {
            // PLT 32位相对重定位
            uint64_t plt_addr = ctx->plt->sh_addr + elf_get_sym_attr(ctx, sym_index, 0)->plt_offset;
            uint64_t pc_rel = plt_addr - addr;
            write32le(ptr, pc_rel);
            break;
        }
        case R_RISCV_SET_ULEB128: {
            // 设置 ULEB128 编码的值
            uint8_t *p = ptr;
            uint64_t value = val;
            while (value >= 0x80) {
                *p++ = (value & 0x7f) | 0x80;
                value >>= 7;
            }
            *p = value & 0x7f;
            break;
        }
        case R_RISCV_SUB_ULEB128: {
            // 从 ULEB128 编码的值中减去指定值
            // 首先解码当前的 ULEB128 值
            uint8_t *p = ptr;
            uint64_t current = 0;
            int shift = 0;
            while (*p & 0x80) {
                current |= (uint64_t) (*p & 0x7f) << shift;
                shift += 7;
                p++;
            }
            current |= (uint64_t) (*p & 0x7f) << shift;

            // 执行减法操作
            uint64_t result = current - val;

            // 重新编码为 ULEB128
            p = ptr;
            while (result >= 0x80) {
                *p++ = (result & 0x7f) | 0x80;
                result >>= 7;
            }
            *p = result & 0x7f;
            break;
        }
        case R_RISCV_TLSDESC_HI20: {
            // TLS 描述符高 20 位
            // 这通常用于 TLS 描述符的高位地址计算
            uint32_t hi20 = (val + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_TLSDESC_LOAD_LO12: {
            // TLS 描述符加载低 12 位 (I-type)
            uint32_t lo12 = val & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_TLSDESC_ADD_LO12: {
            // TLS 描述符加法低 12 位 (S-type)
            uint32_t lo12 = val & 0xfff;
            write32le(ptr, (read32le(ptr) & 0x1fff07f) | ((lo12 & 0x1f) << 7) | ((lo12 & 0xfe0) << 20));
            break;
        }
        case R_RISCV_TLSDESC_CALL:
            // TLS 描述符调用 - 通常不需要运行时处理
            // 这个重定位类型主要在链接时处理
            break;

        default:
            assertf(false, "Unhandled relocation type %d at %lx", type, addr);
            break;
    }
}


static inline void
riscv64_rewrite_rel_symbol(riscv64_asm_inst_t *operation, riscv64_asm_operand_t *operand, int64_t rel_diff) {
    int addend = 0;

    // 如果不是符号操作数,直接返回
    if (operand->type != RISCV64_ASM_OPERAND_SYMBOL) {
        return;
    }

    operand->immediate = rel_diff - addend; // riscv64 具有固定长度 addend 4byte
    operand->type = RISCV64_ASM_OPERAND_IMMEDIATE;
}

static inline void elf_riscv64_operation_encodings(elf_context_t *ctx, module_t *m) {
    if (m->closures->count == 0) {
        return;
    }

    slice_t *build_temps = slice_new();
    uint64_t section_offset = 0; // text section slot

    // 第一遍扫描
    for (int i = 0; i < m->closures->count; ++i) {
        closure_t *c = m->closures->take[i];

        uint64_t fn_offset = 0;

        for (int j = 0; j < c->asm_operations->count; ++j) {
            riscv64_asm_inst_t *operation = c->asm_operations->take[j];
            riscv64_build_temp_t *temp = riscv64_build_temp_new(operation);
            slice_push(build_temps, temp);
            slice_push(c->asm_build_temps, temp);

            *temp->offset = section_offset;

            // 处理标签符号定义
            if (operation->raw_opcode == RV_LABEL) {
                riscv64_asm_operand_t *operand = operation->operands[0];

                // 添加到符号表
                Elf64_Sym sym = {
                        .st_shndx = ctx->text_section->sh_index,
                        .st_size = 0,
                        .st_info = ELF64_ST_INFO(!operand->symbol.is_local, STT_FUNC),
                        .st_other = 0,
                        .st_value = *temp->offset,
                };
                temp->sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, operand->symbol.name);
                continue;
            }

            // 处理符号引用
            riscv64_asm_operand_t *rel_operand = riscv64_extract_symbol_operand(operation);
            char *call_target = NULL;
            if (rel_operand != NULL) {
                assert(rel_operand->type == RISCV64_ASM_OPERAND_SYMBOL);
                call_target = rel_operand->symbol.name;

                // 判断是否为分支/调用指令
                if (riscv64_is_call_op(operation) || riscv64_is_branch_op(operation)) {
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, rel_operand->symbol.name);
                    if (riscv64_is_branch_op(operation) && sym_index > 0) {
                        // 已存在的符号，计算相对偏移并进行符号改写
                        Elf64_Sym sym = ((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
                        int64_t rel_diff = (int64_t) sym.st_value - (int64_t) section_offset;

                        if (riscv64_is_bxx_op(operation)) {
                            assert(rel_diff >= -4096);
                            assert(rel_diff <= 4094);
                        }

                        riscv64_rewrite_rel_symbol(operation, rel_operand, rel_diff);
                    } else {
                        // 未知符号，暂时不做处理，依旧使用 symbol 参数
                        temp->rel_operand = rel_operand;
                        temp->rel_symbol = rel_operand->symbol.name;
                    }
                } else {
                    // 添加重定位项
                    int reloc_type = 0;
                    int st_type = STT_OBJECT;

                    if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_BRANCH) {
                        reloc_type = R_RISCV_BRANCH;
                    } else if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_CALL) {
                        reloc_type = R_RISCV_CALL;
                    } else if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_JAL) {
                        reloc_type = R_RISCV_JAL;
                    } else if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_TPREL_HI20) {
                        reloc_type = R_RISCV_TPREL_HI20;
                        st_type = STT_TLS;
                    } else if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_TPREL_LO12_I) {
                        reloc_type = R_RISCV_TPREL_LO12_I;
                        st_type = STT_TLS;
                    } else if (rel_operand->symbol.reloc_type == ASM_RISCV64_RELOC_TPREL_LO12_S) {
                        reloc_type = R_RISCV_TPREL_LO12_S;
                        st_type = STT_TLS;
                    } else {
                        assertf(false, "unknown reloc type");
                    }

                    // 数据指令访问添加重定位项即可
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, rel_operand->symbol.name);
                    if (sym_index == 0) {
                        // 添加未定义符号
                        Elf64_Sym sym = {
                                .st_shndx = 0,
                                .st_size = 0,
                                .st_info = ELF64_ST_INFO(STB_GLOBAL, st_type),
                                .st_other = 0,
                                .st_value = 0,
                        };
                        sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, rel_operand->symbol.name);
                    }
                    assert(sym_index > 0);

                    // 生成重定位信息
                    temp->sym_index = sym_index;

                    riscv64_asm_inst_encoding(operation, c);
                    temp->data = operation->opcode_data;
                    temp->data_count = operation->opcode_count;
                    section_offset += temp->data_count;
                    fn_offset += temp->data_count;

                    temp->rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                                 *temp->offset, reloc_type,
                                                 (int) sym_index, 0);
                    continue;
                }
            }

            // 编码指令
            riscv64_asm_inst_encoding(operation, c);

            temp->data = operation->opcode_data;
            temp->data_count = operation->opcode_count;

            section_offset += temp->data_count;
            fn_offset += temp->data_count;

            if (operation->raw_opcode == RV_CALL || operation->raw_opcode == RV_JALR) {
                do {
                    // 跳过 linear 阶段大量生成的 rt_call
                    if (call_target && is_rtcall(call_target)) {
                        break;
                    }

                    caller_t caller = {
                            .data = c,
                            .offset = fn_offset,
                            .line = operation->line,
                            .column = operation->column,
                    };
                    if (call_target) {
                        caller.target_name_offset = strtable_put(call_target);
                    }

                    ct_list_push(ct_caller_list, &caller);
                } while (0);
            }
        }
    }

    // 第二遍扫描处理重定位
    for (int i = 0; i < build_temps->count; ++i) {
        riscv64_build_temp_t *temp = build_temps->take[i];
        if (!temp->rel_symbol) {
            continue;
        }

        uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, temp->rel_symbol);
        if (sym_index == 0) {
            // 添加未定义符号
            Elf64_Sym sym = {
                    .st_shndx = 0,
                    .st_size = 0,
                    .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
                    .st_other = 0,
                    .st_value = 0,
            };
            sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, temp->rel_symbol);
        }

        Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
        char *name = (char *) ctx->symtab_section->link->data + sym->st_name;
        assert(sym_index > 0);

        // rv_call 需要拆分为 auipc + jalr，重定位较为困难
        if (riscv64_is_branch_op(temp->operation) && sym->st_value > 0) {
            // 内部符号，计算相对偏移
            int64_t rel_diff = (int64_t) sym->st_value - *temp->offset;

            if (riscv64_is_bxx_op(temp->operation)) {
                assert(rel_diff >= -4096);
                assert(rel_diff <= 4094);
            }

            riscv64_rewrite_rel_symbol(temp->operation, temp->rel_operand, rel_diff);

            riscv64_asm_inst_encoding(temp->operation, NULL);
            temp->data = temp->operation->opcode_data;
            temp->data_count = temp->operation->opcode_count;
        } else {
            int reloc_type;

            switch (temp->operation->raw_opcode) {
                case RV_J:
                    reloc_type = R_RISCV_JAL;
                    break;
                case RV_CALL:
                    reloc_type = R_RISCV_CALL;
                    break;
                default:
                    // 处理分支指令
                    reloc_type = R_RISCV_BRANCH;
                    break;
            };
            assert(sym_index > 0);

            // 确认是外部符号，进行重定位处理
            temp->rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                         *temp->offset, reloc_type,
                                         (int) sym_index, 0);
        }
    }

    // 生成最终的代码段数据
    for (int i = 0; i < m->closures->count; ++i) {
        closure_t *c = m->closures->take[i];
        c->text_count = 0;
        for (int j = 0; j < c->asm_build_temps->count; ++j) {
            riscv64_build_temp_t *temp = c->asm_build_temps->take[j];
            if (temp->data_count == 0) {
                continue;
            }

            elf_put_data(ctx->text_section, temp->data, temp->data_count);
            c->text_count += temp->data_count;
        }
    }
}

#endif // NATURE_BINARY_ARCH_RISCV64_H
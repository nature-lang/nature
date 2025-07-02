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
        // PLT0 entry (32 bytes)
        uint8_t *p = section_ptr_add(plt, 32);

        // auipc t2, %pcrel_hi(.got.plt)
        write32le(p, 0x00000397); // auipc t2, 0

        // ld t2, %pcrel_lo(.got.plt)(t2)
        write32le(p + 4, 0x0003b383); // ld t2, 0(t2)

        // auipc t1, %pcrel_hi(.got.plt+8)
        write32le(p + 8, 0x00000317); // auipc t1, 0

        // ld t1, %pcrel_lo(.got.plt+8)(t1)
        write32le(p + 12, 0x00033303); // ld t1, 0(t1)

        // jalr t1
        write32le(p + 16, 0x00030067); // jalr t1

        // nop
        write32le(p + 20, 0x00000013); // nop
        write32le(p + 24, 0x00000013); // nop
        write32le(p + 28, 0x00000013); // nop
    }

    // Create PLT entry (16 bytes)
    uint64_t plt_offset = plt->data_count;
    uint8_t *p = section_ptr_add(plt, 16);

    // auipc t2, %pcrel_hi(got_entry)
    write32le(p, 0x00000397); // auipc t2, 0

    // ld t2, %pcrel_lo(got_entry)(t2)
    write32le(p + 4, 0x0003b383); // ld t2, 0(t2)

    // jalr t1, t2
    write32le(p + 8, 0x000380e7); // jalr t1, t2

    // nop
    write32le(p + 12, 0x00000013); // nop

    return plt_offset;
}

static inline int riscv64_gotplt_entry_type(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_RISCV_NONE:
        case R_RISCV_32:
        case R_RISCV_64:
        case R_RISCV_RELAX:
        case R_RISCV_RVC_BRANCH:
        case R_RISCV_RVC_JUMP:
        case R_RISCV_32_PCREL:
        case R_RISCV_PCREL_HI20:
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_TPREL_HI20:
        case R_RISCV_TPREL_LO12_I:
        case R_RISCV_TPREL_ADD:
        case R_RISCV_ADD16:
        case R_RISCV_ADD32:
        case R_RISCV_ADD64:
        case R_RISCV_SUB6:
        case R_RISCV_SUB8:
        case R_RISCV_SUB16:
        case R_RISCV_SUB32:
        case R_RISCV_SUB64:
        case R_RISCV_SET6:
        case R_RISCV_SET8:
        case R_RISCV_SET16:
        case R_RISCV_SET_ULEB128:
        case R_RISCV_SUB_ULEB128:
            return NO_GOTPLT_ENTRY;

        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
        case R_RISCV_JAL:
        case R_RISCV_BRANCH:
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
        case R_RISCV_32:
        case R_RISCV_64:
        case R_RISCV_PCREL_HI20:
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_HI20:
        case R_RISCV_LO12_I:
        case R_RISCV_LO12_S:
        case R_RISCV_GOT_HI20:
        case R_RISCV_COPY:
        case R_RISCV_RELATIVE:
            return 0;

        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
        case R_RISCV_JAL:
        case R_RISCV_BRANCH:
        case R_RISCV_JUMP_SLOT:
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
    int sym_index = ELF64_R_SYM(rel->r_info);

    switch (type) {
        case R_RISCV_64:
            add64le(ptr, val);
            break;
        case R_RISCV_32:
            add32le(ptr, val);
            break;
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
        case R_RISCV_PCREL_HI20: {
            uint64_t pc_rel = val - addr;
            uint32_t hi20 = (pc_rel + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_PCREL_LO12_I: {
            uint64_t pc_rel = val - addr;
            uint32_t lo12 = pc_rel & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_PCREL_LO12_S: {
            uint64_t pc_rel = val - addr;
            uint32_t lo12 = pc_rel & 0xfff;
            write32le(ptr, (read32le(ptr) & 0x1fff07f) | ((lo12 & 0x1f) << 7) | ((lo12 & 0xfe0) << 20));
            break;
        }
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT: {
            uint64_t pc_rel = val - addr;
            if (((pc_rel + (1ULL << 31)) >> 32) != 0) {
                assertf(0, "R_RISCV_CALL relocation out of range");
            }
            // AUIPC + JALR sequence
            uint32_t hi20 = (pc_rel + 0x800) >> 12;
            uint32_t lo12 = pc_rel & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            write32le(ptr + 4, (read32le(ptr + 4) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_JAL: {
            uint64_t pc_rel = val - addr;
            if (((pc_rel + (1ULL << 20)) >> 21) != 0) {
                assertf(0, "R_RISCV_JAL relocation out of range");
            }
            uint32_t imm = pc_rel & 0x1fffff;
            uint32_t jal_imm = ((imm & 0x100000) << 11) | (imm & 0xff000) | ((imm & 0x800) << 9) | ((imm & 0x7fe) << 20);
            write32le(ptr, (read32le(ptr) & 0xfff) | jal_imm);
            break;
        }
        case R_RISCV_BRANCH: {
            uint64_t pc_rel = val - addr;
            if (((pc_rel + (1ULL << 12)) >> 13) != 0) {
                assertf(0, "R_RISCV_BRANCH relocation out of range");
            }
            uint32_t imm = pc_rel & 0x1fff;
            uint32_t branch_imm = ((imm & 0x1000) << 19) | ((imm & 0x7e0) << 20) | ((imm & 0x1e) << 7) | ((imm & 0x800) >> 4);
            write32le(ptr, (read32le(ptr) & 0x1fff07f) | branch_imm);
            break;
        }
        case R_RISCV_GOT_HI20: {
            uint64_t got_addr = ctx->got->sh_addr + elf_get_sym_attr(ctx, sym_index, 0)->got_offset;
            uint64_t pc_rel = got_addr - addr;
            uint32_t hi20 = (pc_rel + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_COPY:
            break;
        case R_RISCV_JUMP_SLOT:
            write64le(ptr, val - rel->r_addend);
            break;
        case R_RISCV_RELATIVE:
            write64le(ptr, ctx->text_section->sh_addr + rel->r_addend);
            break;
        case R_RISCV_ADD16:
            write16le(ptr, read16le(ptr) + val);
            break;
        case R_RISCV_SUB16:
            write16le(ptr, read16le(ptr) - val);
            break;
        case R_RISCV_SUB32:
            write32le(ptr, read32le(ptr) - val);
            break;
        case R_RISCV_RELAX:
            // RELAX 重定位通常用于链接时优化，不需要运行时处理
            break;
        case R_RISCV_RVC_BRANCH: {
            // 压缩指令分支重定位 (13位偏移)
            uint64_t pc_rel = val - addr;
            if (((pc_rel + (1ULL << 8)) >> 9) != 0) {
                assertf(0, "R_RISCV_RVC_BRANCH relocation out of range");
            }
            uint16_t imm = pc_rel & 0x1ff;
            uint16_t branch_imm = ((imm & 0x100) << 4) | ((imm & 0x18) << 7) | ((imm & 0x6) << 1) | ((imm & 0x60) >> 1) | ((imm & 0x80) >> 3);
            write16le(ptr, (read16le(ptr) & 0xe383) | branch_imm);
            break;
        }
        case R_RISCV_RVC_JUMP: {
            // 压缩指令跳转重定位 (12位偏移)
            uint64_t pc_rel = val - addr;
            if (((pc_rel + (1ULL << 11)) >> 12) != 0) {
                assertf(0, "R_RISCV_RVC_JUMP relocation out of range");
            }
            uint16_t imm = pc_rel & 0xfff;
            uint16_t jump_imm = ((imm & 0x800) << 1) | ((imm & 0x400) >> 2) | ((imm & 0x300) >> 1) | ((imm & 0x80) >> 1) | ((imm & 0x40) << 1) | ((imm & 0x20) >> 3) | ((imm & 0x10) << 7) | ((imm & 0xe) << 2);
            write16le(ptr, (read16le(ptr) & 0xe003) | jump_imm);
            break;
        }
        case R_RISCV_ADD32:
            write32le(ptr, read32le(ptr) + val);
            break;
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
        case R_RISCV_32_PCREL: {
            uint64_t pc_rel = val - addr;
            write32le(ptr, pc_rel);
            break;
        }
        case R_RISCV_TPREL_HI20: {
            // Thread-local storage high 20 bits
            uint32_t hi20 = (val + 0x800) >> 12;
            write32le(ptr, (read32le(ptr) & 0xfff) | (hi20 << 12));
            break;
        }
        case R_RISCV_TPREL_LO12_I: {
            // Thread-local storage low 12 bits (I-type)
            uint32_t lo12 = val & 0xfff;
            write32le(ptr, (read32le(ptr) & 0xfffff) | (lo12 << 20));
            break;
        }
        case R_RISCV_TPREL_ADD:
            // Thread-local storage add (no operation needed)
            break;
        case R_RISCV_ADD64:
            write64le(ptr, read64le(ptr) + val);
            break;
        case R_RISCV_SUB6: {
            uint8_t current = *ptr;
            *ptr = (current & 0xc0) | ((current - val) & 0x3f);
            break;
        }
        case R_RISCV_SUB8:
            *ptr = *ptr - val;
            break;
        case R_RISCV_SUB64:
            write64le(ptr, read64le(ptr) - val);
            break;
        case R_RISCV_SET6: {
            *ptr = (*ptr & 0xc0) | (val & 0x3f);
            break;
        }
        case R_RISCV_SET8:
            *ptr = val;
            break;
        case R_RISCV_SET16:
            write16le(ptr, val);
            break;
        default:
            assertf(0, "Unhandled relocation type %x at %lx", type, addr);
            //            log_error("unhandled relocation type %d", type);
            break;
    }
}

static inline void
riscv64_rewrite_rel_symbol(riscv64_asm_inst_t *operation, riscv64_asm_operand_t *operand, uint64_t rel_diff) {
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
                        int64_t rel_diff = sym.st_value - section_offset;
                        riscv64_rewrite_rel_symbol(operation, rel_operand, rel_diff);
                    } else {
                        // 未知符号，不做处理，依旧使用 symbol 参数
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
                        str_rcpy(caller.target_name, call_target, 24);
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
        if (sym->st_value > 0) {
            // 内部符号，计算相对偏移
            uint64_t rel_diff = sym->st_value - *temp->offset;
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
            }

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
            // 看看奇怪的 a009 是怎么来的，不应该有这个才对！
            printf("c=%s, offset=0x%lx\n", c->linkident, ctx->text_section->data_count);
            asm_op_to_string(0, temp->operation);
            code_to_string(temp->data, temp->data_count);

            elf_put_data(ctx->text_section, temp->data, temp->data_count);
            c->text_count += temp->data_count;
        }
    }
}

#endif // NATURE_BINARY_ARCH_RISCV64_H
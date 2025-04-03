#ifndef NATURE_BINARY_ARCH_ARM64_H
#define NATURE_BINARY_ARCH_ARM64_H

#include "src/binary/encoding/arm64/asm.h"
#include "src/binary/encoding/arm64/opcode.h"
#include "src/binary/linker.h"

#include <stdlib.h>

#define ARM64_ELF_START_ADDR 0x400000
#define ARM64_ELF_PAGE_SIZE 0x200000

typedef struct {
    uint32_t data;                   // 汇编指令
    uint8_t data_count;              // 汇编指令长度, 单位 byte(arm64 默认长度 4byte)
    uint64_t *offset;                // 指令的位置
    arm64_asm_inst_t *operation;     // 原始指令,指令改写与二次扫描时使用
    string rel_symbol;               // 使用的符号,二次扫描时用于判断是否需要重定位，目前都只适用于 label
    arm64_asm_operand_t *rel_operand;// 引用自 asm_operations
    uint64_t sym_index;              // 指令引用的符号在符号表的索引，如果指令发生了 slot 变更，则相应的符号的 value 同样需要变更
    void *rel;                       // elf_rela, mach relacate_info
} arm64_build_temp_t;

static inline uint64_t arm64_create_plt_entry(elf_context_t *ctx, uint64_t got_offset, sym_attr_t *attr) {
    section_t *plt = ctx->plt;

    if (plt->data_count == 0) {
        // PLT0 entry (20 bytes)
        uint8_t *p = section_ptr_add(plt, 32);

        // stp x16, x30, [sp, #-16]!    // Save x16 and link register
        write32le(p, 0xa9bf7bf0);

        // adrp x16, Page(got)          // Load page address of GOT
        write32le(p + 4, 0x90000010);

        // ldr x17, [x16, #GOTPLT[1]]   // Load address of link_map
        write32le(p + 8, 0xf9400211);

        // add x16, x16, #off(got)      // Add offset of GOT
        write32le(p + 12, 0x91000210);

        // br x17                        // Jump to dynamic linker
        write32le(p + 16, 0xd61f0220);

        // PLT0 padding to maintain 16-byte alignment
        write32le(p + 20, 0xd503201f);// nop
        write32le(p + 24, 0xd503201f);// nop
        write32le(p + 28, 0xd503201f);// nop
    }

    // Create PLT entry (16 bytes)
    uint64_t plt_offset = plt->data_count;
    uint8_t *p = section_ptr_add(plt, 16);

    // adrp x16, Page(got + n)          // Load page address of GOT entry
    write32le(p, 0x90000010 | ((got_offset & 0xfffff000) << 3));

    // ldr x17, [x16, #off(got + n)]    // Load address from GOT
    write32le(p + 4, 0xf9400211 | ((got_offset & 0xfff) << 10));

    // add x16, x16, #off(got + n)      // Add offset of GOT entry
    write32le(p + 8, 0x91000210 | ((got_offset & 0xfff) << 10));

    // br x17                           // Jump to target
    write32le(p + 12, 0xd61f0220);

    return plt_offset;
}

static inline int arm64_gotplt_entry_type(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_AARCH64_NONE:
        case R_AARCH64_P32_ABS32:
        case R_AARCH64_P32_COPY:
        case R_AARCH64_P32_GLOB_DAT:
        case R_AARCH64_P32_JUMP_SLOT:
        case R_AARCH64_P32_RELATIVE:
        case R_AARCH64_P32_TLS_DTPMOD:
        case R_AARCH64_P32_TLS_DTPREL:
        case R_AARCH64_P32_TLS_TPREL:
        case R_AARCH64_P32_TLSDESC:
        case R_AARCH64_P32_IRELATIVE:

        case R_AARCH64_PREL32:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_COPY:
            return NO_GOTPLT_ENTRY;

        case R_AARCH64_ABS32:
        case R_AARCH64_ABS64:
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
            return AUTO_GOTPLT_ENTRY;

        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_LD64_GOT_LO12_NC:
            return ALWAYS_GOTPLT_ENTRY;
    }
    return -1;
}

static inline int8_t arm64_is_code_relocate(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_AARCH64_ABS32:
        case R_AARCH64_ABS64:
        case R_AARCH64_PREL32:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_LD64_GOT_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_COPY:
            return 0;

        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
        case R_AARCH64_JUMP_SLOT:
            return 1;
    }
    return -1;
}


static inline bool arm64_is_imm_operand(arm64_asm_operand_t *operand) {
    return operand->type == ARM64_ASM_OPERAND_IMMEDIATE;
}


static inline bool arm64_is_call_op(arm64_asm_inst_t *operation) {
    return operation->raw_opcode == R_BL || operation->raw_opcode == R_BLR;
}

static inline bool arm64_is_branch_op(arm64_asm_inst_t *operation) {
    return operation->raw_opcode >= R_B && operation->raw_opcode <= R_BNV;
}

static inline arm64_asm_operand_t *arm64_extract_symbol_operand(arm64_asm_inst_t *operation) {
    for (int k = 0; k < operation->count; k++) {
        if (operation->operands[k] && operation->operands[k]->type == ARM64_ASM_OPERAND_SYMBOL) {
            return operation->operands[k];
        }
    }
    return NULL;
}

static inline arm64_build_temp_t *arm64_build_temp_new(arm64_asm_inst_t *operation) {
    arm64_build_temp_t *temp = NEW(arm64_build_temp_t);
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
elf_arm64_relocate(elf_context_t *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, uint64_t addr, uint64_t val) {
    int sym_index = ELF64_R_SYM(rel->r_info);

    switch (type) {
        case R_AARCH64_ABS64:
            add64le(ptr, val);
            break;
        case R_AARCH64_ABS32:
            add32le(ptr, val);
            break;
        case R_AARCH64_PREL32:
            add32le(ptr, val - addr);
            break;
        case R_AARCH64_MOVW_UABS_G0_NC:
            write32le(ptr, ((read32le(ptr) & 0xffe0001f) | (val & 0xffff) << 5));
            break;
        case R_AARCH64_MOVW_UABS_G1_NC:
            write32le(ptr, ((read32le(ptr) & 0xffe0001f) | (val >> 16 & 0xffff) << 5));
            break;
        case R_AARCH64_MOVW_UABS_G2_NC:
            write32le(ptr, ((read32le(ptr) & 0xffe0001f) | (val >> 32 & 0xffff) << 5));
            break;
        case R_AARCH64_MOVW_UABS_G3:
            write32le(ptr, ((read32le(ptr) & 0xffe0001f) | (val >> 48 & 0xffff) << 5));
            break;
        case R_AARCH64_ADR_PREL_PG_HI21: {
            uint64_t off = (val >> 12) - (addr >> 12);
            if ((off + ((uint64_t) 1 << 20)) >> 21) {
                assertf(0, "R_AARCH64_ADR_PREL_PG_HI21 relocation failed");
            }
            write32le(ptr, ((read32le(ptr) & 0x9f00001f) | (off & 0x1ffffc) << 3 | (off & 3) << 29));
            break;
        }
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
            write32le(ptr, ((read32le(ptr) & 0xffc003ff) | (val & 0xfff) << 10));
            break;
        case R_AARCH64_LDST16_ABS_LO12_NC:
            write32le(ptr, ((read32le(ptr) & 0xffc003ff) | (val & 0xffe) << 9));
            break;
        case R_AARCH64_LDST32_ABS_LO12_NC:
            write32le(ptr, ((read32le(ptr) & 0xffc003ff) | (val & 0xffc) << 8));
            break;
        case R_AARCH64_LDST64_ABS_LO12_NC:
            write32le(ptr, ((read32le(ptr) & 0xffc003ff) | (val & 0xff8) << 7));
            break;
        case R_AARCH64_LDST128_ABS_LO12_NC:
            write32le(ptr, ((read32le(ptr) & 0xffc003ff) | (val & 0xff0) << 6));
            break;
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
            if (((val - addr) + ((uint64_t) 1 << 27)) & ~(uint64_t) 0xffffffc) {
                assertf(0, "R_AARCH64_(JUMP|CALL)26 relocation failed");
            }
            write32le(
                    ptr, (0x14000000 | (uint32_t) (type == R_AARCH64_CALL26) << 31 | ((val - addr) >> 2 & 0x3ffffff)));
            break;
        case R_AARCH64_ADR_GOT_PAGE: {
            uint64_t off = (((ctx->got->sh_addr + elf_get_sym_attr(ctx, sym_index, 0)->got_offset) >> 12) - (addr >> 12));
            if ((off + ((uint64_t) 1 << 20)) >> 21) {
                assertf(0, "R_AARCH64_ADR_GOT_PAGE relocation failed");
            }
            write32le(ptr, ((read32le(ptr) & 0x9f00001f) | (off & 0x1ffffc) << 3 | (off & 3) << 29));
            break;
        }
        case R_AARCH64_LD64_GOT_LO12_NC:
            write32le(
                    ptr, ((read32le(ptr) & 0xfff803ff) | ((ctx->got->sh_addr + elf_get_sym_attr(ctx, sym_index, 0)->got_offset) & 0xff8) << 7));
            break;
        case R_AARCH64_COPY:
            break;
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
            write64le(ptr, val - rel->r_addend);
            break;
        case R_AARCH64_RELATIVE:
            // Handle relative relocation if needed
            break;
        default:
            assertf(0, "Unhandled relocation type %x at %lx", type, addr);
            break;
    }
}


static inline void
arm64_rewrite_rel_symbol(arm64_asm_inst_t *operation, arm64_asm_operand_t *operand, uint64_t rel_diff) {
    int addend = 0;

    // 如果不是符号操作数,直接返回
    if (operand->type != ARM64_ASM_OPERAND_SYMBOL) {
        return;
    }

    operand->immediate = rel_diff - addend;// arm64 具有固定长度 addend 4byte
    operand->type = ARM64_ASM_OPERAND_IMMEDIATE;
}

static inline void elf_arm64_operation_encodings(elf_context_t *ctx, slice_t *closures) {
    if (closures->count == 0) {
        return;
    }

    slice_t *build_temps = slice_new();
    uint64_t section_offset = 0;// text section slot

    // 第一遍扫描
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];

        uint64_t fn_offset = 0;

        for (int j = 0; j < c->asm_operations->count; ++j) {
            arm64_asm_inst_t *operation = c->asm_operations->take[j];
            arm64_build_temp_t *temp = arm64_build_temp_new(operation);
            slice_push(build_temps, temp);
            slice_push(c->asm_build_temps, temp);

            *temp->offset = section_offset;

            // 处理标签符号定义
            if (operation->raw_opcode == R_LABEL) {
                arm64_asm_operand_t *operand = operation->operands[0];


                // 添加到符号表
                Elf64_Sym sym = {
                        .st_shndx = ctx->text_section->sh_index,
                        .st_size = 0,
                        .st_info = ELF64_ST_INFO(!operand->symbol.is_local, STT_FUNC),
                        //                    .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
                        .st_other = 0,
                        .st_value = *temp->offset,
                };
                temp->sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, operand->symbol.name);
                continue;
            }

            // 处理符号引用
            arm64_asm_operand_t *rel_operand = arm64_extract_symbol_operand(operation);
            char *call_target = NULL;
            if (rel_operand != NULL) {
                assert(rel_operand->type == ARM64_ASM_OPERAND_SYMBOL);
                call_target = rel_operand->symbol.name;

                // 判断是否为分支/调用指令
                if (arm64_is_call_op(operation) || arm64_is_branch_op(operation)) {
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, rel_operand->symbol.name);
                    if (sym_index > 0) {
                        // 已存在的符号，计算相对偏移并进行符号改写
                        Elf64_Sym sym = ((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
                        int64_t rel_diff = sym.st_value - section_offset;
                        arm64_rewrite_rel_symbol(operation, rel_operand, rel_diff);
                    } else {
                        // 未知符号，不做处理，依旧使用 symbol 参数
                        //                        arm64_rewrite_rel_symbol(operation, rel_operand, 0);
                        temp->rel_operand = rel_operand;
                        temp->rel_symbol = rel_operand->symbol.name;
                    }
                } else {
                    // 数据指令访问添加重定位项即可
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, rel_operand->symbol.name);
                    if (sym_index == 0) {
                        // 添加未定义符号
                        Elf64_Sym sym = {
                                .st_shndx = 0,
                                .st_size = 0,
                                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
                                .st_other = 0,
                                .st_value = 0,
                        };
                        sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, rel_operand->symbol.name);
                    }


                    // 生成重定位信息
                    temp->sym_index = sym_index;

                    temp->data = arm64_asm_inst_encoding(operation, &temp->data_count);
                    section_offset += temp->data_count;
                    fn_offset += temp->data_count;


                    // 添加重定位项
                    int reloc_type = R_AARCH64_ADR_PREL_PG_HI21;
                    if (rel_operand->symbol.reloc_type == ARM64_RELOC_LO12) {
                        reloc_type = R_AARCH64_ADD_ABS_LO12_NC;
                    }
                    temp->rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                                 *temp->offset, reloc_type,
                                                 (int) sym_index, 0);
                    continue;
                }
            }

            // 编码指令
            temp->data = arm64_asm_inst_encoding(operation, &temp->data_count);
            section_offset += temp->data_count;
            fn_offset += temp->data_count;

            if (operation->raw_opcode == R_BL || operation->raw_opcode == R_BLR) {
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
        arm64_build_temp_t *temp = build_temps->take[i];
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
            arm64_rewrite_rel_symbol(temp->operation, temp->rel_operand, rel_diff);

            temp->data = arm64_asm_inst_encoding(temp->operation, &temp->data_count);
        } else {
            int reloc_type;

            switch (temp->operation->raw_opcode) {
                case R_B:
                    reloc_type = R_AARCH64_JUMP26;
                    break;
                case R_BL:
                    reloc_type = R_AARCH64_CALL26;
                    break;
                default:
                    // 处理 BCC 等其他条件分支指令
                    reloc_type = R_AARCH64_CONDBR19;
                    break;
            }

            // 确认是外部符号，进行重定位处理
            temp->rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                         *temp->offset, reloc_type,
                                         (int) sym_index, 0);
        }
    }

    // 生成最终的代码段数据
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];
        c->text_count = 0;
        for (int j = 0; j < c->asm_build_temps->count; ++j) {
            arm64_build_temp_t *temp = c->asm_build_temps->take[j];
            if (temp->data_count == 0) {
                continue;
            }

            uint8_t bytes[4];
            bytes[0] = temp->data & 0xFF;        // 最低字节
            bytes[1] = (temp->data >> 8) & 0xFF; // 次低字节
            bytes[2] = (temp->data >> 16) & 0xFF;// 次高字节
            bytes[3] = (temp->data >> 24) & 0xFF;// 最高字节

            elf_put_data(ctx->text_section, bytes, temp->data_count);
            c->text_count += temp->data_count;
        }
    }
}

static void mach_arm64_operation_encodings(mach_context_t *ctx, slice_t *closures) {
    if (closures->count == 0) {
        return;
    }

    slice_t *build_temps = slice_new();
    uint64_t section_offset = 0;// text section slot
    table_t *symtab_hash = table_new();

    // 第一遍扫描
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];

        uint64_t fn_offset = 0;

        for (int j = 0; j < c->asm_operations->count; ++j) {
            arm64_asm_inst_t *operation = c->asm_operations->take[j];
            arm64_build_temp_t *temp = arm64_build_temp_new(operation);
            slice_push(build_temps, temp);
            slice_push(c->asm_build_temps, temp);

            *temp->offset = section_offset;

            // 处理标签符号定义
            if (operation->raw_opcode == R_LABEL) {
                arm64_asm_operand_t *operand = operation->operands[0];
                uint32_t n_type = N_SECT;
                if (!operand->symbol.is_local) {
                    n_type |= N_EXT;
                }
                temp->sym_index = mach_put_sym(ctx->symtab_command, &(struct nlist_64){
                                                                            .n_sect = ctx->text_section->sh_index,
                                                                            .n_value = *temp->offset,
                                                                            .n_type = n_type,
                                                                    },
                                               operand->symbol.name);

                table_set(symtab_hash, operand->symbol.name, (void *) temp->sym_index);
                continue;
            }

            // 处理符号引用(label 或者 symbol 符号)
            arm64_asm_operand_t *rel_operand = arm64_extract_symbol_operand(operation);
            char *call_target = NULL;
            if (rel_operand != NULL) {
                assert(rel_operand->type == ARM64_ASM_OPERAND_SYMBOL);
                call_target = rel_operand->symbol.name;

                // 判断是否为分支/调用指令
                if (arm64_is_call_op(operation) || arm64_is_branch_op(operation)) {
                    uint64_t sym_index = (uint64_t) table_get(symtab_hash, rel_operand->symbol.name);
                    if (sym_index > 0) {
                        // 已存在的符号，计算相对偏移并进行符号改写
                        struct nlist_64 sym = ((struct nlist_64 *) ctx->symtab_command->symbols->data)[sym_index];
                        int64_t rel_diff = sym.n_value - section_offset;
                        arm64_rewrite_rel_symbol(operation, rel_operand, rel_diff);
                    } else {
                        // 未知符号，不做处理，依旧使用 symbol 参数
                        temp->rel_operand = rel_operand;
                        temp->rel_symbol = rel_operand->symbol.name;
                    }
                } else {
                    // 其他指令的符号引用(如数据访问)
                    uint64_t sym_index = (uint64_t) table_get(symtab_hash, rel_operand->symbol.name);
                    if (sym_index == 0) {
                        // 添加未定义符号
                        sym_index = mach_put_sym(ctx->symtab_command, &(struct nlist_64){
                                                                              .n_sect = NO_SECT,
                                                                              .n_value = 0,
                                                                              .n_type = N_UNDF | N_EXT,
                                                                      },
                                                 rel_operand->symbol.name);
                    }

                    // 生成重定位信息
                    temp->sym_index = sym_index;
                    temp->data = arm64_asm_inst_encoding(operation, &temp->data_count);
                    section_offset += temp->data_count;
                    fn_offset += temp->data_count;

                    // 添加重定位项
                    uint64_t reloc_type = ARM64_RELOC_PAGE21;
                    if (rel_operand->symbol.reloc_type == ARM64_RELOC_LO12) {
                        reloc_type = ARM64_RELOC_PAGEOFF12;
                    }
                    temp->rel = mach_put_relocate(ctx, ctx->text_section, *temp->offset,
                                                  reloc_type, (int) sym_index);
                    continue;
                }
            }

            // 编码指令
            temp->data = arm64_asm_inst_encoding(operation, &temp->data_count);
            section_offset += temp->data_count;
            fn_offset += temp->data_count;

            if (operation->raw_opcode == R_BL || operation->raw_opcode == R_BLR) {
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
                        strncpy(caller.target_name, call_target, 23);
                    }

                    ct_list_push(ct_caller_list, &caller);
                } while (0);
            }
        }
    }

    // 第二遍扫描处理重定位
    for (int i = 0; i < build_temps->count; ++i) {
        arm64_build_temp_t *temp = build_temps->take[i];
        if (!temp->rel_symbol) {
            continue;
        }

        uint64_t sym_index = (uint64_t) table_get(symtab_hash, temp->rel_symbol);
        if (sym_index == 0) {
            // 添加未定义符号
            sym_index = mach_put_sym(ctx->symtab_command, &(struct nlist_64){
                                                                  .n_sect = NO_SECT,
                                                                  .n_value = 0,
                                                                  .n_type = N_UNDF | N_EXT,
                                                          },
                                     temp->rel_symbol);
        }

        struct nlist_64 *sym = &((struct nlist_64 *) ctx->symtab_command->symbols->data)[sym_index];
        if (sym->n_value > 0) {
            // 内部符号，计算相对偏移
            uint64_t rel_diff = sym->n_value - *temp->offset;
            arm64_rewrite_rel_symbol(temp->operation, temp->rel_operand, rel_diff);

            temp->data = arm64_asm_inst_encoding(temp->operation, &temp->data_count);
        } else {
            // 确认是外部符号，进行重定位处理
            temp->rel = mach_put_relocate(ctx, ctx->text_section, *temp->offset,
                                          ARM64_RELOC_BRANCH26, (int) sym_index);
        }
    }

    // 生成最终的代码段数据
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];
        c->text_count = 0;
        for (int j = 0; j < c->asm_build_temps->count; ++j) {
            arm64_build_temp_t *temp = c->asm_build_temps->take[j];
            if (temp->data_count == 0) {
                continue;
            }

            uint8_t bytes[4];
            bytes[0] = temp->data & 0xFF;        // 最低字节
            bytes[1] = (temp->data >> 8) & 0xFF; // 次低字节
            bytes[2] = (temp->data >> 16) & 0xFF;// 次高字节
            bytes[3] = (temp->data >> 24) & 0xFF;// 最高字节

            mach_put_data(ctx->text_section, bytes, temp->data_count);
            c->text_count += temp->data_count;
        }
    }
}

#endif// NATURE_BINARY_ARCH_ARM64_H

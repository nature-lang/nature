#include "amd64.h"
#include "src/cross.h"
#include "elf.h"
#include "utils/error.h"
#include "string.h"
#include <assert.h>

static bool is_imm_operand(asm_operand_t *operand) {
    return operand->type == ASM_OPERAND_TYPE_UINT ||
           operand->type == ASM_OPERAND_TYPE_UINT8 ||
           operand->type == ASM_OPERAND_TYPE_UINT16 ||
           operand->type == ASM_OPERAND_TYPE_UINT32 ||
           operand->type == ASM_OPERAND_TYPE_UINT64 ||
           operand->type == ASM_OPERAND_TYPE_INT8 ||
           operand->type == ASM_OPERAND_TYPE_INT32;
}

/**
 * mov 0x0(%rip),%rsi  // 48 8b 35 00 00 00 00，return 3
 * lea 0x0(%rip),%rax // 48 8d 05 00 00 00 00
 * call 0x0(%rip) // ff 15 00 00 00 00
 * call rel32 // e8 00 00 00 00
 * jmp rel32 // e9 00 00 00 00
 * movsd  0x0(%rip),%xmm0 //  f2 0f 10 05 00 00 00 00
 * rex mov WORD PTR [rip+0x0],0x2551 // 0x66, 0x40, 0xC7, 0x05, 0x00, 0x00, 0x00, 0x00, 0x51, 0x25
 * movb $0x18,0x0(%rip) // 40 c6 05 00 00 00 00 18 由于包含 imm 部分，所以 rip offset 不能是直接减去 4，还需要剪掉 imm 部分的宽度。
 * @param inst_count
 * @return
 */
static uint64_t rip_offset(uint64_t data_count, asm_operation_t *operation) {
    // R_X86_64_PC32 默认就是占用 4 byte
    uint64_t offset = data_count - 4;

    if (operation->count > 1) {
        asm_operand_t *operand = operation->operands[1];
        if (is_imm_operand(operand)) {
            offset -= operand->size;
        }
    }

    return offset;
}

static uint8_t jmp_rewrite_rel8_reduce_count(asm_operation_t *operation) {
    if (operation->name[0] != 'j') {
        return 0;
    }
    if (str_equal(operation->name, "jmp")) {
        //   b:	eb f3                	jmp    0 <test>
        //   d:	e9 14 00 00 00       	jmpq   26 <test+0x26>
        return 5 - 2;
    }
    //   3:	74 fb                	je     0 <test>
    //   5:	0f 84 08 00 00 00    	je     13 <test+0x13>
    return 6 - 2;
}

static uint8_t jmp_operation_count(asm_operation_t *operation, uint8_t size) {
    if (operation->name[0] != 'j') {
        error_exit("[linux_elf_amd64_jmp_inst_count] operation: %s not jmp or jcc:", operation->name);
    }
    if (size == BYTE) {
        return 2;
    }
    if (str_equal(operation->name, "jmp")) {
        return 5;
    }
    return 6;
}


static bool is_call_op(char *name) {
    return str_equal(name, "call");
}

static bool is_jmp_op(char *name) {
    return name[0] == 'j';
}

/**
 * symbol to rel32 or rel8
 * @param operation
 * @param operand
 * @param rel_diff
 */
static void amd64_rewrite_rel_symbol(asm_operation_t *operation, asm_operand_t *operand, uint64_t rel_diff) {

    // 目标 operand 已经确定了指令长度，不再是一个符号，所以不能在随意修正了
    if (operand->type != ASM_OPERAND_TYPE_SYMBOL) {
        if (rel_diff == 0) {
            return;
        }

        if (operand->type == ASM_OPERAND_TYPE_UINT32) {
            uint8_t data_count = 5;
            if (!is_call_op(operation->name)) {
                data_count = jmp_operation_count(operation, DWORD);
            }
            asm_uint32_t *v = NEW(asm_uint32_t);
            v->value = (uint32_t) (rel_diff - data_count);
            operand->value = v;
        } else {
            asm_uint8_t *v = NEW(asm_uint8_t);
            v->value = (uint8_t) (rel_diff - 2); // -2 表示去掉当前指令的差值
            operand->value = v;
        }
        return;
    }


    // symbol to rel32
    // call 指令不能改写成 rel8(-128 ~ 127)
    if (rel_diff == 0 || rel_diff > 127 || rel_diff < -128 || is_call_op(operation->name)) {
        uint8_t data_count = 5;
        if (!is_call_op(operation->name)) {
            data_count = jmp_operation_count(operation, DWORD);
        }

        operand->type = ASM_OPERAND_TYPE_UINT32;
        operand->size = DWORD;
        asm_uint32_t *v = NEW(asm_uint32_t);
        v->value = 0;
        if (rel_diff != 0) {
            v->value = (uint32_t) (rel_diff - data_count); // -5 表示去掉当前指令的差值
        }
        operand->value = v;
        return;
    }

    // jmp 指令
    operand->type = ASM_OPERAND_TYPE_UINT8;
    operand->size = BYTE;
    asm_uint8_t *v = NEW(asm_uint8_t);
    v->value = (uint8_t) (rel_diff - jmp_operation_count(operation, operand->size)); // 去掉当前指令的差值
    operand->value = v;
}

static void amd64_rewrite_rip_symbol(asm_operand_t *operand) {
    operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;
    operand->size = operand->size;
    asm_rip_relative_t *r = NEW(asm_rip_relative_t);
    r->disp = 0;
    operand->value = r;
}

static asm_operand_t *extract_symbol_operand(asm_operation_t *operation) {
    for (int i = 0; i < operation->count; ++i) {
        asm_operand_t *operand = operation->operands[i];
        if (operand->type == ASM_OPERAND_TYPE_SYMBOL) {
            return operand;
        }
    }
    return NULL;
}


static amd64_build_temp_t *build_temp_new(asm_operation_t *operation) {
    amd64_build_temp_t *temp = NEW(amd64_build_temp_t);
    temp->data = mallocz(sizeof(uint8_t) * 30);
    temp->data_count = 0;
    temp->inst = NULL;
    temp->offset = NEW(uint64_t);
    temp->operation = operation;
    temp->rel_operand = NULL;
    temp->rel_symbol = NULL;
//    temp->may_need_reduce = false;
//    temp->reduce_count = 0;
    temp->sym_index = 0;
    temp->elf_rel = NULL;
    return temp;
}

//static void amd64_rewrite_rel32_to_rel8(amd64_build_temp_t *temp) {
//    asm_uint8_t *operand = NEW(asm_uint8_t);
//    operand->value = 0; // 仅占位即可
//    temp->operation->count = 1;
//    temp->operation->operands[0]->type = ASM_OPERAND_TYPE_UINT8;
//    temp->operation->operands[0]->size = BYTE;
//    temp->operation->operands[0]->value = operand;
//    temp->inst = amd64_operation_encoding(*temp->operation, temp->data, &temp->data_count);
//    temp->may_need_reduce = false;
//}

///**
// * 有 bug,请勿调用, 暂时不搞这里到逻辑了，太复杂了
// * @param symtab
// * @param build_temps
// * @param section_offset
// * @param name
// */
//static void amd64_confirm_rel(section_t *symtab, slice_t *build_temps, uint64_t *section_offset, string name) {
//    if (build_temps->count == 0) {
//        return;
//    }
//
//    amd64_build_temp_t *temp;
//    int i;
//    uint8_t reduce_count = 0;
//
//    // 从尾部开始查找,
//    for (i = build_temps->count - 2; i > 0; --i) {
//        temp = build_temps->take[i];
//        // 直到总指令长度超过 128 就可以结束查找
//        if ((*section_offset - reduce_count - *temp->offset) > 128) {
//            break;
//        }
//
//        // 前 128 个指令内找到了符号引用
//        if (temp->may_need_reduce && str_equal(temp->rel_symbol, name)) {
//            reduce_count += temp->reduce_count;
//        }
//    }
//
//    if (reduce_count == 0) {
//        return;
//    }
//
//    // 指令偏移修复
//    uint64_t temp_section_offset = *temp->offset;
//    // 从 temp 开始遍历
//    for (int j = i; j < build_temps->count; ++j) {
//        temp = build_temps->take[j];
//        *temp->offset = temp_section_offset;
//
//        if (temp->may_need_reduce && str_equal(temp->rel_symbol, name)) {
//            amd64_rewrite_rel32_to_rel8(temp); // 这里修正了历史上的 data->count
//        }
//        // 如果存在符号表引用了位置数据，则修正符号表中的数据
//        if (temp->sym_index > 0) {
//            ((Elf64_Sym *) symtab->data)[temp->sym_index].st_value = *temp->offset;
//        }
//        if (temp->elf_rel) {
//            temp->elf_rel->r_offset = *temp->offset + operation_rip_offset(temp->inst);
//        }
//
//        temp_section_offset += temp->data_count;
//    }
//
//    // 更新 section slot
//    *section_offset = temp_section_offset;
//}

int amd64_gotplt_entry_type(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_COPY:
        case R_X86_64_RELATIVE:
            return NO_GOTPLT_ENTRY;

            /* The following relocs wouldn't normally need GOT or PLT
               slots, but we need them for simplicity in the link
               editor part.  See our caller for comments.  */
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_64:
        case R_X86_64_PC32:
        case R_X86_64_PC64:
            return AUTO_GOTPLT_ENTRY;

        case R_X86_64_GOTTPOFF:
            return BUILD_GOT_ONLY;

        case R_X86_64_GOT32:
        case R_X86_64_GOT64:
        case R_X86_64_GOTPC32:
        case R_X86_64_GOTPC64:
        case R_X86_64_GOTOFF64:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        case R_X86_64_TPOFF32:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_PLT32:
        case R_X86_64_PLTOFF64:
            return ALWAYS_GOTPLT_ENTRY;
    }

    return -1;
}

uint64_t amd64_create_plt_entry(linker_context *ctx, uint64_t got_offset, sym_attr_t *attr) {
    section_t *plt = ctx->plt;

    int modrm = 0x25;
    if (plt->data_count == 0) {
        uint8_t *p = section_ptr_add(plt, 16);
        p[0] = 0xff; // pushl got + PTR_SIZE
        p[1] = modrm + 0x10;
        write32le(p + 2, 8);
        p[6] = 0xff;
        p[7] = modrm;
        write32le(p + 8, POINTER_SIZE * 2);
    }
    uint64_t plt_offset = plt->data_count;
    uint8_t plt_rel_offset = plt->relocate ? plt->relocate->data_count : 0;

    uint8_t *p = section_ptr_add(plt, 16);
    p[0] = 0xff; /* jmp *(got + x) */
    p[1] = modrm;
    write32le(p + 2, got_offset);
    p[6] = 0x68; /* push $xxx */
    /* On x86-64, the relocation is referred to by _index_ */
    write32le(p + 7, plt_rel_offset / sizeof(Elf64_Rela) - 1);
    p[11] = 0xe9; /* jmp plt_start */
    write32le(p + 12, -(plt->data_count));
    return plt_offset;
}

int8_t amd64_is_code_relocate(uint64_t relocate_type) {
    switch (relocate_type) {
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_64:
        case R_X86_64_GOTPC32:
        case R_X86_64_GOTPC64:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_GOT32:
        case R_X86_64_GOT64:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_COPY:
        case R_X86_64_RELATIVE:
        case R_X86_64_GOTOFF64:
        case R_X86_64_TLSGD:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        case R_X86_64_TPOFF32:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
            return 0;

        case R_X86_64_PC32:
        case R_X86_64_PC64:
        case R_X86_64_PLT32:
        case R_X86_64_PLTOFF64:
        case R_X86_64_JUMP_SLOT:
            return 1;
    }
    return -1;
}

void amd64_relocate(linker_context *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, uint64_t addr, uint64_t val) {
    int sym_index = ELF64_R_SYM(rel->r_info);
    switch (type) {
        case R_X86_64_64:
            // 应该是绝对地址定位，所以直接写入符号的绝对位置即可
            add64le(ptr, val);
            break;
        case R_X86_64_32:
        case R_X86_64_32S:
            add32le(ptr, val);
            break;

        case R_X86_64_PC32:
            goto plt32pc32;

        case R_X86_64_PLT32:
            /* fallthrough: val already holds the PLT slot address */

        plt32pc32:
        {
            // 相对地址计算，
            // addr 保存了符号的使用位置（加载到虚拟内存中的位置）
            // val 保存了符号的定义的位置（加载到虚拟内存中的位置）
            // ptr 真正的段数据,存储在编译时内存中，相对地址修正的填充点
            int64_t diff;
            diff = (int64_t) (val - addr);
            if (diff < -2147483648LL || diff > 2147483647LL) {
                assert(false && "relocation failed");
            }
            // 小端写入
            add32le(ptr, diff);
        }
            break;

        case R_X86_64_COPY:
            break;

        case R_X86_64_PLTOFF64:
            add64le(ptr, val - ctx->got->sh_addr + rel->r_addend);
            break;

        case R_X86_64_PC64:
            add64le(ptr, val - addr);
            break;

        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            /* They don't need addend */ // 还有不少这种方式的重定位，got 表的重定位应该都来了？
            write64le(ptr, val - rel->r_addend);
            break;
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
            add32le(ptr, ctx->got->sh_addr - addr +
                         elf_get_sym_attr(ctx, sym_index, 0)->got_offset - 4);
            break;
        case R_X86_64_GOTPC32:
            add32le(ptr, ctx->got->sh_addr - addr + rel->r_addend);
            break;
        case R_X86_64_GOTPC64:
            add64le(ptr, ctx->got->sh_addr - addr + rel->r_addend);
            break;
        case R_X86_64_GOTTPOFF:
            add32le(ptr, val - ctx->got->sh_addr);
            break;
        case R_X86_64_GOT32:
            /* we load the got slot */
            add32le(ptr, elf_get_sym_attr(ctx, sym_index, 0)->got_offset);
            break;
        case R_X86_64_GOT64:
            /* we load the got slot */
            add64le(ptr, elf_get_sym_attr(ctx, sym_index, 0)->got_offset);
            break;
        case R_X86_64_GOTOFF64:
            add64le(ptr, val - ctx->got->sh_addr);
            break;
        case R_X86_64_TLSGD: {
            static const unsigned char expect[] = {
                    /* .byte 0x66; lea 0(%rip),%rdi */
                    0x66, 0x48, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00,
                    /* .word 0x6666; rex64; call __tls_get_addr@PLT */
                    0x66, 0x66, 0x48, 0xe8, 0x00, 0x00, 0x00, 0x00};
            static const unsigned char replace[] = {
                    /* mov %fs:0,%rax */
                    0x64, 0x48, 0x8b, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00,
                    /* lea -4(%rax),%rax */
                    0x48, 0x8d, 0x80, 0x00, 0x00, 0x00, 0x00};

            if (memcmp(ptr - 4, expect, sizeof(expect)) == 0) {
                Elf64_Sym *sym;
                section_t *section;
                int32_t x;

                memcpy(ptr - 4, replace, sizeof(replace));
                rel[1].r_info = ELF64_R_INFO(0, R_X86_64_NONE);
                sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
                section = SEC_TACK(sym->st_shndx);
                x = sym->st_value - section->sh_addr - section->data_count;
                add32le(ptr + 8, x);
            } else {
                error_exit("[amd64_relocate]unexpected R_X86_64_TLSGD pattern");
            }

            break;
        }
        case R_X86_64_TLSLD: {
            static const unsigned char expect[] = {
                    /* lea 0(%rip),%rdi */
                    0x48, 0x8d, 0x3d, 0x00, 0x00, 0x00, 0x00,
                    /* call __tls_get_addr@PLT */
                    0xe8, 0x00, 0x00, 0x00, 0x00};
            static const unsigned char replace[] = {
                    /* data16 data16 data16 mov %fs:0,%rax */
                    0x66, 0x66, 0x66, 0x64, 0x48, 0x8b, 0x04, 0x25,
                    0x00, 0x00, 0x00, 0x00};

            if (memcmp(ptr - 3, expect, sizeof(expect)) == 0) {
                memcpy(ptr - 3, replace, sizeof(replace));
                rel[1].r_info = ELF64_R_INFO(0, R_X86_64_NONE);
            } else {
                error_exit("[amd64_relocate] unexpected R_X86_64_TLSLD pattern");
            }

            break;
        }
        case R_X86_64_DTPOFF32:
        case R_X86_64_TPOFF32: {
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            section_t *s = SEC_TACK(sym->st_shndx);
            int32_t x;

            x = val - s->sh_addr - s->data_count;
            add32le(ptr, x);
            break;
        }
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64: {
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            section_t *s = SEC_TACK(sym->st_shndx);
            int32_t x;

            x = val - s->sh_addr - s->data_count;
            add64le(ptr, x);
            break;
        }
        case R_X86_64_NONE:
            break;
        case R_X86_64_RELATIVE:
            /* do nothing */
            break;
        default:
            error_exit("[amd64_relocate] unknown rel code");
            break;
    }
}

/**
 * 两次遍历汇编
 * @param ctx
 * @param closures
 */
void amd64_operation_encodings(linker_context *ctx, slice_t *closures) {
    if (closures->count == 0) {
        return;
    }

    slice_t *build_temps = slice_new();
    uint64_t section_offset = 0; // text section slot

    // 一次遍历
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];
        for (int j = 0; j < c->asm_operations->count; ++j) {
            asm_operation_t *operation = c->asm_operations->take[j];
            amd64_build_temp_t *temp = build_temp_new(operation);
            slice_push(build_temps, temp);
            slice_push(c->asm_build_temps, temp);

            *temp->offset = section_offset;

            // 定义符号
            if (str_equal(operation->name, "label")) {
                // 解析符号值，并添加到符号表
                asm_symbol_t *s = operation->operands[0]->value;
                // 之前的指令由于找不到相应的符号，所以暂时使用了 rel32 来填充
                // 一旦发现定义点,就需要反推  取消反推重写逻辑,实现太复杂
//                amd64_confirm_rel(ctx->symtab_section, build_temps, &section_offset, s->name);
                // amd64_confirm_rel 可能会修改 section_offset， 所以需要重新计算
//                *temp->offset = section_offset;

                Elf64_Sym sym = {
                        .st_shndx = ctx->text_section->sh_index,
                        .st_size = 0,
                        .st_info = ELF64_ST_INFO(!s->is_local, STT_FUNC),
                        .st_other = 0,
                        .st_value = *temp->offset,
                };
                uint64_t sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, s->name);
                temp->sym_index = sym_index;
                continue;
            }

            asm_operand_t *rel_operand = extract_symbol_operand(operation);
            if (rel_operand != NULL) {
                // 指令引用了符号，符号可能是数据符号的引用，也可能是标签符号的引用
                // 1. 数据符号引用(直接改写成 0x0(rip)) , 已经跨 section 了，此时不能使用相对寻址，会造成链接阶段异常
                // 2. 标签符号引用(在符号表中,表明为内部符号,否则使用 rel32 先占位),都是在 .text section 内，所以可以使用 jmp 相对寻址, 连接器不会破坏同一个段内的位置
                asm_symbol_t *symbol_operand = rel_operand->value;
                // 判断是否为标签符号引用, 比如 call symbol call(一次遍历时不能确定符号是否必定不存在，所以必须等二次遍历才能确定是否写入 rel)
                if (is_call_op(operation->name) || is_jmp_op(operation->name)) {
                    // 标签符号
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, symbol_operand->name);
                    if (sym_index > 0) {
                        // 引用了已经存在的符号，直接计算相对位置即可
                        Elf64_Sym sym = ((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
                        int rel_diff = sym.st_value - section_offset;
                        amd64_rewrite_rel_symbol(operation, rel_operand, rel_diff);
                    } else {
                        // 引用了 label 符号，但是符号目前不在符号表中(可能在后续 text 中，也可以能不在，所以需要二次扫描才能确定,这里仅仅占位)
                        // 此时使用 rel32 占位，~~其中 jmp 指令后续可能需要替换 rel8~~
                        amd64_rewrite_rel_symbol(operation, rel_operand, 0);
                        temp->rel_operand = rel_operand; // 等到二次遍历时再确认是否需要改写
                        temp->rel_symbol = symbol_operand->name;
//                        uint8_t reduce_count = jmp_rewrite_rel8_reduce_count(operation);
//                        if (reduce_count > 0) {
//                            temp->may_need_reduce = true;
//                            temp->reduce_count = reduce_count;
//                        }
                    }
                } else {
                    // 其他指令(可能是 mov 等,对数据段符号的引用)引用了符号，由于不用考虑指令重写的问题,所以直接写入 0(%rip),让重定位阶段去找改符号进行重定位即可
                    // 完全不用考虑是标签符号还是数据符号
                    // 添加到重定位表(.rela.text)
                    // 根据指令计算 slot 重定位 rip slot
                    // 这里先注册到符号表，让符号和和 symbol 关联
                    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, symbol_operand->name);
                    if (sym_index == 0) {
                        Elf64_Sym sym = {
                                .st_shndx = 0,
                                .st_size = 0,
                                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
                                .st_other = 0,
                                .st_value = 0,
                        };
                        sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, symbol_operand->name);
                    }

                    // rewrite symbol
                    amd64_rewrite_rip_symbol(rel_operand);

                    // 编码
                    temp->inst = amd64_operation_encoding(*operation, temp->data, &temp->data_count);
                    section_offset += temp->data_count;

                    // 将符号和 sym_index 关联,rel 记录了符号的使用位置， sym_index 记录的符号的信息(包括 linker 完成后的绝对虚拟地址)
                    // 计算重定位的起点信息
                    uint64_t rel_offset = *temp->offset + rip_offset(temp->data_count, temp->operation);
                    int64_t addend = (int64_t) (*temp->offset + temp->data_count) - (int64_t) rel_offset;

                    // addend = 下一条指令的起始位置 - rel_offset
                    temp->elf_rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                                     rel_offset, R_X86_64_PC32, (int) sym_index, -addend);

                    continue;

                }
            }

            // 编码
            temp->inst = amd64_operation_encoding(*operation, temp->data, &temp->data_count);
            section_offset += temp->data_count;
        }
    }

    // 基于 build_temps 做二次遍历，主要是对不好进行重定位，不会在改写 build opcode 了
    for (int i = 0; i < build_temps->count; ++i) {
        amd64_build_temp_t *temp = build_temps->take[i];
        if (!temp->rel_symbol) {
            continue;
        }

        // 二次扫描时再符号表中找到了符号
        // rel_symbol 仅记录了 label, 数据符号直接使用 rip 寻址
        uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, temp->rel_symbol);
        if (sym_index == 0) {
            Elf64_Sym sym = {
                    .st_shndx = 0,
                    .st_size = 0,
                    .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
                    .st_other = 0,
                    .st_value = 0,
            };
            // 如果遍历没有找到符号则会添加一条  UND 符号信息到符号表中
            //  10: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND string_new
            sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, temp->rel_symbol);
        }

        // sym->st_value 表示符号定义的位置，基于符号所在的 section(.section)
        // 如果是数据符号就是 .data 段的偏移，如果是函数符号就是 .text 段的偏移
        Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
        if (sym->st_value > 0) {
            // 相对偏移 = 当前指令的偏移 - (jmp)目标符号的偏移
            uint64_t rel_diff = sym->st_value - *temp->offset;
            // 仅仅重写了符号，data 长度不会再变了
            amd64_rewrite_rel_symbol(temp->operation, temp->rel_operand, rel_diff);

            uint8_t old_count = temp->data_count;
            temp->inst = amd64_operation_encoding(*temp->operation, temp->data, &temp->data_count);
            assertf(temp->data_count == old_count, "second traverse cannot update encoding data_count");
        } else {
            // st_value = 0 表示这是一个外部的符号， 需要添加重定位
            // 外部符号添加重定位信息(temp->offset + 当前指令长度减去重定位的位置长度。 PC32 默认就是 4byte)
            uint64_t rel_offset = *temp->offset + rip_offset(temp->data_count, temp->operation);
            temp->elf_rel = elf_put_relocate(ctx, ctx->symtab_section, ctx->text_section,
                                             rel_offset, R_X86_64_PC32, (int) sym_index, -4);
        }

    }

    // 代码段已经确定，生成 text 数据
    for (int i = 0; i < closures->count; ++i) {
        closure_t *c = closures->take[i];
        c->text_count = 0;
        for (int j = 0; j < c->asm_build_temps->count; ++j) {
            amd64_build_temp_t *temp = c->asm_build_temps->take[j];
            elf_put_data(ctx->text_section, temp->data, temp->data_count);
            c->text_count += temp->data_count;
        }
    }
}

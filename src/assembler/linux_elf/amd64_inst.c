#include "amd64_inst.h"
#include "utils/helper.h"
#include "utils/table.h"
#include "utils/error.h"
#include "elf.h"

list *linux_elf_amd64_text_insts;

static linux_elf_amd64_text_inst_t *linux_elf_amd64_inst_new(amd64_asm_inst_t asm_inst) {
    linux_elf_amd64_text_inst_t *_inst = NEW(linux_elf_amd64_text_inst_t);
    _inst->data = NULL;
    _inst->count = 0;
    _inst->offset = 0;
    _inst->asm_inst = asm_inst;
    _inst->rel_operand = NULL;
    _inst->rel_symbol = NULL;
    _inst->may_need_reduce = false;
    _inst->reduce_count = 0;
    return _inst;
}

static void linux_elf_amd64_var_operand_rewrite(amd64_asm_operand_t *operand) {
    operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;
    operand->size = QWORD;
    asm_operand_rip_relative_t *r = NEW(asm_operand_rip_relative_t);
    r->disp = 0;
    operand->value = r;
}

static bool linux_elf_amd64_is_jmp(amd64_asm_inst_t asm_inst) {
    return asm_inst.name[0] == 'j';
}

static bool linux_elf_amd64_is_call(amd64_asm_inst_t asm_inst) {
    return str_equal(asm_inst.name, "call");
}

static uint8_t linux_elf_amd64_jmp_reduce_count(amd64_asm_inst_t asm_inst) {
    if (asm_inst.name[0] != 'j') {
        return 0;
    }

    if (str_equal(asm_inst.name, "jmp")) {
        //   b:	eb f3                	jmp    0 <test>
        //   d:	e9 14 00 00 00       	jmpq   26 <test+0x26>
        return 5 - 2;
    }
    //   3:	74 fb                	je     0 <test>
    //   5:	0f 84 08 00 00 00    	je     13 <test+0x13>
    return 6 - 2;
}

static uint8_t linux_elf_amd64_jmp_inst_count(amd64_asm_inst_t inst, uint8_t size) {
    if (inst.name[0] != 'j') {
        error_exit("[linux_elf_amd64_jmp_inst_count] inst: %s not jmp or jcc:", inst.name);
    }
    if (size == BYTE) {
        return 2;
    }
    if (str_equal(inst.name, "jmp")) {
        return 5;
    }
    return 6;
}

static void linux_elf_amd64_operand_rewrite_rel(amd64_asm_inst_t inst, amd64_asm_operand_t *operand, int rel_diff) {
    // 已经确定了指令长度，不能再随意修正了
    if (operand->type != ASM_OPERAND_TYPE_SYMBOL) {
        if (rel_diff == 0) {
            return;
        }
        if (operand->type == ASM_OPERAND_TYPE_UINT32) {
            uint8_t inst_count = 5;
            if (!linux_elf_amd64_is_call(inst)) {
                inst_count = linux_elf_amd64_jmp_inst_count(inst, DWORD);
            }
            asm_operand_uint32_t *v = NEW(asm_operand_uint32_t);
            v->value = (uint32_t) (rel_diff - inst_count);
            operand->value = v;
        } else {
            asm_operand_uint8_t *v = NEW(asm_operand_uint8_t);
            v->value = (uint8_t) (rel_diff - 2); // -2 表示去掉当前指令的差值
            operand->value = v;
        }
        return;
    }

    // symbol to rel32
    // call 指令不能改写成 rel8
    if (rel_diff == 0 || abs(rel_diff) > 128 || linux_elf_amd64_is_call(inst)) {
        uint8_t inst_count = 5;
        if (!linux_elf_amd64_is_call(inst)) {
            inst_count = linux_elf_amd64_jmp_inst_count(inst, DWORD);
        }

        operand->type = ASM_OPERAND_TYPE_UINT32;
        operand->size = DWORD;
        asm_operand_uint32_t *v = NEW(asm_operand_uint32_t);
        v->value = 0;
        if (rel_diff != 0) {
            v->value = (uint32_t) (rel_diff - inst_count); // -5 表示去掉当前指令的差值
        }
        operand->value = v;
        return;
    }

    // jmp 指令
    operand->type = ASM_OPERAND_TYPE_UINT8;
    operand->size = BYTE;
    asm_operand_uint8_t *v = NEW(asm_operand_uint8_t);
    v->value = (uint8_t) (rel_diff - 2); // -2 表示去掉当前指令的差值
    operand->value = v;
}


/**
 * 一次遍历
 * @param asm_inst
 */
void linux_elf_amd64_text_inst_build(amd64_asm_inst_t asm_inst, uint64_t *offset) {
    linux_elf_amd64_text_inst_t *inst = linux_elf_amd64_inst_new(asm_inst);

    // 外部标签引用处理
    amd64_asm_operand_t *rel_operand = amd64_asm_symbol_operand(asm_inst);
    if (rel_operand != NULL) {
        // 1. 数据符号引用(直接改写成 0x0(rip))
        // 2. 标签符号引用(在符号表中,表明为内部符号,否则使用 rel32 占位)
        amd64_asm_operand_symbol_t *symbol_operand = rel_operand->value;
        if (linux_elf_amd64_is_call(asm_inst) || linux_elf_amd64_is_jmp(asm_inst)) { // TODO 改成 asm is
            // 引用了内部符号,根据实际距离判断使用 rel32 还是 rel8
            if (table_exist(linux_elf_symbol_table, symbol_operand->name)) {
                elf_symbol_t *symbol = table_get(linux_elf_symbol_table, symbol_operand->name);
                // 计算 offset 并填充
                int rel_diff = (int) *symbol->offset - global_text_offset;
                // call symbol 只有 rel32
                linux_elf_amd64_operand_rewrite_rel(asm_inst, rel_operand, rel_diff); // TODO asm
            } else {
                // 引用了 label 符号，但是符号确不在符号表中
                // 此时使用 rel32 占位，如果是 jmp 指令后续可能需要替换
                linux_elf_amd64_operand_rewrite_rel(asm_inst, rel_operand, 0);
                inst->rel_operand = rel_operand; // 二次遍历时改写
                inst->rel_symbol = symbol_operand->name; // 二次遍历是需要
                // 如果是 jmp 或者 jcc 指令就需要改写
                uint8_t reduce_count = linux_elf_amd64_jmp_reduce_count(asm_inst);
                if (reduce_count > 0) {
                    inst->may_need_reduce = true;
                    inst->reduce_count = reduce_count;
                }
            }
        } else {
            // 数据符号重写(symbol to 0(%rip))
            linux_elf_amd64_var_operand_rewrite(rel_operand);

            // 添加到重定位表
            elf_rel_t *rel = NEW(elf_rel_t);
            rel->name = symbol_operand->name;
            rel->offset = offset; // 引用的具体位置
            rel->addend = -4;
            rel->section = ELF_SECTION_TEXT;
            rel->type = ELF_SYMBOL_TYPE_VAR;
            list_push(linux_elf_rel_list, rel);

            // 如果符号表中不存在就直接添加到符号,请勿重复添加
            elf_symbol_t *symbol = table_get(linux_elf_symbol_table, symbol_operand->name);
            if (symbol == NULL) {
                symbol = NEW(elf_symbol_t);
                symbol->name = symbol_operand->name;
                symbol->type = 0; // 外部符号引用直接 no type
                symbol->section = 0; // 外部符号，所以这些信息都没有
                symbol->offset = 0;
                symbol->size = 0;
                symbol->is_rel = true; // 是否为外部引用符号(避免重复添加)
                symbol->is_local = false; // 局部 label 在生成符号表的时候可以忽略
                linux_elf_symbol_insert(symbol);
            }

        }
    }

    inst->data = amd64_opcode_encoding(asm_inst, &inst->count);
    global_text_offset += inst->count; // advance global offset

    inst->offset = offset;
    inst->asm_inst = asm_inst;

    // 注册 elf_text_inst_t 到 linux_elf_text_inst_list 和 elf_text_table 中
    list_push(linux_elf_amd64_text_insts, inst);
}

/**
 * 指令重写， jmp/jcc rel32 重写为 rel8
 * 倒推符号表，如果找到符号占用引用则记录位置
 */
void linux_elf_amd64_confirm_text_rel(string name) {
    if (list_empty(linux_elf_amd64_text_insts)) {
        return;
    }

    list_node *current = list_last(linux_elf_amd64_text_insts); // rear 为 empty 占位
    uint8_t reduce_count = 0;
    // 从尾部像前找, 找到超过 128 即可
    while (true) {
        linux_elf_amd64_text_inst_t *inst = current->value;
        if ((global_text_offset - reduce_count - *inst->offset) > 128) {
            break;
        }

        if (inst->may_need_reduce && str_equal(inst->rel_symbol, name)) {
            reduce_count += inst->reduce_count;
        }

        // current 保存当前值
        if (current->prev == NULL) {
            break;
        }
        current = current->prev;
    }

    if (reduce_count == 0) {
        return;
    }

    // 这一行非常关键
    uint64_t *offset = NEW(uint64_t);
    *offset = *(((linux_elf_text_inst_t *) current->value)->offset);
    // 从 current 开始，从左往右做 rewrite
    while (current->value != NULL) {
        linux_elf_amd64_text_inst_t *inst = current->value;
        if (inst->may_need_reduce && str_equal(inst->rel_symbol, name)) {
            // 重写 inst 指令 rel32 为 rel8
            // jmp 的具体位置可以不计算，等到二次遍历再计算
            // 届时符号表已经全部收集完毕
            linux_elf_amd64_rewrite_text_rel(inst);
        }
        *inst->offset = *offset;
        *offset += inst->count; // 重新计算当前 offset
        current = current->next;
    }

    // 最新的 offset
    global_text_offset = *offset;
}

/**
 * inst rel32 to rel8
 * @param t
 */
void linux_elf_amd64_rewrite_text_rel(linux_elf_amd64_text_inst_t *t) {
    asm_operand_uint8_t *operand = NEW(asm_operand_uint8_t);
    operand->value = 0; // 仅占位即可
    t->asm_inst.count = 1;
    t->asm_inst.operands[0]->type = ASM_OPERAND_TYPE_UINT8;
    t->asm_inst.operands[0]->size = BYTE;
    t->asm_inst.operands[0]->value = operand;
    t->data = amd64_opcode_encoding(t->asm_inst, &t->count);
    t->may_need_reduce = false;
}

/**
 * 遍历  linux_elf_text_inst_list 如果其存在 rel_symbol,即符号引用
 * 则判断其是否在符号表中，如果其在符号表中，则填充指令 value 部分(此时可以选择重新编译)
 * 如果其依旧不在符号表中，则表示其引用了外部符号，此时直接添加一条 rel 记录即可
 * @param elf_text_inst_list
 */
void linux_elf_adm64_text_inst_list_second_build() {
    if (list_empty(linux_elf_amd64_text_insts)) {
        return;
    }

    list_node *current = linux_elf_amd64_text_insts->front;
    while (current->value != NULL) {
        linux_elf_amd64_text_inst_t *inst = current->value;
        if (inst->rel_symbol == NULL) {
            current = current->next;
            continue;
        }

        // 计算 rel
        elf_symbol_t *symbol = table_get(linux_elf_symbol_table, inst->rel_symbol);
        if (symbol != NULL && !symbol->is_rel) {
            int rel_diff = *symbol->offset - *inst->offset;

            linux_elf_amd64_operand_rewrite_rel(inst->asm_inst, inst->rel_operand, rel_diff);

            // 重新 encoding 指令
            inst->data = amd64_opcode_encoding(inst->asm_inst, &inst->count);
        } else {
            // 二次扫描都没有在符号表中找到
            // 说明引用了外部 label 符号(是否需要区分引用的外部符号的类型不同？section 填写的又是什么段？)
            // jmp or call rel32
            elf_rel_t *rel = NEW(elf_rel_t);
            rel->name = inst->rel_symbol;
            *inst->offset += 1; // 定位到实际需要 rel 的位置，而不是 inst offset
            rel->offset = inst->offset; // 实际引用位置
            rel->addend = -4; // 符号表如果都使用 rip,则占 4 个偏移
            rel->section = ELF_SECTION_TEXT;
            rel->type = ELF_SYMBOL_TYPE_FN;
            list_push(linux_elf_rel_list, rel);

            // 添加到符号表表
            if (symbol == NULL) {
                symbol = NEW(elf_symbol_t);
                symbol->name = inst->rel_symbol;
                symbol->type = 0; // 外部符号引用直接 no type ?
                symbol->section = 0;
                symbol->offset = 0;
                symbol->size = 0;
                symbol->is_rel = true; // 是否为外部引用符号(避免重复添加)
                symbol->is_local = false; // 局部 label 在生成符号表的时候可以忽略
                linux_elf_symbol_insert(symbol); // 插入 linux 维度 elf symbol 表
            }
        }
        current = current->next;
    }
}

void elf_text_inst_list_build(list *inst_list) {
    if (list_empty(inst_list)) {
        return;
    }
    list_node *current = inst_list->front;
    uint64_t *offset = linux_elf_amd64_current_text_offset();
    while (current->value != NULL) {
        amd64_asm_inst_t *inst = current->value;
        if (str_equal(inst->name, "label")) {
            elf_text_label_build(*inst, offset);
            current = current->next;
            continue;
        }
        linux_elf_amd64_text_inst_build(*inst, offset);
        offset = linux_elf_amd64_current_text_offset();

        current = current->next;
    }
}

void elf_text_label_build(amd64_asm_inst_t asm_inst, uint64_t *offset) {
    // text 中唯一需要注册到符号表的数据, 且不需要编译进 elf_text_item
    amd64_asm_operand_symbol_t *s = asm_inst.operands[0]->value;
    elf_symbol_t *symbol = NEW(elf_symbol_t);
    symbol->name = s->name;
    symbol->type = ELF_SYMBOL_TYPE_FN;
    symbol->section = ELF_SECTION_TEXT;
    symbol->size = 0;
    symbol->is_rel = false;
    symbol->is_local = s->is_local; // 局部 label 在生成符号表的时候可以忽略

    linux_elf_amd64_confirm_text_rel(symbol->name);

    // confirm 后可能会产生 offset 修正,所以需要在 confirm 之后再确定当前 offset
    *offset = global_text_offset;
    symbol->offset = offset; // symbol 和 inst 共用 offset
    linux_elf_symbol_insert(symbol);
}

uint64_t *linux_elf_amd64_current_text_offset() {
    uint64_t *offset = NEW(uint64_t);
    *offset = global_text_offset;
    return offset;
}

list *linux_elf_amd64_insts_build(list *insts) {
    linux_elf_amd64_text_insts = list_new();

    elf_text_inst_list_build(insts);
    linux_elf_adm64_text_inst_list_second_build();

    // 转换成 elf text inst 标准格式返回
    list *text_inst_list = list_new();
    list_node *current = linux_elf_amd64_text_insts->front;
    while (current->value != NULL) {
        linux_elf_amd64_text_inst_t *item = current->value;

        linux_elf_text_inst_t *text_inst = NEW(linux_elf_text_inst_t);
        text_inst->count = item->count;
        text_inst->data = item->data;
        text_inst->offset = item->offset;
        list_push(text_inst_list, text_inst);

        current = current->next;
    }

    return text_inst_list;
}

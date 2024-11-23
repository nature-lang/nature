#include "utils/stack.h"
#include "lir.h"


closure_t *lir_closure_new(ast_fndef_t *fndef) {
    closure_t *c = NEW(closure_t);

    if (fndef->linkid) {
        c->linkident = fndef->linkid;
    } else {
        c->linkident = fndef->symbol_name;
    }

    c->operations = linked_new();
    c->text_count = 0;
    c->asm_operations = slice_new();
    c->asm_build_temps = slice_new();
    c->asm_symbols = slice_new();
    c->entry = NULL;
    c->var_defs = slice_new();
    c->blocks = slice_new(); // basic_block_t

    c->break_targets = stack_new();
    c->continue_labels = stack_new();
    c->break_labels = stack_new();

    c->ssa_globals = slice_new();
    c->ssa_globals_table = table_new();
    c->ssa_var_blocks = table_new();
    c->ssa_var_block_exists = table_new();

    c->match_has_ret = table_new();

    c->interval_table = table_new();

    // 在需要使用的时候再初始化
//    c->closure_vars = slice_new();
//    c->closure_var_table = table_new();

    c->stack_offset = 0;
    c->stack_vars = slice_new();
    c->loop_count = 0;
    c->loop_ends = slice_new();
    c->loop_headers = slice_new();

    c->interval_count = alloc_reg_count() + 1;

    fndef->closure = c;
    c->fndef = fndef;

    c->stack_gc_bits = bitmap_new(1024);
    return c;
}

lir_operand_t *lir_reg_operand(uint8_t index, type_kind kind) {
    reg_t *reg = reg_select(index, kind);
    assert(reg);
    return operand_new(LIR_OPERAND_REG, reg);
}

linked_t *lir_memory_mov(module_t *m, uint16_t size, lir_operand_t *dst, lir_operand_t *src) {
    linked_t *result = linked_new();
    uint16_t remind = size;
    uint16_t offset = 0;
    while (remind > 0) {
        uint16_t count = 0;
        uint16_t item_size = 0; // unit byte
        type_kind kind;
        if (remind >= QWORD) {
            kind = TYPE_UINT64;
            item_size = QWORD;
        } else if (remind >= DWORD) {
            kind = TYPE_UINT32;
            item_size = DWORD;
        } else if (remind >= WORD) {
            kind = TYPE_UINT16;
            item_size = WORD;
        } else {
            kind = TYPE_UINT8;
            item_size = BYTE;
        }

        count = remind / item_size;
        for (int i = 0; i < count; ++i) {
            lir_operand_t *temp_var = temp_var_operand(m, type_kind_new(kind));
            lir_operand_t *temp_src = indirect_addr_operand(m, type_kind_new(kind), src, offset);
            lir_operand_t *temp_dst = indirect_addr_operand(m, type_kind_new(kind), dst, offset);
            linked_push(result, lir_op_move(temp_var, temp_src));
            linked_push(result, lir_op_move(temp_dst, temp_var));
            offset += item_size;
        }

        remind -= count * item_size;
    }

    return result;
}

#include "cross.h"
#include "utils/stack.h"

closure_t *lir_closure_new(ast_fndef_t *fndef) {
    closure_t *c = NEW(closure_t);
    c->symbol_name = fndef->symbol_name;
    c->closure_name = fndef->closure_name;
    c->operations = linked_new();
    c->text_count = 0;
    c->asm_operations = slice_new();
    c->asm_build_temps = slice_new();
    c->asm_symbols = slice_new();
    c->entry = NULL;
    c->var_defs = slice_new();
    c->blocks = slice_new(); // basic_block_t

    c->for_start_labels = stack_new();
    c->for_end_labels = stack_new();

    c->ssa_globals = slice_new();
    c->ssa_globals_table = table_new();
    c->ssa_var_blocks = table_new();
    c->ssa_var_block_exists = table_new();

    c->interval_table = table_new();

    // 在需要使用的时候再初始化
//    c->closure_vars = slice_new();
//    c->closure_var_table = table_new();

    c->stack_offset = 0;
    c->stack_vars = slice_new();
    c->loop_count = 0;
    c->loop_ends = slice_new();
    c->loop_headers = slice_new();

    c->interval_count = cross_alloc_reg_count() + 1;
    c->line = fndef->line;
    c->column = fndef->column;

    fndef->closure = c;
    return c;
}

lir_operand_t *reg_operand(uint8_t index, type_kind kind) {
    reg_t *reg = cross_reg_select(index, kind);
    assert(reg);
    return operand_new(LIR_OPERAND_REG, reg);
}

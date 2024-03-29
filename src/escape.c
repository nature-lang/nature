#include "escape.h"
#include "lir.h"

void escape(closure_t *c) {
    LINKED_FOR(c->operations) {
        lir_op_t *op = LINKED_VALUE();
        if (op->code != LIR_OPCODE_ALLOC) {
            continue;
        }

        lir_operand_t *operand = op->output;
        assert(operand->assert_type == LIR_OPERAND_VAR);
        lir_var_t *var = operand->value;
        symbol_t *s = symbol_table_get(var->ident);
        assert(s->type == SYMBOL_VAR);
        if (!s->is_local) {
            continue;
        }

        ast_var_decl_t *var_decl = s->ast_value;
        assert(var_decl->type.kind == TYPE_STRUCT || var_decl->type.kind == TYPE_ARR);
        bool in_heap;
        if (var_decl->type.kind == TYPE_STRUCT) {
            in_heap = var_decl->type.struct_->in_heap;
        } else {
            in_heap = var_decl->type.array->in_heap;
        }

        // TODO
        if (in_heap) {

        } else {
//            lir_op_t* op = lir_stack_alloc(c,  var_decl->type, operand);
        }
    }
}

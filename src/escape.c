#include "escape.h"
#include "lir.h"

void escape(closure_t *c) {
    linked_t *operations = linked_new();
    LINKED_FOR(c->operations) {
        lir_op_t *op = LINKED_VALUE();
        if (op->code != LIR_OPCODE_ALLOC) {
            linked_push(operations, op);
            continue;
        }

        lir_operand_t *target = op->output;
        assert(target->assert_type == LIR_OPERAND_VAR);
        lir_var_t *var = target->value;
        symbol_t *s = symbol_table_get(var->ident);
        assert(s->type == SYMBOL_VAR);
        if (!s->is_local) {
            linked_push(operations, op);
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

        if (in_heap) {
            // 调用 gc_malloc 分配内存指针
            uint64_t rtype_hash = ct_find_rtype_hash(var->type);

            // 更新类型避免在 lower 被识别成 struct 进行 amd64 下的特殊值传递
            var->type = type_kind_new(TYPE_VOID_PTR);

            lir_op_t *malloc_op = lir_rtcall(RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash));
            linked_push(operations, malloc_op);

            lir_op_t *hook_op = lir_rtcall(RT_CALL_POST_RTCALL_HOOK, NULL, 1, string_operand(RT_CALL_GC_MALLOC));
            linked_push(operations, hook_op);
        } else {
            lir_op_t *new_op = lir_stack_alloc(c, var_decl->type, target);
            linked_push(operations, new_op);
        }
    }

    c->operations = operations;
}

#include <string.h>
#include "compiler.h"
#include "semantic/infer.h"
#include "src/symbol/symbol.h"
#include "utils/error.h"
#include "src/debug/debug.h"
#include "utils/helper.h"
#include "stdio.h"

int compiler_line = 0;

lir_opcode_t ast_expr_operator_transform[] = {
        [AST_EXPR_OPERATOR_ADD] = LIR_OPCODE_ADD,
        [AST_EXPR_OPERATOR_SUB] = LIR_OPCODE_SUB,
        [AST_EXPR_OPERATOR_MUL] = LIR_OPCODE_MUL,
        [AST_EXPR_OPERATOR_DIV] = LIR_OPCODE_DIV,
        [AST_EXPR_OPERATOR_REM] = LIR_OPCODE_REM,

        [AST_EXPR_OPERATOR_LT] = LIR_OPCODE_SLT,
        [AST_EXPR_OPERATOR_LTE] = LIR_OPCODE_SLE,
        [AST_EXPR_OPERATOR_GT] = LIR_OPCODE_SGT,
        [AST_EXPR_OPERATOR_GTE] = LIR_OPCODE_SGE,
        [AST_EXPR_OPERATOR_EQ_EQ] = LIR_OPCODE_SEE,
        [AST_EXPR_OPERATOR_NOT_EQ] = LIR_OPCODE_SNE,

        [AST_EXPR_OPERATOR_NOT] = LIR_OPCODE_NOT,
        [AST_EXPR_OPERATOR_NEG] = LIR_OPCODE_NEG,
};


static lir_operand_t *type_convert(module_t *m, lir_operand_t *source, ast_expr expr) {
    // 待优化目前仅仅服务于 printf
    if (expr.target_type.kind == TYPE_ANY) {
        // any new 处理 float 时需要转换成 int64 处理
        if (source->assert_type == LIR_OPERAND_IMM) {
            lir_imm_t *imm = source->value;
            imm->kind = TYPE_INT64;
        }

        lir_operand_t *new_source_var = temp_var_operand(m, expr.target_type);
        // 基于 source 类型计算 element_rtype index
        uint rtype_index = ct_find_rtype_index(expr.type);
        // lir call type, value =>
        OP_PUSH(lir_rt_call(RT_CALL_TRANS_ANY, new_source_var, 2,
                            int_operand(rtype_index), // arg1
                            source)); // arg2
        return new_source_var;
    }

    return source;
}


/**
 * TODO 如果使用了全局符号怎么办？
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *compiler_ident(module_t *m, ast_expr expr) {
    ast_ident *ident = expr.value;
    lir_operand_t *target = lir_new_empty_operand();

    symbol_t *s = symbol_table_get(ident->literal);

    if (s->type == SYMBOL_FN) {
        // label
        target->assert_type = LIR_OPERAND_SYMBOL_LABEL;
        target->value = label_operand(s->ident, s->is_local)->value;
    } else if (s->type == SYMBOL_VAR) {
        ast_var_decl *var = s->ast_value;
        if (s->is_local) {
            target->assert_type = LIR_OPERAND_VAR;
            target->value = lir_var_new(m, ident->literal);
        } else {
            lir_symbol_var_t *symbol = NEW(lir_symbol_var_t);
            symbol->ident = ident->literal;
            symbol->type = var->type.kind;
            target->assert_type = LIR_OPERAND_SYMBOL_VAR;
            target->value = symbol;
        }
    } else {
        assertf(false, "ident %s exception", ident);
    }

    return target;
}

static lir_operand_t *compiler_addr_of(module_t *m, ast_expr expr) {
    ast_addr_of_t *addr_of = expr.value;
    return var_ref_operand(m, compiler_expr(m, *addr_of->expr));
}

// *expr ,expr 的 type 必须是 pointer
static lir_operand_t *compiler_value_of(module_t *m, ast_expr expr) {
    ast_value_of_t *value_of = expr.value;
    assertf(value_of->expr->type.kind == TYPE_POINTER, "value of expr must pointer");
    lir_operand_t *ptr_target = compiler_expr(m, *value_of->expr);

    // 将改地址中的存储的数据取出来
    return lir_indirect_addr_operand(ptr_target, 0);
}

static void compiler_list_assign(module_t *m, lir_operand_t *list_target, lir_operand_t *index_target, ast_expr src) {
    // 取 value 栈指针,如果 value 不是 var， 会自动转换成 var
    lir_operand_t *value_ref = var_ref_operand(m, compiler_expr(m, src));

    // mov $1, -4(%rbp) // 以 var 的形式入栈
    // mov -4(%rbp), rcx // 参数 1, move 将 -4(%rbp) 处的值穿递给了 rcx, 而不是 -4(%rbp) 这个栈地址
    OP_PUSH(lir_rt_call(RT_CALL_LIST_ASSIGN, NULL,
                        3, list_target, index_target, value_ref));
}

/**
 * @param m
 * @param stmt
 */
static void compiler_env_assign(module_t *m, ast_assign_stmt *stmt) {
    ast_env_access *ast = stmt->left.value;
    lir_operand_t *fn_name = string_operand(m->compiler_current->name);
    lir_operand_t *index = int_operand(ast->index);

    lir_operand_t *src_ref = var_ref_operand(m, compiler_expr(m, stmt->right));
    uint64_t size = type_sizeof(stmt->right.type);

    OP_PUSH(lir_rt_call(RT_CALL_ENV_ASSIGN_REF, NULL,
                        4, fn_name, index, src_ref, int_operand(size)));
}


static void compiler_map_assign(module_t *m, ast_assign_stmt *stmt) {
    ast_map_access_t *map_access = stmt->left.value;
    lir_operand_t *map_target = compiler_expr(m, map_access->left);
    lir_operand_t *key_ref = var_ref_operand(m, compiler_expr(m, map_access->key));
    lir_operand_t *value_ref = var_ref_operand(m, compiler_expr(m, stmt->right));
    lir_op_t *call_op = lir_rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
    OP_PUSH(call_op);
}

static void compiler_struct_assign(module_t *m, ast_assign_stmt *stmt) {
    ast_struct_select_t *struct_access = stmt->left.value;
    typeuse_t struct_type = struct_access->left.type;
    lir_operand_t *struct_target = compiler_expr(m, struct_access->left);
    uint64_t offset = type_struct_offset(struct_type.struct_, struct_access->key);
    uint64_t item_size = type_sizeof(struct_access->property->type);

    lir_operand_t *dst_ref = lir_indirect_addr_operand(struct_target, offset);
    lir_operand_t *src_ref = var_ref_operand(m, compiler_expr(m, stmt->right));

    // move by item size
    OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        3, dst_ref, src_ref, int_operand(item_size)));
}

/**
 * ident = expr
 * @param c
 * @param stmt
 */
static void compiler_ident_assign(module_t *m, ast_assign_stmt *stmt) {
    // 如果 left 是 var
    lir_operand_t *src = compiler_expr(m, stmt->right);
    lir_operand_t *dst = compiler_ident(m, stmt->left); // ident
    OP_PUSH(lir_op_move(dst, src));
}


// 将 tuple 按递归解析赋值给 tuple_destr 中声明的所有 var
// 递归将导致优先从左侧进行展开
static void compiler_tuple_destr(module_t *m, ast_tuple_destr *destr, lir_operand_t *tuple_target) {
    uint64_t offset = 0;
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *element = ct_list_value(destr->elements, i);
        // tuple_operand 对应到当前 index 到值
        uint64_t item_size = type_sizeof(element->type);
        offset = align((int64_t) offset, (int64_t) item_size);

        lir_operand_t *dst = temp_var_operand(m, element->type);
        lir_operand_t *dst_ref = var_ref_operand(m, dst);
        lir_operand_t *src_ref = lir_indirect_addr_operand(tuple_target, offset);
        OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                            3, dst_ref, src_ref, int_operand(item_size)));
        lir_operand_t *src = dst;

        // dst 中已经存储了 tuple element compiler 后的值
        if (element->assert_type == AST_VAR_DECL) {
            ast_var_decl *var_decl = element->value;
            dst = var_operand(m, var_decl->ident);
            OP_PUSH(lir_op_move(dst, src));
        } else if (can_assign(element->assert_type)) {
            dst = compiler_expr(m, *element);
            OP_PUSH(lir_op_move(dst, src));
        } else if (element->assert_type == AST_EXPR_TUPLE_DESTR) {
            compiler_tuple_destr(m, element->value, src);
        } else {
            assertf(false, "var tuple destr must var/tuple_destr");
        }
        offset += item_size;
    }
}

/**
 * (a, b, (c[0], d.b)) = expr
 * @param m
 * @param stmt
 */
static void compiler_tuple_destr_stmt(module_t *m, ast_assign_stmt *stmt) {
    ast_tuple_destr *destr = stmt->left.value;
    lir_operand_t *tuple_target = compiler_expr(m, stmt->right);
    compiler_tuple_destr(m, destr, tuple_target);
}

/**
 * var (a, b, (c, d)) = expr
 * @param m
 * @param var_tuple_def
 * @return
 */
static void compiler_var_tuple_def_stmt(module_t *m, ast_var_tuple_def_stmt *var_tuple_def) {
    // 理论上只需要不停的 move 就行了
    lir_operand_t *tuple_target = compiler_expr(m, var_tuple_def->right);
    compiler_tuple_destr(m, var_tuple_def->tuple_destr, tuple_target);
}

/**
 * 这里不包含如 var a = 1 这样的 assign
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
static void compiler_vardef(module_t *m, ast_vardef_stmt *stmt) {
    lir_operand_t *src = compiler_expr(m, stmt->right);
    lir_operand_t *dst = var_operand(m, stmt->var_decl.ident);

    OP_PUSH(lir_op_move(dst, src));
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * (a, b, (a.b, b[0])) = expr
 * @param c
 * @param stmt
 * @return
 */
static void compiler_assign(module_t *m, ast_assign_stmt *stmt) {
    ast_expr left = stmt->left;

    // map assign list[0] = 1
    if (left.assert_type == AST_EXPR_LIST_ACCESS) {
        ast_list_access_t *list_access = stmt->left.value;
        lir_operand_t *list_target = compiler_expr(m, list_access->left);
        lir_operand_t *index_target = compiler_expr(m, list_access->index);
        return compiler_list_assign(m, list_target, index_target, stmt->right);
    }

    // set assign m["a"] = 2
    if (left.assert_type == AST_EXPR_MAP_ACCESS) {
        return compiler_map_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_ENV_ACCESS) {
        return compiler_env_assign(m, stmt);
    }

    // struct assign p.name = "wei"
    if (left.assert_type == AST_EXPR_STRUCT_SELECT) {
        return compiler_struct_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_TUPLE_DESTR) {
        return compiler_tuple_destr_stmt(m, stmt);
    }

    // a = 1
    if (left.assert_type == AST_EXPR_IDENT) {
        return compiler_ident_assign(m, stmt);
    }

    // tuple[0] = 1 x 禁止这种操作
    // set[0] = 1 x 同样进制这种操作，set 只能通过 add 来添加 key
    assertf(left.assert_type != AST_EXPR_TUPLE_ACCESS, "tuple dose not support item assign");
    assertf(false, "dose not support assign to %d", left.assert_type);
}

/**
 * @param c
 * @param var_decl
 * @return
 */
static linked_t *compiler_var_decl(module_t *m, ast_var_decl *var_decl) {
    return linked_new();
}


/**
 * rt_call get count => count
 * for_iterator:
 *  cmp_goto count == 0 to end for
 *  rt_call get key => key
 *  rt_call get value => value // 可选
 *  ....
 *  sub count, 1 => count
 *  goto for:
 * end_for_iterator:
 * @param c
 * @param for_in_stmt
 * @return
 */
static void compiler_for_iterator(module_t *m, ast_for_iterator_stmt *ast) {
    // map or list
    lir_operand_t *iterator_target = compiler_expr(m, ast->iterate);

//    lir_operand_t *length_target = temp_var_operand(c, type_base_new(TYPE_INT));
    uint64_t rtype_index = ct_find_rtype_index(ast->iterate.type);

    lir_operand_t *cursor_operand = temp_var_operand(m, type_base_new(TYPE_INT));
    OP_PUSH(lir_op_move(cursor_operand, int_operand(0)));

    // make label
    lir_op_t *for_start_label = lir_op_unique_label(m, FOR_ITERATOR_IDENT);
    lir_op_t *for_end_label = lir_op_unique_label(m, FOR_END_ITERATOR_IDENT);

    // set label
    OP_PUSH(for_start_label);

    lir_operand_t *key_target = operand_new(LIR_OPERAND_VAR, lir_var_new(m, ast->key.ident));
    OP_PUSH(lir_rt_call(
            RT_CALL_ITERATE_NEXT_KEY, key_target, 3, iterator_target, rtype_index, cursor_operand));

    // beq length == 0? for_end_label
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, int_operand(0),
                       key_target, lir_copy_label_operand(for_end_label->output)));

    // 添加 continue label
    OP_PUSH(lir_op_unique_label(m, CONTINUE_IDENT));

    // gen value
    if (ast->value) {
        lir_operand_t *value_target = operand_new(LIR_OPERAND_VAR, lir_var_new(m, ast->value->ident));
        OP_PUSH(lir_rt_call(
                RT_CALL_ITERATE_NEXT_VALUE, value_target, 3, iterator_target, rtype_index, cursor_operand));

    }
    // block
    compiler_block(m, ast->body);

    // sub count, 1 => count
    lir_op_t *add_op = lir_op_new(
            LIR_OPCODE_ADD,
            cursor_operand,
            int_operand(1),
            cursor_operand);

    OP_PUSH(add_op);

    // goto for start
    OP_PUSH(lir_op_bal(for_start_label->output));

    OP_PUSH(for_end_label);
}


/**
 *
 * @param c
 * @param ast
 */
static void compiler_for_cond(module_t *m, ast_for_cond_stmt *ast) {
    lir_op_t *for_start = lir_op_unique_label(m, FOR_COND_IDENT);
    OP_PUSH(for_start);
    lir_operand_t *for_end_operand = label_operand(unique_ident(m, FOR_COND_END_IDENT), true);

    lir_operand_t *condition_target = compiler_expr(m, ast->condition);
    lir_op_t *cmp_goto = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), condition_target, for_end_operand);

    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_unique_label(m, CONTINUE_IDENT));
    compiler_block(m, ast->body);

    // bal => goto
    OP_PUSH(lir_op_bal(for_start->output));

    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));
}

static void compiler_for_tradition(module_t *m, ast_for_tradition_stmt *ast) {
    // init
    compiler_stmt(m, ast->init);

    lir_op_t *for_start = lir_op_unique_label(m, FOR_TRADITION_IDENT);
    OP_PUSH(for_start);

    lir_operand_t *for_end_operand = label_operand(unique_ident(m, FOR_TRADITION_END_IDENT), true);

    // cond -> for_end
    lir_operand_t *cond_target = compiler_expr(m, ast->cond);
    lir_op_t *cmp_goto = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), cond_target, for_end_operand);
    OP_PUSH(cmp_goto);

    // continue
    OP_PUSH(lir_op_unique_label(m, CONTINUE_IDENT));

    // block
    compiler_block(m, ast->body);

    // update
    compiler_stmt(m, ast->update);

    // bal for_start_label
    OP_PUSH(lir_op_bal(for_start->output));

    // label for_end
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));
}

static void compiler_return(module_t *m, ast_return_stmt *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *target = compiler_expr(m, *ast->expr);
        lir_op_t *return_op = lir_op_new(LIR_OPCODE_RETURN, target, NULL, NULL);
        OP_PUSH(return_op); // return op 只是做了个 mov result -> rax 的操作
    }

    OP_PUSH(lir_op_bal(label_operand(m->compiler_current->end_label, false)));
}

static void compiler_if(module_t *m, ast_if_stmt *if_stmt) {
    // 编译 condition
    lir_operand_t *condition_target = compiler_expr(m, if_stmt->condition);

    // 判断结果是否为 false, false 对应 else
    lir_operand_t *false_target = bool_operand(false);
    lir_operand_t *end_label_operand = label_operand(unique_ident(m, END_IF_IDENT), true);
    lir_operand_t *alternate_label_operand = label_operand(unique_ident(m, ALTERNATE_IF_IDENT), true);

    lir_op_t *cmp_goto;
    if (if_stmt->alternate->count == 0) {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(end_label_operand));
    } else {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(alternate_label_operand));
    }
    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_unique_label(m, CONTINUE_IDENT));

    // 编译 consequent block
    compiler_block(m, if_stmt->consequent);
    OP_PUSH(lir_op_bal(end_label_operand));

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, alternate_label_operand));
        compiler_block(m, if_stmt->alternate);
    }

    // 追加 end_if 标签
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_label_operand));
}

/**
 * 1.0 函数参数使用 param var 存储,按约定从左到右(code.result 为 param, code.first 为实参)
 * 1.1 code.operand 模仿 phi body 弄成列表的形式！
 * 2. 目前编译依旧使用 var，所以不需要考虑寄存器溢出
 * 3. 函数返回结果存储在 target 中
 *
 * call as, param => result
 * @param c
 * @param expr
 * @return
 */
static lir_operand_t *compiler_call(module_t *m, ast_expr expr) {
    ast_call *call = expr.value;

    lir_operand_t *target = NULL;
    // TYPE_VOID 是否有返回值
    if (call->return_type.kind != TYPE_VOID) {
        target = temp_var_operand(m, call->return_type);
    }


    lir_operand_t *base_target = compiler_expr(m, call->left);

    slice_t *params = slice_new();
    type_fn_t *formal_fn = call->left.type.fn;

    // call 所有的参数都丢到 params 变量中
    for (int i = 0; i < formal_fn->formal_types->length; ++i) {
        ast_expr *param_expr = ct_list_value(call->actual_params, i);

        lir_operand_t *param_operand = compiler_expr(m, *param_expr);

        if (!formal_fn->rest || i < formal_fn->formal_types->length - 1) {
            slice_push(params, param_operand);
            continue;
        }

        int rest_index = i;
        typeuse_t *rest_type = ct_list_value(formal_fn->formal_types, i);
        assertf(rest_type->kind == TYPE_LIST, "rest param must list type");

        // actual 剩余的所有参数都需要用一个数组收集起来，并写入到 target_operand 中
        lir_operand_t *rest_target = temp_var_operand(m, *rest_type);
        lir_operand_t *rtype_index = int_operand(ct_find_rtype_index(*rest_type));
        lir_operand_t *element_index = int_operand(ct_find_rtype_index(rest_type->list->element_type));
        lir_operand_t *capacity = int_operand(0);
        OP_PUSH(lir_rt_call(RT_CALL_LIST_NEW, rest_target, 3, rtype_index, element_index, capacity));

        for (int j = i; j < call->actual_params->length; ++j) {
            ct_list_value(call->actual_params, j);

            // 将栈上的地址传递给 list 即可,不需要管栈中存储的值
            lir_operand_t *param_ref = var_ref_operand(m, param_operand);
            OP_PUSH(lir_rt_call(RT_CALL_LIST_PUSH, NULL, 2, rest_target, param_ref));
        }

        slice_push(params, rest_target);
        break;
    }

    // call base_target,params -> target
    lir_op_t *call_op = lir_op_new(LIR_OPCODE_CALL, base_target,
                                   operand_new(LIR_OPERAND_ACTUAL_PARAMS, params), target);

    // 触发 call 指令, 结果存储在 target 指令中
    OP_PUSH(call_op);

    // 判断 call 是否 throw 了 error 到
    lir_operand_t *has_errort = temp_var_operand(m, type_base_new(TYPE_BOOL));
    OP_PUSH(lir_rt_call(RT_CALL_PROCESSOR_HAS_ERRORT, has_errort, 0));

    // 编译时判断是否有 catch 当前 call expr, 如果存在 catch,则无论如何都不进行 beq
    if (!call->catch) {
        // beq has_errort,true -> fn_end_label
        OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, bool_operand(true), has_errort,
                           label_operand(m->compiler_current->end_label, false)));
    }

    return target;
}


static lir_operand_t *compiler_binary(module_t *m, ast_expr expr) {
    ast_binary_expr *binary_expr = expr.value;

    lir_opcode_t type = ast_expr_operator_transform[binary_expr->operator];

    lir_operand_t *left_target = compiler_expr(m, binary_expr->left);
    lir_operand_t *right_target = compiler_expr(m, binary_expr->right);
    lir_operand_t *result_target = temp_var_operand(m, expr.type);

    OP_PUSH(lir_op_new(type, left_target, right_target, result_target));

    return result_target;
}

/**
 * - (1 + 1)
 * NOT first_param => result_target
 * @param c
 * @param expr
 * @param result_target
 * @return
 */
static lir_operand_t *compiler_unary(module_t *m, ast_expr expr) {
    ast_unary_expr *unary_expr = expr.value;

    lir_operand_t *target = temp_var_operand(m, expr.type);

    lir_operand_t *first = compiler_expr(m, unary_expr->operand);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_EXPR_OPERATOR_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        imm->int_value = -imm->int_value;
        // move 操作即可
        OP_PUSH(lir_op_move(target, first));
        return target;
    }

    assertf(unary_expr->operator != AST_EXPR_OPERATOR_IA, "not support IA op");

    lir_opcode_t type = ast_expr_operator_transform[unary_expr->operator];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    OP_PUSH(unary);

    return target;
}

/**
 * int a = list[0]
 * string s = list[1]
 */
static lir_operand_t *compiler_list_access(module_t *m, ast_expr expr) {
    ast_list_access_t *ast = expr.value;

    lir_operand_t *list_target = compiler_expr(m, ast->left);
    lir_operand_t *index_target = compiler_expr(m, ast->index);

    lir_operand_t *result = temp_var_operand(m, expr.type);
    // 读取 result 的指针地址，给到 access 进行写入
    lir_operand_t *result_ref = var_ref_operand(m, result);

    OP_PUSH(lir_rt_call(RT_CALL_LIST_ACCESS, NULL,
                        3, list_target, index_target, result_ref));

    return result;
}

/**
 * origin [1, foo, bar(), car.done]
 * call runtime.make_list => t1
 * move 1 => t1[0]
 * move foo => t1[1]
 * move bar() => t1[2]
 * move car.done => t1[3]
 * move t1 => target
 * @param c
 * @param new_list
 * @param target
 * @return
 */
static lir_operand_t *compiler_list_new(module_t *m, ast_expr expr) {
    ast_list_new *ast = expr.value;

    lir_operand_t *list_target = temp_var_operand(m, expr.type);

    type_list_t *list_decl = ast->type.list;
    // call list_new
    lir_operand_t *rtype_index = int_operand(ct_find_rtype_index(ast->type));

    lir_operand_t *element_index = int_operand(ct_find_rtype_index(list_decl->element_type));

    lir_operand_t *capacity = int_operand(0);

    // 传递 list element type size 或者自己计算出来也行
    lir_op_t *call_op = lir_rt_call(RT_CALL_LIST_NEW, list_target, 3,
                                    rtype_index, element_index, capacity);
    OP_PUSH(call_op);

    // 值初始化 assign
    for (int i = 0; i < ast->values->length; ++i) {
        ast_expr *item_expr = ct_list_value(ast->values, i);
        lir_operand_t *index_target = int_operand(i);
        compiler_list_assign(m, list_target, index_target, *item_expr);
    }

    return list_target;
}

/**
 * 1. 根据 c->env_name 得到 base_target   call GET_ENV
 * var a = b + 3 // 其中 b 是外部环境变量,需要改写成 GET_ENV
 * b = 12 + c  // 类似这样对外部变量的重新赋值操作，此时 b 的访问直接改成了
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_env_access(module_t *m, ast_expr expr) {
    ast_env_access *ast = expr.value;
    lir_operand_t *fn_name = string_operand(m->compiler_current->name);
    lir_operand_t *index = int_operand(ast->index);

    lir_operand_t *result = temp_var_operand(m, expr.type);
    // 读取 result 的指针地址，给到 access 进行写入
    lir_operand_t *dst_ref = var_ref_operand(m, result);

    uint64_t size = type_sizeof(expr.type);

    OP_PUSH(lir_rt_call(RT_CALL_ENV_ACCESS_REF, NULL,
                        4, fn_name, index, dst_ref, int_operand(size)));

    return result;
}

/**
 * foo.bar
 * foo[0].bar
 * foo.bar.car
 * 证明非变量
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_map_access(module_t *m, ast_expr expr) {
    ast_map_access_t *ast = expr.value;


    // compiler base address left_target
    lir_operand_t *map_target = compiler_expr(m, ast->left);
    typeuse_t type_map_decl = ast->left.type;

    // compiler key to temp var
    lir_operand_t *key_target_ref = var_ref_operand(m, compiler_expr(m, ast->key));
    lir_operand_t *value_target = temp_var_operand(m, type_map_decl.map->value_type);
    lir_operand_t *value_target_ref = var_ref_operand(m, value_target);

    // runtime get slot by temp var runtime.map_offset(base, "key")
    lir_op_t *call_op = lir_rt_call(RT_CALL_MAP_ACCESS, NULL,
                                    3, map_target, key_target_ref, value_target_ref);
    OP_PUSH(call_op);

    return value_target;
}


/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *compiler_set_new(module_t *m, ast_expr expr) {
    ast_set_new *ast = expr.value;
    typeuse_t typedecl = expr.type;

    uint64_t rtype_index = ct_find_rtype_index(typedecl);
    uint64_t key_index = ct_find_rtype_index(typedecl.map->key_type);

    lir_operand_t *set_target = temp_var_operand(m, expr.type);
    lir_op_t *call_op = lir_rt_call(RT_CALL_SET_NEW, set_target,
                                    2, int_operand(rtype_index), int_operand(key_index));
    OP_PUSH(call_op);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->keys->length; ++i) {
        ast_map_element *element = ct_list_value(ast->keys, i);
        ast_expr key_expr = element->key;
        lir_operand_t *key_ref = var_ref_operand(m, compiler_expr(m, key_expr));


        call_op = lir_rt_call(RT_CALL_SET_ADD, NULL, 2, set_target, key_ref);
        OP_PUSH(call_op);
    }

    return set_target;
}

/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *compiler_map_new(module_t *m, ast_expr expr) {
    ast_map_new *ast = expr.value;
    typeuse_t map_type = expr.type;

    uint64_t map_rtype_index = ct_find_rtype_index(map_type);
    uint64_t key_index = ct_find_rtype_index(map_type.map->key_type);
    uint64_t value_index = ct_find_rtype_index(map_type.map->value_type);

    lir_operand_t *map_target = temp_var_operand(m, expr.type);
    lir_op_t *call_op = lir_rt_call(RT_CALL_MAP_NEW, map_target,
                                    3,
                                    int_operand(map_rtype_index),
                                    int_operand(key_index),
                                    int_operand(value_index));
    OP_PUSH(call_op);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_map_element *element = ct_list_value(ast->elements, i);
        ast_expr key_expr = element->key;
        ast_expr value_expr = element->value;
        lir_operand_t *key_ref = var_ref_operand(m, compiler_expr(m, key_expr));
        lir_operand_t *value_ref = var_ref_operand(m, compiler_expr(m, value_expr));

        call_op = lir_rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
        OP_PUSH(call_op);
    }

    return map_target;
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_struct_select(module_t *m, ast_expr expr) {
    ast_struct_select_t *ast = expr.value;

    lir_operand_t *struct_target = compiler_expr(m, ast->left);
    typeuse_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->property->type);
    uint64_t offset = type_struct_offset(t.struct_, ast->key);

    lir_operand_t *dst = temp_var_operand(m, ast->property->type);
    lir_operand_t *dst_ref = var_ref_operand(m, dst);

    lir_operand_t *src_ref = lir_indirect_addr_operand(struct_target, offset);
    OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        3, dst_ref, src_ref, int_operand(item_size)));

    return dst;
}

/**
 * expr = ast_call, but ast_call.left is struct access
 * @param c
 * @param expr
 * @return
 */
static lir_operand_t *compiler_struct_access_call(module_t *m, ast_expr expr) {
    ast_select *ast = expr.value;

    // TODO 这里的 ast->left 的类型可能并不是 struct,expr 自身则是 var 或者 global symbol operand
    lir_operand_t *left_target = compiler_expr(m, ast->left);

    // if left_target == list,

    // 现在 left 的类型可能是 struct 也有可能是 list 或者 map
    // list 就算打死我我也不可能编译出一个 memory_struct_t 出来? 现在问题的关键是 var 中存储的是 memory_list_t
    // 此时不能走基于 memory_struct_t 的 offset 方案读取 fn 的基础地址
    // 那如何才能读取 list 的 push 或者 pop 或者 delete 等等属性呢?
    // 假如做一层抽象,让 left_target == memory_struct_t(list),其他部分继续走
    // 这需要服务端配合定义一个 typeuse_struct_t 名字叫 list, 并且写入到符号表中
    // 由于该 list 没有进行过实际的 struct_new 操作,所以需要在 runtime_init 时进行相关的 struct_new?
    // 通过固定的 rtype_index 找到 struct 并且 new 即可
    // 在 runtime 中 init 回面临一个指针存储的问题.需要通过 rt_call 获得 list 的 typeuse_struct_t? 好像也没啥问题
    // 至此 list 的 typeuse_struct_t 的 typeuse_t 和 memory_struct_t 都有了,可以通过一般方法进行 struct call

    // TODO left 的类型决定了怎么 call, 如果是 struct access 则需要做特殊逻辑支持。

    // 假设 left 是
    // rt_call list_push(left:memory_list_t, )
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_tuple_access(module_t *m, ast_expr expr) {
    ast_tuple_access_t *ast = expr.value;

    lir_operand_t *tuple_target = compiler_expr(m, ast->left);
    typeuse_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->element_type);
    uint64_t offset = type_tuple_offset(t.tuple, ast->index);

    lir_operand_t *dst = temp_var_operand(m, ast->element_type);
    lir_operand_t *dst_ref = var_ref_operand(m, dst);
    lir_operand_t *src_ref = lir_indirect_addr_operand(tuple_target, offset);
    OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        3, dst_ref, src_ref, int_operand(item_size)));

    return dst;
}

/**
 * foo.bar = 1
 *
 * person baz = person {
 *  age = 100
 *  sex = true
 * }
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_struct_new(module_t *m, ast_expr expr) {
    ast_struct_new_t *ast = expr.value;
    lir_operand_t *struct_target = temp_var_operand(m, expr.type);

    typeuse_t type = ast->type;

    uint64_t rtype_index = ct_find_rtype_index(type);

    OP_PUSH(lir_rt_call(RT_CALL_STRUCT_NEW, struct_target,
                        1, int_operand(rtype_index)));

    // 快速赋值,由于 struct 的相关属性都存储在 type 中，所以偏移量等值都需要在前端完成计算
    uint64_t offset = 0;
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *p = ct_list_value(ast->properties, i);

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t item_size = type_sizeof(p->type);
        offset = align((int64_t) offset, (int64_t) item_size);
        assertf(p->right, "struct new property value empty");

        ast_expr *property = p->right;
        // offset(var) var must assign reg
        lir_operand_t *dst_ref = lir_indirect_addr_operand(struct_target, offset);
        lir_operand_t *src_ref = var_ref_operand(m, compiler_expr(m, *property));

        // move by item size
        OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                            3, dst_ref, src_ref, int_operand(item_size)));
        offset += item_size;
    }

    return struct_target;
}


/**
 * var a = (1, a, 1.25)
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_tuple_new(module_t *m, ast_expr expr) {
    ast_tuple_new *ast = expr.value;

    typeuse_t typedecl = expr.type;
    uint64_t rtype_index = ct_find_rtype_index(typedecl);

    lir_operand_t *tuple_target = temp_var_operand(m, expr.type);
    OP_PUSH(lir_rt_call(RT_CALL_TUPLE_NEW, tuple_target,
                        1, int_operand(rtype_index)));

    uint64_t offset = 0;
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_expr *element = ct_list_value(ast->elements, i);

        uint64_t item_size = type_sizeof(element->type);
        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align((int64_t) offset, (int64_t) item_size);

        // offset(var) var must assign reg
        lir_operand_t *dst_ref = lir_indirect_addr_operand(tuple_target, offset);
        lir_operand_t *src_ref = var_ref_operand(m, compiler_expr(m, *element));

        // move by item size
        OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                            3, dst_ref, src_ref, int_operand(item_size)));
        offset += item_size;
    }

    return tuple_target;
}

static lir_operand_t *compiler_catch(module_t *m, ast_expr expr) {
    ast_catch *catch = expr.value;
    typeuse_t tuple_type = expr.type;

    lir_operand_t *call_result_operand = compiler_expr(m, (ast_expr) {
            .assert_type = AST_CALL,
            .type = catch->call->return_type,
            .value = catch->call
    });
    ast_ident *call_result_ident = NEW(ast_ident);
    call_result_ident->literal = ((lir_var_t *) call_result_operand->value)->ident;
    ast_expr call_result_expr = {
            .assert_type= AST_EXPR_IDENT,
            .type = ((lir_var_t *) call_result_operand)->type,
            .value = call_result_ident
    };


    symbol_t *symbol = symbol_table_get(ERRORT_TYPE_IDENT);
    ast_typedef_stmt *typedef_stmt = symbol->ast_value;
    assertf(typedef_stmt->type.status == REDUCTION_STATUS_DONE, "errort type not reduction");

    lir_operand_t *errort_operand = temp_var_operand(m, typedef_stmt->type);
    OP_PUSH(lir_rt_call(RT_CALL_PROCESSOR_REMOVE_ERRORT, errort_operand, 0));

    ast_ident *errort_ident = NEW(ast_ident);
    errort_ident->literal = ((lir_var_t *) errort_operand->value)->ident;
    ast_expr errort_expr = {
            .assert_type= AST_EXPR_IDENT,
            .type = ((lir_var_t *) errort_operand->value)->type,
            .value = errort_ident
    };


    // (call(), error_operand())
    ast_tuple_new *tuple = NEW(ast_tuple_new);
    tuple->elements = NEW(ast_tuple_new);
    ct_list_push(tuple->elements, &call_result_expr);
    ct_list_push(tuple->elements, &errort_expr);

    return compiler_tuple_new(m, (ast_expr) {
            .type = tuple_type,
            .assert_type = AST_EXPR_TUPLE_NEW,
            .value = tuple
    });
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *compiler_literal(module_t *m, ast_expr expr) {
    ast_literal *literal = expr.value;

    switch (literal->kind) {
        case TYPE_STRING: {
            // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
            lir_operand_t *target = temp_var_operand(m, expr.type);
            lir_operand_t *imm_c_string_operand = string_operand(literal->value);
            lir_operand_t *imm_len_operand = int_operand(strlen(literal->value));
            lir_op_t *call_op = lir_rt_call(
                    RT_CALL_STRING_NEW,
                    target,
                    2,
                    imm_c_string_operand,
                    imm_len_operand);
            OP_PUSH(call_op);
            return target;
        }
        case TYPE_RAW_STRING: {
            return string_operand(literal->value);
        }
        case TYPE_INT: {
            return int_operand(atoi(literal->value));
        }
        case TYPE_FLOAT: {
            return float_operand(atof(literal->value));
        }
        case TYPE_BOOL: {
            bool bool_value = false;
            if (strcmp(literal->value, "true") == 0) {
                bool_value = true;
            }
            return bool_operand(bool_value);
        }

        default: {
            assertf(false, "line: %d, cannot compiler literal->code", compiler_line);
        }
    }
    exit(1);
}


/**
 * fndef 到 body 已经编译完成并变成了 label, 此时不需要再递归到 fn body 内部,也不需要调整 m->compiler_current
 * 只需要将 fndef 到 env 写入到 fndef->name 对应到 envs 中即可, 返回值则返回函数到唯一 ident 即可
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *compiler_fndef(module_t *m, ast_expr expr) {
    ast_fndef_t *fndef = expr.value;

    if (fndef->parent_view_envs->length > 0) {
        lir_operand_t *capacity = int_operand(fndef->parent_view_envs->length);
        lir_operand_t *fn_name_operand = string_operand(fndef->name);
        // rt_call env_new(fndef->name, capacity)
        OP_PUSH(lir_rt_call(RT_CALL_ENV_NEW, NULL, 2, fn_name_operand, capacity));

        for (int i = 0; i < fndef->parent_view_envs->length; ++i) {
            ast_expr *item = ct_list_value(fndef->parent_view_envs, i);

            // 此时更新了 envs 中的值
            lir_operand_t *ref = var_ref_operand(m, compiler_expr(m, *item));

            // rt_call env_assign(fndef->name, index_operand lir_operand)
            OP_PUSH(lir_rt_call(RT_CALL_ENV_ASSIGN, NULL, 3, fn_name_operand, int_operand(i), ref));
        }

    }

    return var_operand(m, fndef->name);
}

static void compiler_throw(module_t *m, ast_throw_stmt *stmt) {
    // msg to errort
    symbol_t *symbol = symbol_table_get(ERRORT_TYPE_IDENT);
    ast_typedef_stmt *typedef_stmt = symbol->ast_value;
    assertf(typedef_stmt->type.status == REDUCTION_STATUS_DONE, "errort type not reduction");

    // 构建 struct new  结构
    ast_struct_new_t *errort_struct = NEW(ast_struct_new_t);
    errort_struct->type = typedef_stmt->type;
    errort_struct->properties = ct_list_new(sizeof(struct_property_t));
    struct_property_t property = {
            .type = type_base_new(TYPE_STRING),
            .key = ERRORT_MSG_IDENT,
            .right = &stmt->error,
    };
    ct_list_push(errort_struct->properties, &property);

    // target 是一个 ptr, 指向了一段 memory_struct_t
    lir_operand_t *errort_target = compiler_struct_new(m, (ast_expr) {
            .type = errort_struct->type,
            .assert_type = AST_EXPR_STRUCT_NEW,
            .value = errort_struct
    });

    // attach errort to processor
    OP_PUSH(lir_rt_call(RT_CALL_PROCESSOR_ATTACH_ERRORT, NULL, 1, errort_target));

    // ret
    OP_PUSH(lir_op_bal(label_operand(m->compiler_current->end_label, false)));
}

static void compiler_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            compiler_var_decl(m, stmt->value);
            return;
        }
        case AST_STMT_VAR_DEF: {
            return compiler_vardef(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return compiler_assign(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return compiler_var_tuple_def_stmt(m, stmt->value);
        }
        case AST_STMT_IF: {
            return compiler_if(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return compiler_for_iterator(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return compiler_for_cond(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return compiler_for_tradition(m, stmt->value);
        }
        case AST_FNDEF: {
            compiler_fndef(m, (ast_expr) {
                    .line = stmt->line,
                    .assert_type = stmt->assert_type,
                    .value = stmt->value,
            });
            return;
        }
        case AST_CALL: {
            // stmt 中都 call 都是没有返回值的
            compiler_call(m, (ast_expr) {
                    .line = stmt->line,
                    .assert_type = stmt->assert_type,
                    .value = stmt->value,
            });
            return;
        }
        case AST_STMT_RETURN: {
            return compiler_return(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return compiler_throw(m, stmt->value);
        }
        case AST_STMT_TYPEDEF: {
            return;
        }
        default: {
            assertf(false, "unknown stmt type=%d", stmt->assert_type);
        }
    }
}

compiler_expr_fn expr_fn_table[] = {
        [AST_EXPR_LITERAL] = compiler_literal,
        [AST_EXPR_ADDR_OF] = compiler_addr_of,
        [AST_EXPR_IDENT] = compiler_ident,
        [AST_EXPR_ENV_ACCESS] = compiler_env_access,
        [AST_EXPR_BINARY] = compiler_binary,
        [AST_EXPR_UNARY] = compiler_unary,
        [AST_EXPR_LIST_NEW] = compiler_list_new,
        [AST_EXPR_LIST_ACCESS] = compiler_list_access,
        [AST_EXPR_MAP_NEW] = compiler_map_new,
        [AST_EXPR_MAP_ACCESS] = compiler_map_access,
        [AST_EXPR_STRUCT_NEW] = compiler_struct_new,
        [AST_EXPR_STRUCT_SELECT] = compiler_struct_select,
        [AST_EXPR_TUPLE_NEW] = compiler_tuple_new,
        [AST_EXPR_TUPLE_ACCESS] = compiler_tuple_access,
        [AST_EXPR_SET_NEW] = compiler_set_new,
        [AST_CALL] = compiler_call,
        [AST_FNDEF] = compiler_fndef,
        [AST_EXPR_CATCH] = compiler_catch,
};


static lir_operand_t *compiler_expr(module_t *m, ast_expr expr) {
    // 特殊处理
    compiler_expr_fn fn = expr_fn_table[expr.assert_type];
    assertf(fn, "ast right not support");

    lir_operand_t *source = fn(m, expr);

//    expr.target_type.kind
//    expr.type.kind

    if (expr.type.kind != expr.target_type.kind) {
        source = type_convert(m, source, expr);
    }

    return source;
}


static void compiler_block(module_t *m, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
        ast_stmt *stmt = block->take[i];
        compiler_line = stmt->line;
        m->compiler_line = stmt->line;
#ifdef DEBUG_COMPILER
        debug_stmt("COMPILER", stmt);
#endif
        compiler_stmt(m, stmt);
    }
}


// 看起来是返回了 fn->name ? 但是部分 fn 是没有名称的?
// 比如 fn 在 list[0] 中? 不对 fn 一定有名字,不然怎么能写入到 text 中?

static closure_t *compiler_closure(module_t *m, ast_fndef_t *fndef) {
    // 创建 closure, 并写入到 m module 中
    closure_t *c = lir_closure_new(fndef);
    // 互相关联关系
    m->compiler_current = c;
    c->module = m;
    c->end_label = str_connect("end_", c->name);

    OP_PUSH(lir_op_label(fndef->name, false));

    // 编译 fn param -> lir_var_t*
    slice_t *params = slice_new();
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var_decl = ct_list_value(fndef->formals, i);

        slice_push(params, lir_var_new(c, var_decl->ident));
    }
    OP_PUSH(lir_op_result(LIR_OPCODE_FN_BEGIN, operand_new(LIR_OPERAND_FORMAL_PARAMS, params)));

    compiler_block(m, fndef->body);

    OP_PUSH(lir_op_label(c->end_label, true));

    OP_PUSH(lir_op_new(LIR_OPCODE_FN_END, NULL, NULL, NULL));

    return c;
}

/**
 * @param c
 * @param ast
 * @return
 */
void compiler(module_t *m) {
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        closure_t *closure = compiler_closure(m, fndef);
        slice_push(m->closures, closure);
    }
}

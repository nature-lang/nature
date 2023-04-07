#include <string.h>
#include "compiler.h"
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


static void compiler_list_assign(module_t *m, lir_operand_t *list_target, lir_operand_t *index_target, ast_expr src) {
    // 取 value 栈指针,如果 value 不是 var， 会自动转换成 var
    lir_operand_t *value_ref = var_ref_operand(m, compiler_expr(m, src));

    // mov $1, -4(%rbp) // 以 var 的形式入栈
    // mov -4(%rbp), rcx // 参数 1, move 将 -4(%rbp) 处的值穿递给了 rcx, 而不是 -4(%rbp) 这个栈地址
    OP_PUSH(lir_rt_call(RT_CALL_LIST_ASSIGN, NULL,
                        3, list_target, index_target, value_ref));
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
    ast_instance_select_t *struct_access = stmt->left.value;
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

/**
 * 这里不包含如 var a = 1 这样的 assign
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
static void compiler_var_decl_assign(module_t *m, ast_vardef_stmt *stmt) {
    lir_operand_t *src = compiler_expr(m, stmt->right);
    lir_operand_t *dst = operand_new(LIR_OPERAND_VAR, lir_var_new(m, stmt->var_decl.ident));

    OP_PUSH(lir_op_move(dst, src));
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
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

//    if (left.assert_type == AST_EXPR_ENV_ACCESS) {
//        return compiler_env_assign();
//    }

    // instance assign p.name = "wei"
    if (left.assert_type == AST_EXPR_STRUCT_SELECT) {
        return compiler_struct_assign(m, stmt);
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
 * 1.0.1 code.operand 模仿 phi body 弄成列表的形式！
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
    if (expr.type.kind != TYPE_VOID) {
        target = temp_var_operand(m, expr.type);
    }

    // TODO left 的类型决定了怎么 call, 如果是 struct access 则需要做特殊逻辑支持。
    // TODO list_select()
    // TODO map_select()
    // TODO set()
    // TODO set_select()

    // push 指令所有的物理寄存器入栈
    // 这里增加了无意义的堆栈和符号表,不符合简捷之道
    lir_operand_t *base_target = compiler_expr(m, call->left);

    slice_t *params = slice_new();

    type_fn_t *formal_fn = call->left.type.fn;



    // call 所有的参数都丢到 params 变量中
    for (int i = 0; i < formal_fn->formal_types->length; ++i) {
        ast_expr *param_expr = ct_list_value(call->actual_params, i);

        if (formal_fn->rest_param && i == formal_fn->formal_types->length - 1) {
            
        }

        lir_operand_t *operand = compiler_expr(m, *param_expr);
        slice_push(params, operand);
    }




    // 预编译 spread operand, 避免每一次使用 spread 都编译一次
    lir_operand_t *spread_list_target = NULL;
    if (call->spread_param) {
        // compiler_expr 中已经做了 mov 操作
        spread_list_target = compiler_expr(m, call->actual_params[call->param_count - 1]);
    }


    uint8_t spread_index = 0;
    for (int i = 0; i < formal_fn->formals_count; ++i) {
        lir_operand_t *rest_param_target = NULL;

        // rest
        bool is_rest = formal_fn->rest_param && i == formal_fn->formals_count - 1;
        if (is_rest) {
            // lir array_new(count, size) -> rest_param_target
            typeuse_t rest_param_type = formal_fn->formals_types[formal_fn->formals_count - 1];
            assertf(rest_param_type.kind == TYPE_LIST, "rest param must be list type");

            uint64_t rest_param_rtype_index = ct_find_rtype_index(rest_param_type);


            type_list_t *list_decl = rest_param_type.list;
            // actual 剩余的所有参数都需要用一个数组收集起来，并写入到 target_operand 中
            rest_param_target = temp_var_operand(c, rest_param_type);

            lir_operand_t *rtype_index = int_operand(ct_find_rtype_index(rest_param_type));
            lir_operand_t *element_index = int_operand(ct_find_rtype_index(list_decl->element_type));
            lir_operand_t *capacity = int_operand(0);
            OP_PUSH(lir_rt_call(
                    RT_CALL_LIST_NEW, rest_param_target, // rest_param_target is array_t
                    3, rtype_index, element_index, capacity));

            // param target 目前存储了 array_t 的地址信息
            // 从 i 开始算起，剩余的所有实参都编译并填入到 rest param target 中,
            for (int j = i; j < call->param_count; ++j) {
                bool is_spread = call->spread_param && j == call->param_count - 1;
                if (is_spread) {
                    lir_var_t *spared_target_var = spread_list_target->value;
                    lir_operand_t *spread_rtype_index = int_operand(ct_find_rtype_index(spared_target_var->type));

                    // spread 已经预编译过了
                    if (spread_index > 0) {
                        lir_operand_t *length_target = temp_var_operand(c, type_base_new(TYPE_INT));
                        OP_PUSH(lir_rt_call(RT_CALL_LIST_LENGTH,
                                            length_target,
                                            1,
                                            spread_list_target));


                        // spread 被分割了一部分，所以需要对剩余的 temp target 进行 array_slice 切割
                        // 此方法不会改变现有数组，而是生成一个新的数组, rest 在 fn def 定义，所以先找到定义的 type
                        OP_PUSH(lir_rt_call(RT_CALL_LIST_SPLICE,
                                            spread_list_target, // 切割后的结果保存回 spread_target
                                            4,
                                            spread_rtype_index,
                                            spread_list_target,
                                            int_operand(spread_index),
                                            length_target));
                    }
                    // 此方法不会改变现有数组，而是生成一个新的数组
                    OP_PUSH(lir_rt_call(
                            RT_CALL_LIST_CONCAT,
                            rest_param_target,
                            3,
                            spread_rtype_index,
                            rest_param_target,
                            spread_list_target));

                } else {
                    lir_operand_t *actual_param_target = compiler_expr(c, call->actual_params[j]);

                    // 将栈上的地址传递给 list 即可,不需要管栈中存储的值
                    lir_operand_t *target_ref = var_ref_operand(c, actual_param_target);
                    OP_PUSH(
                            lir_rt_call(RT_CALL_LIST_PUSH,
                                        NULL,
                                        2,
                                        rest_param_target,
                                        target_ref));
                }
            }

            slice_push(params, rest_param_target);
            break;
        }

        // spread call([int] list)
        bool is_spread = call->spread_param && i >= call->param_count - 1;
        if (is_spread) {
            // formal i >= actual param count, 则 actual param count - 1 对应的参数为 spread list,
            // 需要不断的从 spread list 中提取值,并传递到 params 中

            //  1. 读取 spread_target element type
            typeuse_t t = call->actual_params[call->param_count - 1].type;

            lir_operand_t *temp = temp_var_operand(c, t.list->element_type);
            // 读取 result 的指针地址，给到 access 进行写入
            lir_operand_t *temp_ref = var_ref_operand(c, temp);

            OP_PUSH(lir_rt_call(
                    RT_CALL_LIST_ACCESS,
                    NULL,
                    3,
                    spread_list_target,
                    spread_index,
                    temp_ref));

            spread_index += 1;
            slice_push(params, temp);
            continue;
        }

        // common
        rest_param_target = compiler_expr(c, call->actual_params[i]);
        slice_push(params, rest_param_target);
    }

    // return target
    lir_op_t *call_op = lir_op_new(LIR_OPCODE_CALL,
                                   base_target,
                                   operand_new(LIR_OPERAND_ACTUAL_PARAMS, params),
                                   target);

    OP_PUSH(call_op);

    return target;
}

static lir_operand_t *compiler_binary(module_t *m, ast_expr expr) {
    ast_binary_expr *binary_expr = expr.value;

    lir_opcode_t type = ast_expr_operator_transform[binary_expr->operator];

    lir_operand_t *left_target = compiler_expr(c, binary_expr->left);
    lir_operand_t *right_target = compiler_expr(c, binary_expr->right);
    lir_operand_t *result_target = temp_var_operand(c, expr.type);

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

    lir_operand_t *target = temp_var_operand(c, expr.type);

    lir_operand_t *first = compiler_expr(c, unary_expr->operand);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_EXPR_OPERATOR_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        imm->int_value = -imm->int_value;
        // move 操作即可
        OP_PUSH(lir_op_move(target, first));
        return target;
    }

    if (unary_expr->operator == AST_EXPR_OPERATOR_IA) {
        // TODO trans lir_indirect_addr_t
        // 如果 first 都不是指针，那就不给解引用直接报错，只有变量才能是指针类型
//        assertf(first->assert_type == LIR_OPERAND_VAR, "operator IA, but operand not var");
        assertf(false, "not support ia op");
        // 添加引用标识(var 维度，而不是变量维度,而不是 local_var_decl 维度)
//        lir_var_t *var = first->value;
//        var->indirect_addr = true;
//        return first;
    }

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

    lir_operand_t *list_target = compiler_expr(c, ast->left);
    lir_operand_t *index_target = compiler_expr(c, ast->index);

    lir_operand_t *result = temp_var_operand(c, expr.type);
    // 读取 result 的指针地址，给到 access 进行写入
    lir_operand_t *result_ref = var_ref_operand(c, result);

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

    lir_operand_t *list_target = temp_var_operand(c, expr.type);

    type_list_t *list_decl = ast->type.list;
    // call list_new
    lir_operand_t *rtype_index = int_operand(ct_find_rtype_index(ast->type));

    lir_operand_t *element_index = int_operand(ct_find_rtype_index(list_decl->element_type));

    lir_operand_t *capacity = int_operand(0);

    // 传递 list element type size 或者自己计算出来也行
    lir_op_t *call_op = lir_rt_call(
            RT_CALL_LIST_NEW,
            list_target,
            1,
            rtype_index,
            element_index,
            capacity
    );
    OP_PUSH(call_op);

    // 值初始化 assign
    for (int i = 0; i < ast->count; ++i) {
        lir_operand_t *index_target = int_operand(i);
        compiler_list_assign(c, list_target, index_target, ast->values[i]);
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
static lir_operand_t *compiler_env_value(module_t *m, ast_expr expr) {
    ast_env_access *ast = expr.value;
    lir_operand_t *env_name_param = string_operand(c->env_name);
    lir_operand_t *env_index_param = int_operand(ast->index);
    // target 通常就是一个 temp_var
    lir_operand_t *value_point = temp_var_operand(c, type_ptrof(expr.type, 1));
    lir_op_t *call_op = lir_rt_call(
            RT_CALL_ENV_ACCESS,
            value_point,
            2,
            env_name_param,
            env_index_param
    );

    OP_PUSH(call_op);

    return lir_indirect_addr_operand(value_point, 0);
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
    lir_operand_t *map_target = compiler_expr(c, ast->left);
    typeuse_t type_map_decl = ast->left.type;

    // compiler key to temp var
    lir_operand_t *key_target_ref = var_ref_operand(c, compiler_expr(c, ast->key));
    lir_operand_t *value_target = temp_var_operand(c, type_map_decl.map->value_type);
    lir_operand_t *value_target_ref = var_ref_operand(c, value_target);


    // runtime get slot by temp var runtime.map_offset(base, "key")
    lir_op_t *call_op = lir_rt_call(
            RT_CALL_MAP_ACCESS,
            NULL,
            3,
            map_target,
            key_target_ref,
            value_target_ref
    );
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

    lir_operand_t *set_target = temp_var_operand(c, expr.type);
    lir_op_t *call_op = lir_rt_call(RT_CALL_SET_NEW, set_target,
                                    2,
                                    int_operand(rtype_index),
                                    int_operand(key_index));
    OP_PUSH(call_op);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->keys->length; ++i) {
        ast_map_element *element = ct_list_value(ast->keys, i);
        ast_expr key_expr = element->key;
        lir_operand_t *key_ref = var_ref_operand(c, compiler_expr(c, key_expr));


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

    lir_operand_t *map_target = temp_var_operand(c, expr.type);
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
        lir_operand_t *key_ref = var_ref_operand(c, compiler_expr(c, key_expr));
        lir_operand_t *value_ref = var_ref_operand(c, compiler_expr(c, value_expr));


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
static lir_operand_t *compiler_struct_access(module_t *m, ast_expr expr) {
    ast_select *ast = expr.value;

    lir_operand_t *struct_target = compiler_expr(c, ast->left);
    typeuse_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->property.type);
    uint64_t offset = type_struct_offset(t.struct_, ast->key);

    lir_operand_t *dst = temp_var_operand(c, ast->property.type);
    lir_operand_t *dst_ref = var_ref_operand(c, dst);

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
    lir_operand_t *left_target = compiler_expr(c, ast->left);

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

    lir_operand_t *tuple_target = compiler_expr(c, ast->left);
    typeuse_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->element_type);
    uint64_t offset = type_tuple_offset(t.tuple, ast->index);

    lir_operand_t *dst = temp_var_operand(c, ast->element_type);
    lir_operand_t *dst_ref = var_ref_operand(c, dst);
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
    lir_operand_t *struct_target = temp_var_operand(c, expr.type);

    typeuse_t type = ast->type;

    uint64_t rtype_index = ct_find_rtype_index(type);

    OP_PUSH(lir_rt_call(RT_CALL_STRUCT_NEW, struct_target,
                        1, int_operand(rtype_index)));

    // 快速赋值,由于 struct 的相关属性都存储在 type 中，所以偏移量等值都需要在前端完成计算
    uint64_t offset = 0;
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_new_property *struct_property = ct_list_value(ast->properties, i);

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t item_size = type_sizeof(struct_property->type);
        offset = align((int64_t) offset, (int64_t) item_size);

        // offset(var) var must assign reg
        lir_operand_t *dst_ref = lir_indirect_addr_operand(struct_target, offset);
        lir_operand_t *src_ref = var_ref_operand(c, compiler_expr(c, struct_property->value));

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

    lir_operand_t *tuple_target = temp_var_operand(c, expr.type);
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
        lir_operand_t *src_ref = var_ref_operand(c, compiler_expr(c, *element));

        // move by item size
        OP_PUSH(lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                            3, dst_ref, src_ref, int_operand(item_size)));
        offset += item_size;
    }

    return tuple_target;
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
        OP_PUSH(lir_rt_call(RT_CALL_TUPLE_NEW, NULL, 2, fn_name_operand, capacity));

        for (int i = 0; i < fndef->parent_view_envs->length; ++i) {
            ast_expr *item = ct_list_value(fndef->parent_view_envs, i);

            lir_operand_t *ref = var_ref_operand(m, compiler_expr(m, *item));
            // rt_call env_assign(fndef->name, index_operand lir_operand)
            OP_PUSH(lir_rt_call(RT_CALL_ENV_ASSIGN, NULL, 3, fn_name_operand, int_operand(i), ref));
        }

    }

    return var_operand(m, fndef->name);
}


/**
 * target = void () {
 *
 * }
 *
 * info.f 实际存储了什么？
 *
 * 顶层 closure_t 不需要再次捕获外部变量
 * call make_env
 * call set_env => a target
 * call env_value => t1 target
 * @param parent
 * @param ast_closure
 * @return
 */
//static lir_operand_t *compiler_closure(closure_t *parent, ast_expr expr) {
//    ast_closure_t *ast_closure = expr.value;
//    // handle env
//    if (parent && ast_closure->env_list->count > 0) {
//        // 处理 env ---------------
//        // 1. make env_n by count
//        lir_operand_t *env_name_param = string_operand(ast_closure->env_name);
//        lir_operand_t *capacity_param = int_operand(ast_closure->env_list->count);
//        linked_push(parent->operations,
//                    lir_rt_call(RT_CALL_ENV_NEW, NULL, 2, env_name_param, capacity_param));
//
//        // 2. for set ast_ident/ast_env_access to env n
//        for (int i = 0; i < ast_closure->env_list->count; ++i) {
//            ast_expr *env_expr = ast_closure->env_list->take[i];
//            // env_value(indirect_addr operand) or ident(var operand)
//            lir_operand_t *env_target = compiler_expr(parent, *env_expr);
//            assertf(env_target->assert_type == LIR_OPERAND_VAR, "expr_target type not var, cannot use by env set");
//            lir_operand_t *env_point_target = temp_var_operand(parent, type_ptrof(env_expr->type, 1));
//            linked_push(parent->operations, lir_op_new(LIR_OPCODE_LEA, env_target, NULL, env_point_target));
//
//            lir_operand_t *env_index_param = int_operand(i);
//            lir_op_t *call_op = lir_rt_call(
//                    RT_CALL_ENV_ASSIGN,
//                    NULL,
//                    3,
//                    env_name_param,
//                    env_index_param,
//                    env_point_target // value
//            );
//            linked_push(parent->operations, call_op);
//        }
//
//    }
//
//    // new 一个新的 closure_t ---------------
//    closure_t *c = lir_closure_new(ast_closure);
//    c->module = current_module;
//    c->name = ast_closure->fn->name; // analysis 阶段已经进行了唯一名称处理
//    c->end_label = str_connect("end_", c->name);
////    c->parent = parent;
//    slice_push(compiler_closures, c);
//
//    // 添加 label 和 fn begin 入口
//    OP_PUSH(lir_op_label(ast_closure->fn->name, false));
//
//    slice_t *formal_params = slice_new();
//    for (int i = 0; i < ast_closure->fn->param_count; ++i) {
//        ast_var_decl *param = ast_closure->fn->formal_params[i];
//        // var operand 依赖 var_decl 定义的变量
//        lir_var_t *var = lir_var_new(c, param->ident);
//        slice_push(formal_params, var);
//    }
//
//    OP_PUSH(lir_op_new(LIR_OPCODE_FN_BEGIN, NULL, NULL,
//                       operand_new(LIR_OPERAND_FORMAL_PARAMS, formal_params)));
//
//    // 编译 body
//    compiler_block(c, ast_closure->fn->body);
//
//    // 尾部添加结尾 label(basic_block)
//    OP_PUSH(lir_op_label(c->end_label, true));
//    // 添加函数结束指令
//    OP_PUSH(lir_op_new(LIR_OPCODE_FN_END, NULL, NULL, NULL));
//
//    return operand_new(LIR_OPERAND_VAR, lir_var_new(c, ast_closure->fn->name));
//}

static void compiler_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->assert_type) {

        case AST_VAR_DECL: {
            compiler_var_decl(m, stmt->value);
            return;
        }
        case AST_STMT_VAR_DEF: {
            return compiler_var_decl_assign(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return compiler_assign(m, stmt->value);
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
        [AST_EXPR_IDENT] = compiler_ident,
        [AST_EXPR_ENV_ACCESS] = compiler_env_value,
        [AST_EXPR_BINARY] = compiler_binary,
        [AST_EXPR_UNARY] = compiler_unary,
        [AST_EXPR_LIST_NEW] = compiler_list_new,
        [AST_EXPR_LIST_ACCESS] = compiler_list_access,
        [AST_EXPR_MAP_NEW] = compiler_map_new,
        [AST_EXPR_MAP_ACCESS] = compiler_map_access,
        [AST_EXPR_STRUCT_NEW] = compiler_struct_new,
        [AST_EXPR_TUPLE_NEW] = compiler_tuple_new,
        [AST_EXPR_TUPLE_ACCESS] = compiler_tuple_access,
        [AST_CALL] = compiler_call,
        [AST_FNDEF] = compiler_fndef,
};


static lir_operand_t *compiler_expr(module_t *m, ast_expr expr) {
    compiler_expr_fn fn = expr_fn_table[expr.assert_type];
    assertf(fn, "ast right not support");

    lir_operand_t *source = fn(m, expr);

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

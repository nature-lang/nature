#include <string.h>
#include "compiler.h"
#include "src/symbol/symbol.h"
#include "utils/error.h"
#include "src/debug/debug.h"
#include "utils/helper.h"
#include "stdio.h"

int compiler_line = 0;

lir_opcode_e ast_expr_operator_transform[] = {
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


static lir_operand_t *type_convert(closure_t *c, lir_operand_t *source, ast_expr expr) {
    // 待优化目前仅仅服务于 printf
    if (expr.target_type.kind == TYPE_ANY) {
        // any new 处理 float 时需要转换成 int64 处理
        if (source->assert_type == LIR_OPERAND_IMM) {
            lir_imm_t *imm = source->value;
            imm->kind = TYPE_INT64;
        }

        // TODO var 存不下两个指针的数据!! 存下了又能如何，三个，四个指针大小的数据呢？
        lir_operand_t *new_source_var = temp_var_operand(c, expr.target_type);
        // 基于 source 类型计算 element_rtype index
        uint rtype_index = ct_find_rtype_index(expr.type);
        // lir call type, value =>
        linked_push(c->operations,
                    lir_rt_call(RT_CALL_TRANS_ANY, new_source_var, 2,
                                int_operand(rtype_index), // arg1
                                source)); // arg2
        return new_source_var;
    }

    return source;
}


static lir_operand_t *compiler_ident(closure_t *c, ast_expr expr) {
    ast_ident *ident = expr.value;
    lir_operand_t *target = lir_new_empty_operand();

    symbol_t *s = symbol_table_get(ident->literal);

    if (s->type == SYMBOL_TYPE_FN) {
        // label
        target->assert_type = LIR_OPERAND_SYMBOL_LABEL;
        target->value = lir_new_label_operand(s->ident, s->is_local)->value;
    } else if (s->type == SYMBOL_TYPE_VAR) {
        ast_var_decl *var = s->ast_value;
        if (s->is_local) {
            target->assert_type = LIR_OPERAND_VAR;
            target->value = lir_new_var_operand(c, ident->literal);
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


static void compiler_list_assign(closure_t *c, lir_operand_t *list_target, lir_operand_t *index_target, ast_expr src) {
    // value 可能是 lir_imm。
    lir_operand_t *value = compiler_expr(c, src);

    // 取 value 栈指针,如果 value 不是 var， 会自动转换成 var
    lir_operand_t *value_ref = lir_var_ref_operand(c, value);

    // mov $1, -4(%rbp) // 以 var 的形式入栈
    // mov -4(%rbp), rcx // 参数 1, move 将 -4(%rbp) 处的值穿递给了 rcx, 而不是 -4(%rbp) 这个栈地址
    linked_push(c->operations, lir_rt_call(RT_CALL_LIST_ASSIGN, NULL,
                                           3, list_target, index_target, value_ref));
}

static void compiler_map_assign(closure_t *c, ast_assign_stmt *stmt) {
    ast_map_access *map_access = stmt->left.value;
    lir_operand_t *map_target = compiler_expr(c, map_access->left);
    lir_operand_t *key_ref = lir_var_ref_operand(c, compiler_expr(c, map_access->key));
    lir_operand_t *value_ref = lir_var_ref_operand(c, compiler_expr(c, stmt->right));
    lir_op_t *call_op = lir_rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
    linked_push(c->operations, call_op);
}

static void compiler_struct_assign(closure_t *c, ast_assign_stmt *stmt) {
    ast_struct_access *struct_access = stmt->left.value;
    typedecl_t struct_type = struct_access->left.type;
    lir_operand_t *struct_target = compiler_expr(c, struct_access->left);
    uint64_t offset = type_struct_offset(struct_type.struct_decl, struct_access->key);
    uint64_t item_size = type_sizeof(struct_access->property.type);

    lir_operand_t *dst_ref = lir_indirect_addr_operand(struct_target, offset);
    lir_operand_t *src_ref = lir_var_ref_operand(c, compiler_expr(c, stmt->right));

    // move by item size
    linked_push(c->operations, lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                                           3, dst_ref, src_ref, int_operand(item_size)));
}

/**
 * @param c
 * @param stmt
 */
static void compiler_ident_assign(closure_t *c, ast_assign_stmt *stmt) {
    // 如果 left 是 var
    lir_operand_t *src = compiler_expr(c, stmt->right);
    lir_operand_t *dst = compiler_ident(c, stmt->left); // ident
    linked_push(c->operations, lir_op_move(dst, src));
}

/**
 * 这里不包含如 m[a] = 1 这样的 assign
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
static void compiler_var_decl_assign(closure_t *c, ast_var_decl_assign_stmt *stmt) {
    lir_operand_t *src = compiler_expr(c, stmt->expr);
    lir_operand_t *dst = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, stmt->var_decl->ident));

    linked_push(c->operations, lir_op_move(dst, src));
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * @param c
 * @param stmt
 * @return
 */
static void compiler_assign(closure_t *c, ast_assign_stmt *stmt) {
    ast_expr left = stmt->left;

    // list[0] = 1
    if (left.assert_type == AST_EXPR_LIST_ACCESS) {
        ast_list_access_t *list_access = stmt->left.value;
        lir_operand_t *list_target = compiler_expr(c, list_access->left);
        lir_operand_t *index_target = compiler_expr(c, list_access->index);
        return compiler_list_assign(c, list_target, index_target, stmt->right);
    }

    // m["a"] = 2
    if (left.assert_type == AST_EXPR_MAP_ACCESS) {
        return compiler_map_assign(c, stmt);
    }

    // p.name = "wei"
    if (left.assert_type == AST_EXPR_STRUCT_ACCESS) {
        return compiler_struct_assign(c, stmt);
    }

    // a = 1
    if (left.assert_type == AST_EXPR_IDENT) {
        return compiler_ident_assign(c, stmt);
    }

    // tuple[0] = 1 x 禁止这种操作
    assertf(left.assert_type != AST_EXPR_TUPLE_ACCESS, "tuple dose not support item assign");
    assertf(false, "dose not support assign to %d", left.assert_type);
}

/**
 * @param c
 * @param var_decl
 * @return
 */
static linked_t *compiler_var_decl(closure_t *c, ast_var_decl *var_decl) {
    return linked_new();
}


/**
 * call get count => count
 * for:
 *  cmp_goto count == 0 to end for
 *  call get key => key
 *  call get value => value
 *  ....
 *  sub count, 1 => count
 *  goto for:
 * end_for:
 * @param c
 * @param for_in_stmt
 * @return
 */
static void compiler_for_in(closure_t *c, ast_for_in_stmt *ast) {
    lir_operand_t *base_target = compiler_expr(c, ast->iterate);

    lir_operand_t *count_target = temp_var_operand(c, type_base_new(TYPE_INT));
    linked_push(c->operations, lir_rt_call(
            RT_CALL_ITERATE_COUNT,
            count_target,
            1,
            base_target));

    // make label
    lir_op_t *for_label = lir_op_unique_label(FOR_IDENT);
    lir_op_t *end_for_label = lir_op_unique_label(END_FOR_IDENT);
    linked_push(c->operations, for_label);

    lir_op_t *cmp_goto = lir_op_new(
            LIR_OPCODE_BEQ,
            int_operand(0),
            count_target,
            lir_copy_label_operand(end_for_label->output));
    linked_push(c->operations, cmp_goto);

    // 添加 label
    linked_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));

    // gen key
    // gen value
    lir_operand_t *key_target = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast->gen_key->ident));
    lir_operand_t *value_target = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast->gen_value->ident));
    linked_push(c->operations, lir_rt_call(
            RT_CALL_ITERATE_GEN_KEY,
            key_target,
            1,
            base_target));
    linked_push(c->operations, lir_rt_call(
            RT_CALL_ITERATE_GEN_VALUE,
            value_target,
            1,
            base_target));

    // block
    compiler_block(c, ast->body);

    // sub count, 1 => count
    lir_op_t *sub_op = lir_op_new(
            LIR_OPCODE_SUB,
            count_target,
            int_operand(1),
            count_target);

    linked_push(c->operations, sub_op);

    // goto for
    linked_push(c->operations, lir_op_bal(for_label->output));
    linked_push(c->operations, end_for_label);
}

static void compiler_while(closure_t *c, ast_while_stmt *ast) {
    lir_op_t *while_label = lir_op_unique_label(WHILE_IDENT);
    linked_push(c->operations, while_label);
    lir_operand_t *end_while_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_WHILE_IDENT), true);

    lir_operand_t *condition_target = compiler_expr(c, ast->condition);
    lir_op_t *cmp_goto = lir_op_new(
            LIR_OPCODE_BEQ,
            bool_operand(false),
            condition_target,
            end_while_operand);

    linked_push(c->operations, cmp_goto);
    linked_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));
    compiler_block(c, ast->body);

    linked_push(c->operations, lir_op_bal(while_label->output));

    linked_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_while_operand));
}

static void compiler_return(closure_t *c, ast_return_stmt *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *target = compiler_expr(c, *ast->expr);
        lir_op_t *return_op = lir_op_new(LIR_OPCODE_RETURN, target, NULL, NULL);
        linked_push(c->operations, return_op); // return op 只是做了个 mov result -> rax 的操作
    }

    linked_push(c->operations, lir_op_bal(lir_new_label_operand(c->end_label, false)));
}

static void compiler_if(closure_t *c, ast_if_stmt *if_stmt) {
    // 编译 condition
    lir_operand_t *condition_target = compiler_expr(c, if_stmt->condition);

    // 判断结果是否为 false, false 对应 else
    lir_operand_t *false_target = bool_operand(false);
    lir_operand_t *end_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_IF_IDENT), true);
    lir_operand_t *alternate_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(ALTERNATE_IF_IDENT), true);

    lir_op_t *cmp_goto;
    if (if_stmt->alternate->count == 0) {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(end_label_operand));
    } else {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(alternate_label_operand));
    }
    linked_push(c->operations, cmp_goto);
    linked_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));

    // 编译 consequent block
    compiler_block(c, if_stmt->consequent);
    linked_push(c->operations, lir_op_bal(end_label_operand));

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        linked_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, alternate_label_operand));
        compiler_block(c, if_stmt->alternate);
    }

    // 追加 end_if 标签
    linked_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_label_operand));
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
static lir_operand_t *compiler_call(closure_t *c, ast_expr expr) {
    ast_call *call = expr.value;
    lir_operand_t *target = NULL;
    if (expr.type.kind != TYPE_NULL) {
        target = temp_var_operand(c, expr.type);
    }

    // push 指令所有的物理寄存器入栈
    // 这里增加了无意义的堆栈和符号表,不符合简捷之道
    lir_operand_t *base_target = compiler_expr(c, call->left);

    slice_t *call_actual_params = slice_new();
    typedecl_fn_t *formal_fn = call->left.type.fn_decl;
    // 预编译 spread operand, 避免每一次使用 spread 都编译一次
    lir_operand_t *spread_list_target = NULL;
    if (call->spread_param) {
        // compiler_expr 中已经做了 mov 操作
        spread_list_target = compiler_expr(c, call->actual_params[call->param_count - 1]);
    }

    uint8_t spread_index = 0;
    for (int i = 0; i < formal_fn->formals_count; ++i) {
        lir_operand_t *rest_param_target = NULL;

        // rest
        bool is_rest = formal_fn->rest_param && i == formal_fn->formals_count - 1;
        if (is_rest) {
            // lir array_new(count, size) -> rest_param_target
            typedecl_t rest_param_type = formal_fn->formals_types[formal_fn->formals_count - 1];
            assertf(rest_param_type.kind == TYPE_LIST, "rest param must be list type");

            uint64_t rest_param_rtype_index = ct_find_rtype_index(rest_param_type);


            typedecl_list_t *list_decl = rest_param_type.list_decl;
            // actual 剩余的所有参数都需要用一个数组收集起来，并写入到 target_operand 中
            rest_param_target = temp_var_operand(c, rest_param_type);

            lir_operand_t *rtype_index = int_operand(ct_find_rtype_index(rest_param_type));
            lir_operand_t *element_index = int_operand(ct_find_rtype_index(list_decl->element_type));
            lir_operand_t *capacity = int_operand(0);
            linked_push(c->operations, lir_rt_call(
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
                        linked_push(c->operations, lir_rt_call(RT_CALL_LIST_LENGTH,
                                                               length_target,
                                                               1,
                                                               spread_list_target));


                        // spread 被分割了一部分，所以需要对剩余的 temp target 进行 array_slice 切割
                        // 此方法不会改变现有数组，而是生成一个新的数组, rest 在 fn def 定义，所以先找到定义的 type
                        linked_push(c->operations, lir_rt_call(RT_CALL_LIST_SPLICE,
                                                               spread_list_target, // 切割后的结果保存回 spread_target
                                                               4,
                                                               spread_rtype_index,
                                                               spread_list_target,
                                                               int_operand(spread_index),
                                                               length_target));
                    }
                    // 此方法不会改变现有数组，而是生成一个新的数组
                    linked_push(c->operations, lir_rt_call(
                            RT_CALL_LIST_CONCAT,
                            rest_param_target,
                            3,
                            spread_rtype_index,
                            rest_param_target,
                            spread_list_target));

                } else {
                    lir_operand_t *actual_param_target = compiler_expr(c, call->actual_params[j]);

                    // 将栈上的地址传递给 list 即可,不需要管栈中存储的值
                    lir_operand_t *target_ref = lir_var_ref_operand(c, actual_param_target);
                    linked_push(c->operations,
                                lir_rt_call(RT_CALL_LIST_PUSH,
                                            NULL,
                                            2,
                                            rest_param_target,
                                            target_ref));
                }
            }

            slice_push(call_actual_params, rest_param_target);
            break;
        }

        // spread call([int] list)
        bool is_spread = call->spread_param && i >= call->param_count - 1;
        if (is_spread) {
            // formal i >= actual param count, 则 actual param count - 1 对应的参数为 spread list,
            // 需要不断的从 spread list 中提取值,并传递到 call_actual_params 中

            //  1. 读取 spread_target element type
            typedecl_t t = call->actual_params[call->param_count - 1].type;

            lir_operand_t *temp = temp_var_operand(c, t.list_decl->element_type);
            // 读取 result 的指针地址，给到 access 进行写入
            lir_operand_t *temp_ref = lir_var_ref_operand(c, temp);

            linked_push(c->operations, lir_rt_call(
                    RT_CALL_LIST_ACCESS,
                    NULL,
                    3,
                    spread_list_target,
                    spread_index,
                    temp_ref));

            spread_index += 1;
            slice_push(call_actual_params, temp);
            continue;
        }

        // common
        rest_param_target = compiler_expr(c, call->actual_params[i]);
        slice_push(call_actual_params, rest_param_target);
    }

    // return target
    lir_op_t *call_op = lir_op_new(LIR_OPCODE_CALL,
                                   base_target,
                                   LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, call_actual_params),
                                   target);

    linked_push(c->operations, call_op);

    return target;
}

static lir_operand_t *compiler_binary(closure_t *c, ast_expr expr) {
    ast_binary_expr *binary_expr = expr.value;

    lir_opcode_e type = ast_expr_operator_transform[binary_expr->operator];

    lir_operand_t *left_target = compiler_expr(c, binary_expr->left);
    lir_operand_t *right_target = compiler_expr(c, binary_expr->right);
    lir_operand_t *result_target = temp_var_operand(c, expr.type);

    linked_push(c->operations, lir_op_new(type, left_target, right_target, result_target));

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
static lir_operand_t *compiler_unary(closure_t *c, ast_expr expr) {
    ast_unary_expr *unary_expr = expr.value;

    lir_operand_t *target = temp_var_operand(c, expr.type);

    lir_operand_t *first = compiler_expr(c, unary_expr->operand);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_EXPR_OPERATOR_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        imm->int_value = -imm->int_value;
        // move 操作即可
        linked_push(c->operations, lir_op_move(target, first));
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

    lir_opcode_e type = ast_expr_operator_transform[unary_expr->operator];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    linked_push(c->operations, unary);

    return target;
}

/**
 * int a = list[0]
 * string s = list[1]
 */
static lir_operand_t *compiler_list_access(closure_t *c, ast_expr expr) {
    ast_list_access_t *ast = expr.value;

    lir_operand_t *list_target = compiler_expr(c, ast->left);
    lir_operand_t *index_target = compiler_expr(c, ast->index);

    lir_operand_t *result = temp_var_operand(c, expr.type);
    // 读取 result 的指针地址，给到 access 进行写入
    lir_operand_t *result_ref = lir_var_ref_operand(c, result);

    linked_push(c->operations, lir_rt_call(RT_CALL_LIST_ACCESS, NULL,
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
static lir_operand_t *compiler_list_new(closure_t *c, ast_expr expr) {
    ast_new_list *ast = expr.value;

    lir_operand_t *list_target = temp_var_operand(c, expr.type);

    typedecl_list_t *list_decl = ast->type.list_decl;
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
    linked_push(c->operations, call_op);

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
static lir_operand_t *compiler_env_value(closure_t *c, ast_expr expr) {
    ast_env_value *ast = expr.value;
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

    linked_push(c->operations, call_op);

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
static lir_operand_t *compiler_map_access(closure_t *c, ast_expr expr) {
    ast_map_access *ast = expr.value;


    // compiler base address left_target
    lir_operand_t *map_target = compiler_expr(c, ast->left);
    typedecl_t type_map_decl = ast->left.type;

    // compiler key to temp var
    lir_operand_t *key_target_ref = lir_var_ref_operand(c, compiler_expr(c, ast->key));
    lir_operand_t *value_target = temp_var_operand(c, type_map_decl.map_decl->value_type);
    lir_operand_t *value_target_ref = lir_var_ref_operand(c, value_target);


    // runtime get slot by temp var runtime.map_offset(base, "key")
    lir_op_t *call_op = lir_rt_call(
            RT_CALL_MAP_ACCESS,
            NULL,
            3,
            map_target,
            key_target_ref,
            value_target_ref
    );
    linked_push(c->operations, call_op);

    return value_target;
}

/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *compiler_map_new(closure_t *c, ast_expr expr) {
    ast_map_new *ast = expr.value;
    typedecl_t map_type = expr.type;

    uint64_t map_rtype_index = ct_find_rtype_index(map_type);
    uint64_t key_index = ct_find_rtype_index(map_type.map_decl->key_type);
    uint64_t value_index = ct_find_rtype_index(map_type.map_decl->value_type);

    lir_operand_t *map_target = temp_var_operand(c, expr.type);
    lir_op_t *call_op = lir_rt_call(RT_CALL_MAP_NEW, map_target,
                                    3,
                                    int_operand(map_rtype_index),
                                    int_operand(key_index),
                                    int_operand(value_index));
    linked_push(c->operations, call_op);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->count; ++i) {
        ast_expr key_expr = ast->values[i].key;
        ast_expr value_expr = ast->values[i].value;
        lir_operand_t *key_ref = lir_var_ref_operand(c, compiler_expr(c, key_expr));
        lir_operand_t *value_ref = lir_var_ref_operand(c, compiler_expr(c, value_expr));


        call_op = lir_rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
        linked_push(c->operations, call_op);
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
static lir_operand_t *compiler_struct_access(closure_t *c, ast_expr expr) {
    ast_struct_access *ast = expr.value;

    lir_operand_t *struct_target = compiler_expr(c, ast->left);
    typedecl_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->property.type);
    uint64_t offset = type_struct_offset(t.struct_decl, ast->key);

    lir_operand_t *dst = temp_var_operand(c, ast->property.type);
    lir_operand_t *dst_ref = lir_var_ref_operand(c, dst);

    lir_operand_t *src_ref = lir_indirect_addr_operand(struct_target, offset);
    linked_push(c->operations, lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
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
static lir_operand_t *compiler_struct_new(closure_t *c, ast_expr expr) {
    ast_new_struct *ast = expr.value;
    lir_operand_t *struct_target = temp_var_operand(c, expr.type);

    typedecl_t type = ast->type;

    uint64_t rtype_index = ct_find_rtype_index(type);

    linked_push(c->operations, lir_rt_call(RT_CALL_STRUCT_NEW, struct_target,
                                           1, int_operand(rtype_index)));

    // 快速赋值,由于 struct 的相关属性都存储在 type 中，所以便宜量等值都需要在前端完成计算
    uint64_t offset = 0;
    for (int i = 0; i < ast->count; ++i) {
        ast_struct_property struct_property = ast->properties[i];

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t item_size = type_sizeof(struct_property.type);
        offset = align((int64_t) offset, (int64_t) item_size);

        // offset(var) var must assign reg
        lir_operand_t *dst_ref = lir_indirect_addr_operand(struct_target, offset);
        lir_operand_t *src_ref = lir_var_ref_operand(c, compiler_expr(c, struct_property.value));

        // move by item size
        linked_push(c->operations, lir_rt_call(RT_CALL_MEMORY_MOVE, NULL,
                                               3, dst_ref, src_ref, int_operand(item_size)));
        offset += item_size;
    }

    return struct_target;
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *compiler_literal(closure_t *c, ast_expr expr) {
    ast_literal *literal = expr.value;

    switch (literal->kind) {
        case TYPE_STRING: {
            // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
            lir_operand_t *target = temp_var_operand(c, expr.type);
            lir_operand_t *imm_c_string_operand = string_operand(literal->value);
            lir_operand_t *imm_len_operand = int_operand(strlen(literal->value));
            lir_op_t *call_op = lir_rt_call(
                    RT_CALL_STRING_NEW,
                    target,
                    2,
                    imm_c_string_operand,
                    imm_len_operand);
            linked_push(c->operations, call_op);
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
static lir_operand_t *compiler_closure(closure_t *parent, ast_expr expr) {
    ast_closure_t *ast_closure = expr.value;
    // handle env
    if (parent && ast_closure->env_list->count > 0) {
        // 处理 env ---------------
        // 1. make env_n by count
        lir_operand_t *env_name_param = string_operand(ast_closure->env_name);
        lir_operand_t *capacity_param = int_operand(ast_closure->env_list->count);
        linked_push(parent->operations,
                    lir_rt_call(RT_CALL_ENV_NEW, NULL, 2, env_name_param, capacity_param));

        // 2. for set ast_ident/ast_access_env to env n
        for (int i = 0; i < ast_closure->env_list->count; ++i) {
            ast_expr *env_expr = ast_closure->env_list->take[i];
            // env_value(indirect_addr operand) or ident(var operand)
            lir_operand_t *env_target = compiler_expr(parent, *env_expr);
            assertf(env_target->assert_type == LIR_OPERAND_VAR, "expr_target type not var, cannot use by env set");
            lir_operand_t *env_point_target = temp_var_operand(parent, type_ptrof(env_expr->type, 1));
            linked_push(parent->operations, lir_op_new(LIR_OPCODE_LEA, env_target, NULL, env_point_target));

            lir_operand_t *env_index_param = int_operand(i);
            lir_op_t *call_op = lir_rt_call(
                    RT_CALL_ENV_ASSIGN,
                    NULL,
                    3,
                    env_name_param,
                    env_index_param,
                    env_point_target // value
            );
            linked_push(parent->operations, call_op);
        }

    }

    // new 一个新的 closure_t ---------------
    closure_t *c = lir_new_closure(ast_closure);
    c->module = current_module;
    c->name = ast_closure->fn->name; // analysis 阶段已经进行了唯一名称处理
    c->end_label = str_connect("end_", c->name);
    c->parent = parent;
    slice_push(compiler_closures, c);

    // 添加 label 和 fn begin 入口
    linked_push(c->operations, lir_op_label(ast_closure->fn->name, false));

    slice_t *formal_params = slice_new();
    for (int i = 0; i < ast_closure->fn->param_count; ++i) {
        ast_var_decl *param = ast_closure->fn->formal_params[i];
        // var operand 依赖 var_decl 定义的变量
        lir_var_t *var = lir_new_var_operand(c, param->ident);
        slice_push(formal_params, var);
    }

    linked_push(c->operations, lir_op_new(LIR_OPCODE_FN_BEGIN, NULL, NULL,
                                          LIR_NEW_OPERAND(LIR_OPERAND_FORMAL_PARAMS, formal_params)));

    // 编译 body
    compiler_block(c, ast_closure->fn->body);

    // 尾部添加结尾 label(basic_block)
    linked_push(c->operations, lir_op_label(c->end_label, true));
    // 添加函数结束指令
    linked_push(c->operations, lir_op_new(LIR_OPCODE_FN_END, NULL, NULL, NULL));

    return LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast_closure->fn->name));
}

static void compiler_stmt(closure_t *c, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_NEW_CLOSURE: {
            compiler_closure(c, (ast_expr) {
//                    .type = NULL,
//                    .target_type = NULL,
                    .line = stmt->line,
                    .value = stmt->value,
                    .assert_type = AST_NEW_CLOSURE
            });
            return;
        }
        case AST_VAR_DECL: {
            compiler_var_decl(c, (ast_var_decl *) stmt->value);
            return;
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            compiler_var_decl_assign(c, (ast_var_decl_assign_stmt *) stmt->value);
            return;
        }
        case AST_STMT_ASSIGN: {
            compiler_assign(c, (ast_assign_stmt *) stmt->value);
            return;
        }
        case AST_STMT_IF: {
            compiler_if(c, (ast_if_stmt *) stmt->value);
            return;
        }
        case AST_STMT_FOR_IN: {
            compiler_for_in(c, (ast_for_in_stmt *) stmt->value);
            return;
        }
        case AST_STMT_WHILE: {
            compiler_while(c, (ast_while_stmt *) stmt->value);
            return;
        }
        case AST_CALL: {
            ast_expr expr = {
                    .line = stmt->line,
                    .assert_type = stmt->assert_type,
                    .value = stmt->value,
//                    .type = NULL,
            };
            compiler_call(c, expr);
            return;
        }
        case AST_STMT_RETURN: {
            compiler_return(c, (ast_return_stmt *) stmt->value);
            return;
        }
        case AST_STMT_TYPE_DECL: {
            return;
        }
        default: {
            assertf(false, "line: %d, unknown stmt", compiler_line);
        }
    }
}

compiler_expr_fn expr_fn_table[] = {
        [AST_EXPR_LITERAL] = compiler_literal,
        [AST_EXPR_IDENT] = compiler_ident,
        [AST_EXPR_LIST_ACCESS] = compiler_list_access,
        [AST_EXPR_ENV_VALUE] = compiler_env_value,
        [AST_EXPR_BINARY] = compiler_binary,
        [AST_EXPR_UNARY] = compiler_unary,
        [AST_CALL] = compiler_call,
        [AST_EXPR_LIST_NEW] = compiler_list_new,
        [AST_EXPR_MAP_ACCESS] = compiler_map_access,
        [AST_EXPR_MAP_NEW] = compiler_map_new,
        [AST_EXPR_STRUCT_ACCESS] = compiler_struct_access,
        [AST_EXPR_STRUCT_NEW] = compiler_struct_new,
        [AST_NEW_CLOSURE] = compiler_closure
};


static lir_operand_t *compiler_expr(closure_t *c, ast_expr expr) {
    compiler_expr_fn fn = expr_fn_table[expr.assert_type];
    assertf(fn, "ast expr not support");

    lir_operand_t *source = fn(c, expr);

    if (expr.type.kind != expr.target_type.kind) {
        source = type_convert(c, source, expr);
    }

    return source;
}


static void compiler_block(closure_t *c, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
        ast_stmt *stmt = block->take[i];
        compiler_line = stmt->line;
        lir_line = stmt->line;
#ifdef DEBUG_COMPILER
        debug_stmt("COMPILER", stmt);
#endif
        compiler_stmt(c, stmt);
    }
}


/**
 * @param c
 * @param ast
 * @return
 */
slice_t *compiler(module_t *m, ast_closure_t *ast) {
    // init
    compiler_closures = slice_new();
    current_module = m;

    // compiler
    compiler_closure(NULL, (ast_expr) {
//            .type = NULL,
//            .target_type = NULL,
            .assert_type = AST_NEW_CLOSURE,
            .value = ast,
    });

    return compiler_closures;
}

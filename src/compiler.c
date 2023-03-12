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
            imm->type = TYPE_INT64;
        }

        // TODO var 存不下两个指针的数据!! 存下了又能如何，三个，四个指针大小的数据呢？
        lir_operand_t *new_source = lir_temp_var_operand(c, expr.target_type);
        // lir call type, value =>
        list_push(c->operations, lir_op_runtime_call(RUNTIME_CALL_ANY_NEW, new_source, 2,
                                                     LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT8, int_value, expr.type.kind),
                                                     source));
        return new_source;
    }

    return source;
}

/**
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
static void compiler_var_decl_assign(closure_t *c, ast_var_decl_assign_stmt *stmt) {
    lir_operand_t *src = compiler_expr(c, stmt->expr);
    lir_operand_t *dst = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, stmt->var_decl->ident));

    list_push(c->operations, lir_op_move(dst, src));
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * @param c
 * @param stmt
 * @return
 */
static void compiler_assign(closure_t *c, ast_assign_stmt *stmt) {
    // 如果 left 是 var
    lir_operand_t *src = compiler_expr(c, stmt->right);
    lir_operand_t *dst = compiler_expr(c, stmt->left);

    list_push(c->operations, lir_op_move(dst, src));
}

/**
 * @param c
 * @param var_decl
 * @return
 */
static list *compiler_var_decl(closure_t *c, ast_var_decl *var_decl) {
    return list_new();
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

    lir_operand_t *count_target = lir_temp_var_operand(c, type_base_new(TYPE_INT));
    list_push(c->operations, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_COUNT,
            count_target,
            1,
            base_target));

    // make label
    lir_op_t *for_label = lir_op_unique_label(FOR_IDENT);
    lir_op_t *end_for_label = lir_op_unique_label(END_FOR_IDENT);
    list_push(c->operations, for_label);

    lir_op_t *cmp_goto = lir_op_new(
            LIR_OPCODE_BEQ,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 0),
            count_target,
            lir_copy_label_operand(end_for_label->output));
    list_push(c->operations, cmp_goto);

    // 添加 label
    list_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));

    // gen key
    // gen value
    lir_operand_t *key_target = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast->gen_key->ident));
    lir_operand_t *value_target = LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast->gen_value->ident));
    list_push(c->operations, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_GEN_KEY,
            key_target,
            1,
            base_target));
    list_push(c->operations, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_GEN_VALUE,
            value_target,
            1,
            base_target));

    // block
    compiler_block(c, ast->body);

    // sub count, 1 => count
    lir_op_t *sub_op = lir_op_new(
            LIR_OPCODE_SUB,
            count_target,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1),
            count_target);

    list_push(c->operations, sub_op);

    // goto for
    list_push(c->operations, lir_op_bal(for_label->output));
    list_push(c->operations, end_for_label);
}

static void compiler_while(closure_t *c, ast_while_stmt *ast) {
    lir_op_t *while_label = lir_op_unique_label(WHILE_IDENT);
    list_push(c->operations, while_label);
    lir_operand_t *end_while_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_WHILE_IDENT), true);

    lir_operand_t *condition_target = compiler_expr(c, ast->condition);
    lir_op_t *cmp_goto = lir_op_new(
            LIR_OPCODE_BEQ,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false),
            condition_target,
            end_while_operand);

    list_push(c->operations, cmp_goto);
    list_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));
    compiler_block(c, ast->body);

    list_push(c->operations, lir_op_bal(while_label->output));

    list_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_while_operand));
}

static void compiler_return(closure_t *c, ast_return_stmt *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *target = compiler_expr(c, *ast->expr);
        lir_op_t *return_op = lir_op_new(LIR_OPCODE_RETURN, target, NULL, NULL);
        list_push(c->operations, return_op); // return op 只是做了个 mov result -> rax 的操作
    }

    list_push(c->operations, lir_op_bal(lir_new_label_operand(c->end_label, false)));
}

static void compiler_if(closure_t *c, ast_if_stmt *if_stmt) {
    // 编译 condition
    lir_operand_t *condition_target = compiler_expr(c, if_stmt->condition);

    // 判断结果是否为 false, false 对应 else
    lir_operand_t *false_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false);
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
    list_push(c->operations, cmp_goto);
    list_push(c->operations, lir_op_unique_label(CONTINUE_IDENT));

    // 编译 consequent block
    compiler_block(c, if_stmt->consequent);
    list_push(c->operations, lir_op_bal(end_label_operand));

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        list_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, alternate_label_operand));
        compiler_block(c, if_stmt->alternate);
    }

    // 追加 end_if 标签
    list_push(c->operations, lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_label_operand));
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
        target = lir_temp_var_operand(c, expr.type);
    }

    // call->left is debug ident
    // push 指令所有的物理寄存器入栈
    // 这里增加了无意义的堆栈和符号表,不符合简捷之道
    lir_operand_t *base_target = compiler_expr(c, call->left);

    slice_t *params_operand = slice_new();
    type_fn_t *formal_fn = call->left.type.value;
    // 预编译 spread operand, 避免每一次使用 spread 都编译一次
    lir_operand_t *spread_target = NULL;
    if (call->spread_param) {
        // compiler_expr 中已经做了 mov 操作
        spread_target = compiler_expr(c, call->actual_params[call->actual_param_count - 1]);
    }

    uint8_t spread_index = 0;
    for (int i = 0; i < formal_fn->formals_count; ++i) {
        lir_operand_t *param_target = NULL;

        // rest
        bool is_rest = formal_fn->rest_param && i == formal_fn->formals_count - 1;
        if (is_rest) {
            // lir array_new(count, size) -> param_target
            type_t last_param_type = formal_fn->formals_types[formal_fn->formals_count - 1];
            assertf(last_param_type.kind == TYPE_ARRAY, "rest param must be array type");

            ast_array_decl *array_decl = last_param_type.value;
            // actual 剩余的所有参数都需要用一个数组收集起来，并写入到 target_operand 中
            param_target = lir_temp_var_operand(c, last_param_type);
            lir_operand_t *count_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 0);
            lir_operand_t *size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value,
                                                                    (int) type_kind_sizeof(array_decl->type.kind));
            lir_op_t *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_ARRAY_NEW,
                    param_target, // param_target is array_t
                    2,
                    count_operand,
                    size_operand
            );
            list_push(c->operations, call_op);

            // param target 目前存储了 array_t 的地址信息
            // 从 i 开始算起，剩余的所有实参都编译并填入到 rest param target 中,
            for (int j = i; j < call->actual_param_count; ++j) {
                bool is_spread = call->spread_param && j == call->actual_param_count - 1;
                if (is_spread) {
                    // spread 以及预编译过了
                    if (spread_index > 0) {
                        // spread 被分割了一部分，所以需要对剩余的 temp target 进行 array_slice 切割
                        call_op = lir_op_runtime_call(RUNTIME_CALL_ARRAY_SPLIT, spread_target, 3, spread_target,
                                                      LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, spread_index),
                                                      LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, -1));

                        list_push(c->operations, call_op);
                    }
                    list_push(c->operations,
                              lir_op_runtime_call(RUNTIME_CALL_ARRAY_CONCAT, NULL, 2, param_target, spread_target));

                } else {
                    lir_operand_t *temp_target = compiler_expr(c, call->actual_params[j]);
                    list_push(c->operations, lir_op_runtime_call(RUNTIME_CALL_ARRAY_PUSH,
                                                                 NULL, 2, param_target, temp_target));
                }
            }

            slice_push(params_operand, param_target);
            break;
        }

        // spread
        bool is_spread = call->spread_param && i >= call->actual_param_count - 1;
        if (is_spread) {
            // compiler last
            // spread
            // i >= actual param count, 则 actual param count - 1 对应的参数为 spread array, 需要不断的从 spread array 中提取值
            // 首先编译 spread target, 然后基于 spread target 进行 array value 的操作
            // lir array value(spread_target, spread_index) -> param_target
            lir_op_t *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_ARRAY_VALUE,
                    param_target,
                    2,
                    spread_target,
                    spread_index
            );
            list_push(c->operations, call_op);

            spread_index += 1;
            slice_push(params_operand, param_target);
            continue;
        }

        // common
        param_target = compiler_expr(c, call->actual_params[i]);
        slice_push(params_operand, param_target);
    }

    // return target
    lir_op_t *call_op = lir_op_new(LIR_OPCODE_CALL,
                                   base_target,
                                   LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand),
                                   target);

    list_push(c->operations, call_op);

    return target;
}

static lir_operand_t *compiler_binary(closure_t *c, ast_expr expr) {
    ast_binary_expr *binary_expr = expr.value;

    lir_opcode_e type = ast_expr_operator_transform[binary_expr->operator];

    lir_operand_t *left_target = compiler_expr(c, binary_expr->left);
    lir_operand_t *right_target = compiler_expr(c, binary_expr->right);
    lir_operand_t *result_target = lir_temp_var_operand(c, expr.type);

    list_push(c->operations, lir_op_new(type, left_target, right_target, result_target));

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

    lir_operand_t *target = lir_temp_var_operand(c, expr.type);

    lir_operand_t *first = compiler_expr(c, unary_expr->operand);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_EXPR_OPERATOR_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        imm->int_value = -imm->int_value;
        // move 操作即可
        list_push(c->operations, lir_op_move(target, first));
        return target;
    }

    if (unary_expr->operator == AST_EXPR_OPERATOR_IA) {
        // 如果 first 都不是指针，那就不给解引用直接报错，只有变量才能是指针类型
        assertf(first->assert_type == LIR_OPERAND_VAR, "operator IA, but operand not var");
        // 添加引用标识(var 维度，而不是变量维度,而不是 local_var_decl 维度)
        lir_var_t *var = first->value;
        var->indirect_addr = true;
        return first;
    }

    lir_opcode_e type = ast_expr_operator_transform[unary_expr->operator];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    list_push(c->operations, unary);

    return target;
}

/**
 * 访问列表结构
 * 如何区分 a[1] = 2  和 c = a[1]
 *
 * a()[0]
 * a.b[0]
 * a[0]
 * list[int] l = make(list[int]);
 * 简单类型与复杂类型
 *
 * list 的边界是可以使用 push 动态扩容的，因此需要在某个地方存储 list 的最大 index,从而判断是否越界
 * for item by database
 *    list.push(item)
 *
 * 通过上面的示例可以确定在编译截断无法判断数组是否越界，需要延后到运行阶段，也就是 access_list 这里
 */
static lir_operand_t *compiler_array_value(closure_t *c, ast_expr expr) {
    ast_array_value_t *ast = expr.value;
    // new tmp 是无类型的。
    // left_target.code is list[int]
    // left_target.var = runtime.make_list(size)
    // left_target.var to symbol
    // 假设是内存机器，则有 left_target.val = sp[n]
    // 但是无论如何，此时 left_target 的type 是 var, val 对应 lir_operand_var.
    // 且 lir_operand_var.ident 可以在 symbol 查出相关类型
    // var 实际上会在真实物理计算机中的内存或者其他空间拥有一份空间，并写入了一个值。
    // 即当成 var 是有值的即可！！具体的值是多少咱也不知道

    // base_target 存储 list 在内存中的基址
    lir_operand_t *base_target = compiler_expr(c, ast->left);

    // index 为偏移量, index 值是运行时得出的，所以没有办法在编译时计算出偏移size. 虽然通过 MUL 指令可以租到，不过这种事还是交给 runtime 吧
    lir_operand_t *index_target = compiler_expr(c, ast->index);

    lir_operand_t *value_point = lir_temp_var_operand(c, type_with_point(expr.type, 1));
    // 如果使用是没问题的，但是外部的值没法写入进去
    lir_op_t *call_op = lir_op_runtime_call(
            RUNTIME_CALL_ARRAY_VALUE,
            value_point,
            2,
            base_target,
            index_target
    );

    list_push(c->operations, call_op);

    // set target
    return lir_indirect_addr_operand(c, value_point);
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
static lir_operand_t *compiler_new_array(closure_t *c, ast_expr expr) {
    ast_new_list *ast = expr.value;

    lir_operand_t *target = lir_temp_var_operand(c, expr.type);

    // 类型，容量 runtime.make_list(capacity, size)
    ast_array_decl *array_decl = ast->ast_type.value;
    lir_operand_t *count_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, array_decl->count);
    lir_operand_t *size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value,
                                                            (int) type_kind_sizeof(ast->ast_type.kind));
    lir_op_t *call_op = lir_op_runtime_call(
            RUNTIME_CALL_ARRAY_NEW,
            target,
            2,
            count_operand,
            size_operand
    );
    list_push(c->operations, call_op);

    // 值初始化
    for (int i = 0; i < ast->count; ++i) {
        ast_expr item_expr = ast->values[i];
        lir_operand_t *src = compiler_expr(c, item_expr);

        // assign
        lir_operand_t *value_point = lir_temp_var_operand(c, type_with_point(item_expr.type, 1));
        lir_operand_t *index_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
        call_op = lir_op_runtime_call(
                RUNTIME_CALL_ARRAY_VALUE,
                value_point,
                2,
                target,
                index_target
        );
        list_push(c->operations, call_op);

        lir_operand_t *temp_target = lir_indirect_addr_operand(c, value_point);
        list_push(c->operations, lir_op_move(temp_target, src));
    }

    return target;
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
    lir_operand_t *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_RAW_STRING, string_value, c->env_name);
    lir_operand_t *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast->index);
    // target 通常就是一个 temp_var
    lir_operand_t *value_point = lir_temp_var_operand(c, type_with_point(expr.type, 1));
    lir_op_t *call_op = lir_op_runtime_call(
            RUNTIME_CALL_ENV_VALUE,
            value_point,
            2,
            env_name_param,
            env_index_param
    );

    list_push(c->operations, call_op);

    return lir_indirect_addr_operand(c, value_point);
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
static lir_operand_t *compiler_map_value(closure_t *c, ast_expr expr) {
    ast_map_value *ast = expr.value;


    // compiler base address left_target
    lir_operand_t *base_target = compiler_expr(c, ast->left);

    // compiler key to temp var
    lir_operand_t *key_target = compiler_expr(c, ast->key);

    // runtime get slot by temp var runtime.map_offset(base, "key")
    lir_operand_t *value_point = lir_temp_var_operand(c, type_with_point(expr.type, 1));
    lir_op_t *call_op = lir_op_runtime_call(
            RUNTIME_CALL_MAP_VALUE,
            value_point,
            2,
            base_target,
            key_target
    );
    list_push(c->operations, call_op);

    return lir_indirect_addr_operand(c, value_point);
}

/**
 * call runtime.make_map => t1 // 基础地址
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *compiler_new_map(closure_t *c, ast_expr expr) {
    ast_new_map *ast = expr.value;
    lir_operand_t *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, (int) ast->capacity);
    lir_operand_t *size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value,
                                                            (int) type_kind_sizeof(ast->key_type.kind) +
                                                            (int) type_kind_sizeof(ast->value_type.kind));

    lir_operand_t *target = lir_temp_var_operand(c, expr.type);
    lir_op_t *call_op = lir_op_runtime_call(
            RUNTIME_CALL_MAP_NEW,
            target,
            2,
            capacity_operand,
            size_operand
    );
    list_push(c->operations, call_op);

    // 默认值初始化
    for (int i = 0; i < ast->count; ++i) {
        ast_expr key_expr = ast->values[i].key;
        ast_expr value_expr = ast->values[i].value;
        lir_operand_t *key_target = compiler_expr(c, key_expr);
        lir_operand_t *value_target = compiler_expr(c, value_expr);

        lir_operand_t *temp_point = lir_temp_var_operand(c, type_with_point(key_expr.type, 1));
        call_op = lir_op_runtime_call(
                RUNTIME_CALL_MAP_VALUE,
                temp_point,
                2,
                target, // map base
                key_target
        );
        list_push(c->operations, call_op);

        // use by
        list_push(c->operations, lir_op_move(lir_indirect_addr_operand(c, temp_point), value_target));
    }

    return target;
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_select_property(closure_t *c, ast_expr expr) {
    ast_select_property *ast = expr.value;

    // 计算基值
    lir_operand_t *base_target = compiler_expr(c, ast->left);
    int offset = ast_struct_offset(ast->struct_decl, ast->property);

    return lir_indirect_addr_offset_operand(base_target, offset, expr.type);
}

/**
 * foo.bar = 1
 *
 * person baz = person {
 *  age = 100
 *  sex = true
 * }
 *
 * 无论是 baz 还是 foo.bar （经过 compiler_expr_with_convert(expr.left)） 最终都会转换成结构体基址用于赋值
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *compiler_new_struct(closure_t *c, ast_expr expr) {
    ast_new_struct *ast = expr.value;
    lir_operand_t *target = lir_temp_var_operand(c, expr.type);

    ast_struct_decl *struct_decl = ast->type.value;
    int size = ast_struct_decl_size(struct_decl);

    // new struct by runtime call, base_target
    list_push(c->operations, lir_op_runtime_call(RUNTIME_CALL_GC_MALLOC,
                                                 target, 1, LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, size)));

    // set init value
    for (int i = 0; i < ast->count; ++i) {
        ast_struct_property struct_property = ast->list[i];

        lir_operand_t *src = compiler_expr(c, struct_property.value);

        int offset = ast_struct_offset(struct_decl, struct_property.key);
        lir_operand_t *dst = lir_indirect_addr_offset_operand(target, offset, struct_property.value.type);
        list_push(c->operations, lir_op_move(dst, src));
    }

    return target;
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *compiler_literal(closure_t *c, ast_expr expr) {
    ast_literal *literal = expr.value;

    switch (literal->type) {
        case TYPE_STRING: {
            // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
            lir_operand_t *target = lir_temp_var_operand(c, expr.type);
            lir_operand_t *imm_c_string_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_RAW_STRING, string_value,
                                                                            literal->value);
            lir_operand_t *imm_len_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, strlen(literal->value));
            lir_op_t *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_STRING_NEW,
                    target,
                    2,
                    imm_c_string_operand,
                    imm_len_operand);
            list_push(c->operations, call_op);
            return target;
        }
        case TYPE_RAW_STRING: {
            return LIR_NEW_IMMEDIATE_OPERAND(TYPE_RAW_STRING, string_value, literal->value);
        }
        case TYPE_INT: {
            return LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, atoi(literal->value));
        }
        case TYPE_FLOAT: {
            return LIR_NEW_IMMEDIATE_OPERAND(TYPE_FLOAT, float_value, atof(literal->value));
        }
        case TYPE_BOOL: {
            bool bool_value = false;
            if (strcmp(literal->value, "true") == 0) {
                bool_value = true;
            }
            return LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, bool_value);
        }

        default: {
            assertf(false, "line: %d, cannot compiler literal->code", compiler_line);
        }
    }
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
        ast_var_decl *var = s->value;
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
        lir_operand_t *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_RAW_STRING, string_value, ast_closure->env_name);
        lir_operand_t *capacity_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast_closure->env_list->count);
        list_push(parent->operations,
                  lir_op_runtime_call(RUNTIME_CALL_ENV_NEW, NULL, 2, env_name_param, capacity_param));

        // 2. for set ast_ident/ast_access_env to env n
        for (int i = 0; i < ast_closure->env_list->count; ++i) {
            ast_expr *env_expr = ast_closure->env_list->take[i];
            // env_value(indirect_addr operand) or ident(var operand)
            lir_operand_t *env_target = compiler_expr(parent, *env_expr);
            // TODO indirect addr 怎么处理？
            assertf(env_target->assert_type == LIR_OPERAND_VAR, "expr_target type not var, cannot use by env set");
            lir_operand_t *env_point_target = lir_temp_var_operand(parent, type_with_point(env_expr->type, 1));
            list_push(parent->operations, lir_op_new(LIR_OPCODE_LEA, env_target, NULL, env_point_target));

            lir_operand_t *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
            lir_op_t *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_SET_ENV,
                    NULL,
                    3,
                    env_name_param,
                    env_index_param,
                    env_point_target // value
            );
            list_push(parent->operations, call_op);
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
    list_push(c->operations, lir_op_label(ast_closure->fn->name, false));

    slice_t *formal_params = slice_new();
    for (int i = 0; i < ast_closure->fn->formal_param_count; ++i) {
        ast_var_decl *param = ast_closure->fn->formal_params[i];
        // var operand 依赖 var_decl 定义的变量
        lir_var_t *var = lir_new_var_operand(c, param->ident);
        slice_push(formal_params, var);
    }

    list_push(c->operations, lir_op_new(LIR_OPCODE_FN_BEGIN, NULL, NULL,
                                        LIR_NEW_OPERAND(LIR_OPERAND_FORMAL_PARAMS, formal_params)));

    // 编译 body
    compiler_block(c, ast_closure->fn->body);

    // 尾部添加结尾 label(basic_block)
    list_push(c->operations, lir_op_label(c->end_label, true));
    // 添加函数结束指令
    list_push(c->operations, lir_op_new(LIR_OPCODE_FN_END, NULL, NULL, NULL));

    return LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, ast_closure->fn->name));
}

static void compiler_stmt(closure_t *c, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_NEW_CLOSURE: {
            compiler_closure(c, (ast_expr) {
                    .type = NULL,
                    .target_type = NULL,
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
                    .type = NULL,
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
        [AST_EXPR_ARRAY_VALUE] = compiler_array_value,
        [AST_EXPR_ENV_VALUE] = compiler_env_value,
        [AST_EXPR_BINARY] = compiler_binary,
        [AST_EXPR_UNARY] = compiler_unary,
        [AST_CALL] = compiler_call,
        [AST_EXPR_NEW_ARRAY] = compiler_new_array,
        [AST_EXPR_ACCESS_MAP] = compiler_map_value,
        [AST_EXPR_NEW_MAP] = compiler_new_map,
        [AST_EXPR_SELECT_PROPERTY] = compiler_select_property,
        [AST_EXPR_NEW_STRUCT] = compiler_new_struct,
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
            .type = NULL,
            .target_type = NULL,
            .assert_type = AST_NEW_CLOSURE,
            .value = ast,
    });

    return compiler_closures;
}

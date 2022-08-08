#include <string.h>
#include "compiler.h"
#include "symbol.h"
#include "utils/error.h"
#include "src/debug/debug.h"
#include "utils/helper.h"
#include "stdio.h"

lir_op_type ast_expr_operator_to_lir_op[] = {
        [AST_EXPR_OPERATOR_ADD] = LIR_OP_TYPE_ADD,
        [AST_EXPR_OPERATOR_SUB] = LIR_OP_TYPE_SUB,
        [AST_EXPR_OPERATOR_MUL] = LIR_OP_TYPE_MUL,
        [AST_EXPR_OPERATOR_DIV] = LIR_OP_TYPE_DIV,

        [AST_EXPR_OPERATOR_LT] = LIR_OP_TYPE_SLT,
        [AST_EXPR_OPERATOR_LTE] = LIR_OP_TYPE_SLE,
        [AST_EXPR_OPERATOR_GT] = LIR_OP_TYPE_SGT,
        [AST_EXPR_OPERATOR_GTE] = LIR_OP_TYPE_SGE,
        [AST_EXPR_OPERATOR_EQ_EQ] = LIR_OP_TYPE_SEE,
        [AST_EXPR_OPERATOR_NOT_EQ] = LIR_OP_TYPE_SNE,

        [AST_EXPR_OPERATOR_NOT] = LIR_OP_TYPE_NOT,
        [AST_EXPR_OPERATOR_NEG] = LIR_OP_TYPE_NEG,
};

int compiler_line = 0;

/**
 * @param c
 * @param ast
 * @return
 */
slice_t *compiler(ast_closure_t *ast) {
    compiler_closures = slice_new();
    compiler_closure(NULL, ast, NULL);
    return compiler_closures;
}

/**
 * target = void () {
 *
 * }
 *
 * info.f 实际存储了什么？
 *
 * 顶层 closure 不需要再次捕获外部变量
 * call make_env
 * call set_env => a target
 * call get_env => t1 target
 * @param parent
 * @param ast_closure
 * @return
 */
list *compiler_closure(closure *parent, ast_closure_t *ast_closure, lir_operand *target) {
// 捕获逃逸变量，并放在形参1中,对应的实参需要填写啥吗？
    list *parent_list = list_new();

    if (parent != NULL && ast_closure->env_count > 0) {
        // 处理 env ---------------
        // 1. make env_n by count
        lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING_RAW, string_value, ast_closure->env_name);
        lir_operand *capacity_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast_closure->env_count);
        list_push(parent_list, lir_op_runtime_call(RUNTIME_CALL_ENV_NEW, NULL, 2, env_name_param, capacity_param));

        // 2. for set ast_ident/ast_access_env to env n
        for (int i = 0; i < ast_closure->env_count; ++i) {
            ast_expr item_expr = ast_closure->env[i];

            lir_operand *expr_target = lir_new_empty_operand();
            list_append(parent_list, compiler_expr(parent, item_expr, expr_target));
            if (expr_target->type != LIR_OPERAND_TYPE_VAR) {
                error_exit("[compiler_closure] expr_target type not var, cannot use by env set");
            }

            // 1 表示指针深度为 1
            lir_operand *env_point_target = lir_new_temp_var_operand(parent, type_new_point(item_expr.type, 1));
            list_push(parent_list, lir_op_new(LIR_OP_TYPE_LEA, expr_target, NULL, env_point_target));

            lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
            lir_op *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_SET_ENV,
                    NULL,
                    3,
                    env_name_param,
                    env_index_param,
                    env_point_target
            );
            list_push(parent_list, call_op);
        }

    }

    // new 一个新的 closure ---------------
    closure *c = lir_new_closure(ast_closure);
    c->name = ast_closure->function->name; // analysis 阶段已经进行了唯一名称处理
    c->end_label = str_connect("end_", c->name);
    c->parent = parent;
    slice_push(compiler_closures, c);

    list *operates = list_new();
    // 添加 label 和 fn begin 入口
    list_push(operates, lir_op_label(ast_closure->function->name, false));
    list_push(operates, lir_op_new(LIR_OP_TYPE_FN_BEGIN, NULL, NULL, NULL));


    // 直接改写 target 而不是使用一个 move 操作
    if (target != NULL) {
        target->type = LIR_OPERAND_TYPE_VAR;
        target->value = lir_new_var_operand(c, ast_closure->function->name);
    }

    // compiler formal param
    for (int i = 0; i < ast_closure->function->formal_param_count; ++i) {
        ast_var_decl *param = ast_closure->function->formal_params[i];
        lir_local_var_decl *local = lir_new_local_var_decl(c, param->ident, param->type);
        list_push(c->formal_params, local);
    }

    // 编译 body
    list *await = compiler_block(c, ast_closure->function->body);
    list_append(operates, await);

    // 尾部添加结尾 label(basic_block)
    list_push(operates, lir_op_label(c->end_label, true));
    list_push(operates, lir_op_new(LIR_OP_TYPE_FN_END, NULL, NULL, NULL));

    c->operates = operates;

    return parent_list;
}

list *compiler_block(closure *c, slice_t *block) {
    list *operates = list_new();
    for (int i = 0; i < block->count; ++i) {
        ast_stmt *stmt = block->take[i];
        compiler_line = stmt->line;
        lir_line = stmt->line;
#ifdef DEBUG_COMPILER
        debug_stmt("COMPILER", stmt);
#endif
        list *await = compiler_stmt(c, stmt);
        list_append(operates, await);
    }

    return operates;
}

list *compiler_stmt(closure *c, ast_stmt *stmt) {
    switch (stmt->type) {
        case AST_NEW_CLOSURE: {
            return compiler_closure(c, (ast_closure_t *) stmt->stmt, NULL);
        }
        case AST_VAR_DECL: {
            return compiler_var_decl(c, (ast_var_decl *) stmt->stmt);
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            return compiler_var_decl_assign(c, (ast_var_decl_assign_stmt *) stmt->stmt);
        }
        case AST_STMT_ASSIGN: {
            return compiler_assign(c, (ast_assign_stmt *) stmt->stmt);
        }
        case AST_STMT_IF: {
            return compiler_if(c, (ast_if_stmt *) stmt->stmt);
        }
        case AST_STMT_FOR_IN: {
            return compiler_for_in(c, (ast_for_in_stmt *) stmt->stmt);
        }
        case AST_STMT_WHILE: {
            return compiler_while(c, (ast_while_stmt *) stmt->stmt);
        }
        case AST_CALL: {
            return compiler_call(c, (ast_call *) stmt->stmt, NULL);
        }
        case AST_STMT_RETURN: {
            return compiler_return(c, (ast_return_stmt *) stmt->stmt);
        }
        case AST_STMT_TYPE_DECL: {
            return list_new();
        }
        default: {
            error_printf(compiler_line, "unknown stmt");
            exit(0);
        }
    }
}

/**
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
list *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt) {
    list *operates = list_new();
    lir_local_var_decl *local = lir_new_local_var_decl(c, stmt->var_decl->ident, stmt->var_decl->type);
    list_push(c->local_var_decls, local);

    lir_operand *dst = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, lir_new_var_operand(c, stmt->var_decl->ident));
    lir_operand *src = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, stmt->expr, src));

    list_push(operates, lir_op_move(dst, src));

    return operates;
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * @param c
 * @param stmt
 * @return
 */
list *compiler_assign(closure *c, ast_assign_stmt *stmt) {
    lir_operand *left = lir_new_empty_operand();
    list *operates = compiler_expr(c, stmt->left, left);

    // 如果 left 是 var
    lir_operand *right = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, stmt->right, right));

    list_push(operates, lir_op_move(left, right));
    return operates;
}

/**
 * @param c
 * @param var_decl
 * @return
 */
list *compiler_var_decl(closure *c, ast_var_decl *var_decl) {
    lir_local_var_decl *local = lir_new_local_var_decl(c, var_decl->ident, var_decl->type);
    list_push(c->local_var_decls, local);
    return list_new();
}

/**
 * 表达式的返回值都存储在 target, target 一定为 temp var!
 * @param c
 * @param expr
 * @param target
 * @return
 */
list *compiler_expr(closure *c, ast_expr expr, lir_operand *target) {
    // target is empty
    // 简单类型表达式直接处理
    if (expr.assert_type == AST_EXPR_LITERAL) {
        return compiler_literal(c, (ast_literal *) expr.expr, target);
    }
    if (expr.assert_type == AST_EXPR_IDENT) {
        return compiler_ident(c, (ast_ident *) expr.expr, target);
    }

    lir_operand *temp = lir_new_temp_var_operand(c, expr.type);
    target->type = temp->type;
    target->value = temp->value;

    switch (expr.assert_type) {
        case AST_EXPR_BINARY: {
            return compiler_binary(c, expr, target);
        }
        case AST_EXPR_UNARY: {
            return compiler_unary(c, expr, target);
        }
        case AST_CALL: {
            return compiler_call(c, (ast_call *) expr.expr, target);
        }
        case AST_EXPR_ACCESS_LIST: {
            return compiler_array_value(c, expr, target);
        }
        case AST_EXPR_NEW_ARRAY: {
            return compiler_new_array(c, expr, target);
        }
        case AST_EXPR_ACCESS_MAP: {
            return compiler_access_map(c, expr, target);
        }
        case AST_EXPR_NEW_MAP: {
            return compiler_new_map(c, expr, target);
        }
        case AST_EXPR_SELECT_PROPERTY: {
            return compiler_select_property(c, expr, target);
        }
        case AST_EXPR_NEW_STRUCT: {
            return compiler_new_struct(c, expr, target);
        }
        case AST_EXPR_ACCESS_ENV: {
            return compiler_access_env(c, expr, target);
        }
        case AST_NEW_CLOSURE: {
            return compiler_closure(c, (ast_closure_t *) expr.expr, target);
        }
        default: {
            error_exit("[compiler_expr]unknown expr: %v", expr.assert_type);
            exit(0);
        }
    }
}

list *compiler_binary(closure *c, ast_expr expr, lir_operand *result_target) {
    ast_binary_expr *binary_expr = expr.expr;

    lir_op_type type = ast_expr_operator_to_lir_op[binary_expr->operator];

    lir_operand *left_target = lir_new_empty_operand();
    lir_operand *right_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, binary_expr->left, left_target);
    list_append(operates, compiler_expr(c, binary_expr->right, right_target));

    lir_op *binary_op = lir_op_new(type, left_target, right_target, result_target);
    list_push(operates, binary_op);

    return operates;
}

/**
 * - (1 + 1)
 * NOT first_param => result_target
 * @param c
 * @param expr
 * @param result_target
 * @return
 */
list *compiler_unary(closure *c, ast_expr expr, lir_operand *result_target) {
    list *operates = list_new();
    ast_unary_expr *unary_expr = expr.expr;

    lir_operand *first = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, unary_expr->operand, first));

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_EXPR_OPERATOR_NEG && first->type == LIR_OPERAND_TYPE_IMM) {
        lir_operand_immediate *imm = first->value;
        imm->int_value = -imm->int_value;
        // move 操作即可
        list_push(operates, lir_op_move(result_target, first));
        return operates;
    }

    if (unary_expr->operator == AST_EXPR_OPERATOR_IA) {
        // 如果 first 都不是指针，那就不给解引用直接报错，只有变量才能是指针类型
        if (first->type != LIR_OPERAND_TYPE_VAR) {
            error_exit("[compiler_unary] operator IA, but operand not var");
        }

        // 添加引用标识(var 维度，而不是变量维度,而不是 local_var_decl 维度)
        lir_operand_var *var = first->value;
        var->indirect_addr = true;
    }

    lir_op_type type = ast_expr_operator_to_lir_op[unary_expr->operator];
    lir_op *unary = lir_op_new(type, first, NULL, result_target);

    list_push(operates, unary);

    return operates;
}

list *compiler_if(closure *c, ast_if_stmt *if_stmt) {
    // 编译 condition
    lir_operand *condition_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, if_stmt->condition, condition_target);
    // 判断结果是否为 false, false 对应 else
    lir_operand *false_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false);
    lir_operand *end_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_IF_IDENT), true);
    lir_operand *alternate_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(ALTERNATE_IF_IDENT), true);

    lir_op *cmp_goto;
    if (if_stmt->alternate->count == 0) {
        cmp_goto = lir_op_new(LIR_OP_TYPE_BEQ, false_target, condition_target, end_label_operand);
    } else {
        cmp_goto = lir_op_new(LIR_OP_TYPE_BEQ, false_target, condition_target, alternate_label_operand);
    }
    list_push(operates, cmp_goto);
    list_push(operates, lir_op_unique_label(CONTINUE_IDENT));

    // 编译 consequent block
    list *consequent_list = compiler_block(c, if_stmt->consequent);
    list_push(consequent_list, lir_op_bal(end_label_operand));
    list_append(operates, consequent_list);

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        list_push(operates, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, alternate_label_operand));
        list *alternate_list = compiler_block(c, if_stmt->alternate);
        list_append(operates, alternate_list);
    }

    // 追加 end_if 标签
    list_push(operates, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, end_label_operand));

    return operates;
}

/**
 * 1.0 函数参数使用 param var 存储,按约定从左到右(type.result 为 param, type.first 为实参)
 * 1.0.1 type.operand 模仿 phi body 弄成列表的形式！
 * 2. 目前编译依旧使用 var，所以不需要考虑寄存器溢出
 * 3. 函数返回结果存储在 target 中
 *
 * call as, param => result
 * @param c
 * @param expr
 * @return
 */
list *compiler_call(closure *c, ast_call *call, lir_operand *target) {
    // call->left is debug ident
    // push 指令所有的物理寄存器入栈
    // 这里增加了无意义的堆栈和符号表,不符合简捷之道
    lir_operand *base_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, call->left, base_target);

    if (base_target->type == LIR_OPERAND_TYPE_SYMBOL_LABEL &&
        is_print_symbol(((lir_operand_symbol_label *) base_target->value)->ident)) {
        return compiler_builtin_print(c, call, ((lir_operand_symbol_label *) base_target->value)->ident);
    }

    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    for (int i = 0; i < call->actual_param_count; ++i) {
        ast_expr ast_param_expr = call->actual_params[i];
        lir_operand *param_target = lir_new_empty_operand();
        list *param_list = compiler_expr(c, ast_param_expr, param_target);
        list_append(operates, param_list);

        // 写入到 call 指令中
        params_operand->list[params_operand->count++] = param_target;
    }

    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);

    // return target
    lir_op *call_op = lir_op_new(LIR_OP_TYPE_CALL, base_target, call_params_operand, target);

    list_push(operates, call_op);

    return operates;
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
list *compiler_array_value(closure *c, ast_expr expr, lir_operand *target) {
    if (target->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[compiler_access_env] target not var, actual %d", target->type);
    }

    lir_operand_var *var = target->value;
    var->decl->ast_type.point = 1;

    ast_access_list *ast = expr.expr;
    // new tmp 是无类型的。
    // left_target.type is list[int]
    // left_target.var = runtime.make_list(size)
    // left_target.var to symbol
    // 假设是内存机器，则有 left_target.val = sp[n]
    // 但是无论如何，此时 left_target 的type 是 var, val 对应 lir_operand_var.
    // 且 lir_operand_var.ident 可以在 symbol 查出相关类型
    // var 实际上会在真实物理计算机中的内存或者其他空间拥有一份空间，并写入了一个值。
    // 即当成 var 是有值的即可！！具体的值是多少咱也不知道

    // base_target 存储 list 在内存中的基址
    lir_operand *base_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, ast->left, base_target);

    // index 为偏移量, index 值是运行时得出的，所以没有办法在编译时计算出偏移size. 虽然通过 MUL 指令可以租到，不过这种事还是交给 runtime 吧
    lir_operand *index_target = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, ast->index, index_target));

    // 如果使用是没问题的，但是外部的值没法写入进去
    lir_op *call_op = lir_op_runtime_call(
            RUNTIME_CALL_ARRAY_VALUE,
            target,
            2,
            base_target,
            index_target
    );

    var->indirect_addr = 1;
    list_push(operates, call_op);

    return operates;
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
list *compiler_new_array(closure *c, ast_expr expr, lir_operand *base_target) {
    ast_new_list *ast = expr.expr;
    list *operates = list_new();

    // 类型，容量 runtime.make_list(capacity, size)
    ast_array_decl *array_decl = ast->ast_type.value;
    lir_operand *count_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, array_decl->count);

    lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value,
                                                               (int) type_base_sizeof(ast->ast_type.base));
    lir_op *call_op = lir_op_runtime_call(
            RUNTIME_CALL_ARRAY_NEW,
            base_target,
            2,
            count_operand,
            item_size_operand
    );

    list_push(operates, call_op);

    // compiler_expr to access_list
    for (int i = 0; i < ast->count; ++i) {
        ast_expr item_expr = ast->values[i];
        lir_operand *value_target = lir_new_empty_operand();
        list_append(operates, compiler_expr(c, item_expr, value_target));

        lir_operand *refer_target = lir_new_temp_var_operand(c, type_new_point(item_expr.type, 1));
        lir_operand *index_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
        call_op = lir_op_runtime_call(
                RUNTIME_CALL_ARRAY_VALUE,
                refer_target,
                2,
                base_target,
                index_target
        );
        list_push(operates, call_op);
        list_push(operates, lir_op_move(set_indirect_addr(refer_target), value_target));
    }

    return operates;
}

/**
 * 访问环境变量
 * 1. 根据 c->env_name 得到 base_target   call GET_ENV
 * @param c
 * @param ast
 * @param target
 * @return
 */
list *compiler_access_env(closure *c, ast_expr expr, lir_operand *target) {
    ast_access_env *ast = expr.expr;
    if (target->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[compiler_access_env] target not var, actual %d", target->type);
    }

    list *operates = list_new();
    lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING_RAW, string_value, c->env_name);
    lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast->index);

    // target 通常就是一个 temp_var
    lir_operand *env_point_target = lir_new_temp_var_operand(c, type_new_point(expr.type, 1));
    lir_op *call_op = lir_op_runtime_call(
            RUNTIME_CALL_GET_ENV,
            env_point_target,
            2,
            env_name_param,
            env_index_param
    );

    // 合理怀疑 target 为 empty var, 现在将返回值移动给他，并为其添加解引用标识，再继续观察解引用标识能否传递
    lir_operand_var *var = target->value;
    var->decl->ast_type.point = 1;

    list_push(operates, call_op);
    list_push(operates, lir_op_new(LIR_OP_TYPE_MOVE, env_point_target, NULL, target));

    var->indirect_addr = true; // 添加解引用标识，推断后续的操作肯定需要这个标识
    return operates;
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
list *compiler_access_map(closure *c, ast_expr expr, lir_operand *target) {
    lir_operand_var *operand_var = target->value;
    symbol_set_temp_ident(operand_var->ident, type_new_base(TYPE_POINT));

    ast_access_map *ast = expr.expr;
    // compiler base address left_target
    lir_operand *base_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, ast->left, base_target);

    // compiler key to temp var
    lir_operand *key_target = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, ast->key, key_target));

    // runtime get offset by temp var runtime.map_offset(base, "key")
    lir_op *call_op = lir_op_runtime_call(
            RUNTIME_CALL_MAP_VALUE,
            target,
            2,
            base_target,
            key_target
    );
    list_push(operates, call_op);

    return operates;
}

/**
 * call runtime.make_map => t1 // 基础地址
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
list *compiler_new_map(closure *c, ast_expr expr, lir_operand *base_target) {
    ast_new_map *ast = expr.expr;
    list *operates = list_new();
    lir_operand *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, (int) ast->capacity);

    lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(
            TYPE_INT,
            int_value,
            (int) type_base_sizeof(ast->key_type.base) + (int) type_base_sizeof(ast->value_type.base));

    lir_op *call_op = lir_op_runtime_call(
            RUNTIME_CALL_MAP_NEW,
            base_target,
            2,
            capacity_operand,
            item_size_operand
    );
    list_push(operates, call_op);

    // 默认值初始化
    for (int i = 0; i < ast->count; ++i) {
        ast_expr key_expr = ast->values[i].key;
        lir_operand *key_target = lir_new_empty_operand();
        ast_expr value_expr = ast->values[i].value;
        lir_operand *value_target = lir_new_empty_operand();

        list_append(operates, compiler_expr(c, key_expr, key_target));
        list_append(operates, compiler_expr(c, value_expr, value_target));

        lir_operand *refer_target = lir_new_temp_var_operand(c, type_new_base(TYPE_POINT));
        call_op = lir_op_runtime_call(
                RUNTIME_CALL_MAP_VALUE,
                refer_target,
                2,
                base_target,
                key_target
        );
        list_push(operates, call_op);
        list_push(operates, lir_op_move(refer_target, value_target));

    }

    return operates;
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
list *compiler_for_in(closure *c, ast_for_in_stmt *ast) {
    lir_operand *base_target = lir_new_empty_operand();
    list *operates = compiler_expr(c, ast->iterate, base_target);

    lir_operand *count_target = lir_new_temp_var_operand(c, type_new_base(TYPE_INT)); // ?? 这个值特么存在哪里，我现在不可知呀？
    list_push(operates, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_COUNT,
            count_target,
            1,
            base_target));
    // make label
    lir_op *for_label = lir_op_unique_label(FOR_IDENT);
    lir_op *end_for_label = lir_op_unique_label(END_FOR_IDENT);
    list_push(operates, for_label);

    lir_op *cmp_goto = lir_op_new(
            LIR_OP_TYPE_BEQ,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 0),
            count_target,
            end_for_label->result);
    list_push(operates, cmp_goto);

    // 添加 label
    list_push(operates, lir_op_unique_label(CONTINUE_IDENT));

    // gen key
    // gen value
    lir_operand *key_target = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, lir_new_var_operand(c, ast->gen_key->ident));
    lir_operand *value_target = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, lir_new_var_operand(c, ast->gen_value->ident));
    list_push(operates, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_GEN_KEY,
            key_target,
            1,
            base_target));
    list_push(operates, lir_op_runtime_call(
            RUNTIME_CALL_ITERATE_GEN_VALUE,
            value_target,
            1,
            base_target));

    // block
    list_append(operates, compiler_block(c, ast->body));

    // sub count, 1 => count
    lir_op *sub_op = lir_op_new(
            LIR_OP_TYPE_SUB,
            count_target,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1),
            count_target);

    list_push(operates, sub_op);

    // goto for
    list_push(operates, lir_op_bal(for_label->result));

    list_push(operates, end_for_label);

    return operates;
}

list *compiler_while(closure *c, ast_while_stmt *ast) {
    list *operates = list_new();
    lir_op *while_label = lir_op_unique_label(WHILE_IDENT);
    list_push(operates, while_label);
    lir_operand *end_while_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_WHILE_IDENT), true);

    lir_operand *condition_target = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, ast->condition, condition_target));
    lir_op *cmp_goto = lir_op_new(
            LIR_OP_TYPE_BEQ,
            LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false),
            condition_target,
            end_while_operand);
    list_push(operates, cmp_goto);
    list_push(operates, lir_op_unique_label(CONTINUE_IDENT));

    list_append(operates, compiler_block(c, ast->body));

    list_push(operates, lir_op_bal(while_label->result));

    list_push(operates, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, end_while_operand));

    return operates;
}

list *compiler_return(closure *c, ast_return_stmt *ast) {
    list *operates = list_new();
    lir_operand *target = lir_new_empty_operand();
    if (ast->expr != NULL) {
        list *await = compiler_expr(c, *ast->expr, target);
        list_append(operates, await);
        lir_op *return_op = lir_op_new(LIR_OP_TYPE_RETURN, NULL, NULL, target);
        list_push(operates, return_op); // return op 只是做了个 mov result -> rax 的操作
    }

    list_push(operates, lir_op_bal(lir_new_label_operand(c->end_label, false)));

    return operates;
}

/**
 * mov [base+offset,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
list *compiler_select_property(closure *c, ast_expr expr, lir_operand *target) {
    ast_select_property *ast = expr.expr;
    list *operates = list_new();

    if (target->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[compiler_select_property] target not var, actual %d", target->type);
    }

    // 计算基值
    lir_operand *base_target = lir_new_empty_operand();
    list_append(operates, compiler_expr(c, ast->left, base_target));

    int offset = ast_struct_offset(ast->struct_decl, ast->property);
    lir_operand *src = lir_new_addr_operand(base_target, offset, expr.type.base);

    // target 就是个临时变量，直接修改 target 为 addr 类型,省心又省力

    set_indirect_addr(src);
    target->type = src->type;
    target->value = src->value;

    return operates;
}

/**
 * foo.bar = 1
 *
 * person baz = person {
 *  age = 100
 *  sex = true
 * }
 *
 * 无论是 baz 还是 foo.bar （经过 compiler_expr(expr.left)） 最终都会转换成结构体基址用于赋值
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
list *compiler_new_struct(closure *c, ast_expr expr, lir_operand *base_target) {
    ast_new_struct *ast = expr.expr;
    list *operates = list_new();
    ast_struct_decl *struct_decl = ast->type.value;

    int size = ast_struct_decl_size(struct_decl);
    // new struct by runtime call, base_target
    list_push(operates, lir_op_runtime_call(RUNTIME_CALL_GC_NEW, base_target, 1,
                                            LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, size)));

    // set init value
    for (int i = 0; i < ast->count; ++i) {
        ast_struct_property struct_property = ast->list[i];

        lir_operand *src_temp = lir_new_empty_operand();
        list_append(operates, compiler_expr(c, struct_property.value, src_temp));

        int offset = ast_struct_offset(struct_decl, struct_property.key);
        lir_operand *dst = lir_new_addr_operand(base_target, offset, struct_property.value.type.base);
        lir_op *move_op = lir_op_move(set_indirect_addr(dst), src_temp);
        list_push(operates, move_op);
    }

    return operates;
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
list *compiler_literal(closure *c, ast_literal *literal, lir_operand *target) {
    lir_operand *temp_operand;
    list *operates = list_new();
    switch (literal->type) {
        case TYPE_STRING: {
            lir_operand *temp = lir_new_temp_var_operand(c, type_new_base(TYPE_STRING));
            target->type = temp->type;
            target->value = temp->value;

            // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
            lir_operand *imm_string_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING_RAW, string_value, literal->value);
            lir_operand *imm_len_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, strlen(literal->value));
            lir_op *call_op = lir_op_runtime_call(
                    RUNTIME_CALL_STRING_NEW,
                    target,
                    2,
                    imm_string_operand,
                    imm_len_operand);
            list_push(operates, call_op);
            return operates;
        }
        case TYPE_STRING_RAW: {
            temp_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING_RAW, string_value, literal->value);
            break;
        }
        case TYPE_INT: {
            temp_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, atoi(literal->value));
            break;
        }
        case TYPE_FLOAT: {
            temp_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_FLOAT, float_value, atof(literal->value));
            break;
        }
        case TYPE_BOOL: {
            bool bool_value = false;
            if (strcmp(literal->value, "true") == 0) {
                bool_value = true;
            }
            temp_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, bool_value);
            break;
        }

        default: {
            error_printf(compiler_line, "cannot compiler literal->type");
            exit(0);
        }
    }

    // 执行改写类型，而不是重复 mov 操作
    target->type = temp_operand->type;
    target->value = temp_operand->value;
    return list_new();
}

list *compiler_ident(closure *c, ast_ident *ident, lir_operand *target) {
    symbol_t *s = symbol_table_get(ident->literal);
    if (s->type == SYMBOL_TYPE_FN) {
        // label
        target->type = LIR_OPERAND_TYPE_SYMBOL_LABEL;
        target->value = lir_new_label_operand(s->ident, s->is_local)->value;
    }
    if (s->type == SYMBOL_TYPE_VAR) {
        ast_var_decl *var = s->decl;
        if (s->is_local) {
            target->type = LIR_OPERAND_TYPE_VAR;
            target->value = lir_new_var_operand(c, ident->literal);
        } else {
            lir_operand_symbol_var *symbol = NEW(lir_operand_symbol_var);
            symbol->ident = ident->literal;
            symbol->type = var->type.base;
            target->type = LIR_OPERAND_TYPE_SYMBOL_VAR;
            target->value = symbol;
        }
    }

    if (s->type == SYMBOL_TYPE_CUSTOM) {
        error_exit("[compiler_ident] s->type not ident  SYMBOL_TYPE_CUSTOM");
    }

    return list_new();
}

list *compiler_builtin_print(closure *c, ast_call *call, string print_suffix) {
    list *operates = list_new();
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;
    params_operand->list[params_operand->count++] = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value,
                                                                              call->actual_param_count);
    for (int i = 0; i < call->actual_param_count; ++i) {
        ast_expr ast_param_expr = call->actual_params[i];

        lir_operand *origin_param_target = lir_new_empty_operand();
        list_append(operates, compiler_expr(c, ast_param_expr, origin_param_target));

        if (origin_param_target->type == LIR_OPERAND_TYPE_IMM) {
            lir_operand *imm_param = origin_param_target;
            lir_operand_immediate *imm = imm_param->value;
            imm->type = TYPE_INT64; // int 默认都使用了 mov asm uint 32 处理。但是这里确实需要 64 位处理。
        }

        lir_operand *param_target = lir_new_temp_var_operand(c, type_new_base(TYPE_POINT));
        lir_operand *data_type_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT8, int_value,
                                                                 ast_param_expr.type.base);

        lir_op *op = lir_op_builtin_call("builtin_new_operand", param_target, 2, data_type_param, origin_param_target);
        // 包裹 type
        list_push(operates, op);

        // 写入到 call 指令中
        params_operand->list[params_operand->count++] = param_target;
    }

    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);

    lir_operand *base_target = lir_new_label_operand("builtin_print", false);
    if (str_equal("println", print_suffix)) {
        base_target = lir_new_label_operand("builtin_println", false);
    }

    lir_op *call_op = lir_op_new(LIR_OP_TYPE_BUILTIN_CALL, base_target, call_params_operand, NULL);

    list_push(operates, call_op);

    return operates;
}

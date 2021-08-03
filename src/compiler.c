#include <string.h>
#include "compiler.h"
#include "symbol.h"
#include "src/lib/error.h"
#include "src/debug/debug.h"
#include "stdio.h"

lir_op_type ast_expr_operator_to_lir_op[] = {
    [AST_EXPR_OPERATOR_ADD] = LIR_OP_TYPE_ADD,
    [AST_EXPR_OPERATOR_SUB] = LIR_OP_TYPE_SUB,
    [AST_EXPR_OPERATOR_MUL] = LIR_OP_TYPE_MUL,
    [AST_EXPR_OPERATOR_DIV] = LIR_OP_TYPE_DIV,

    [AST_EXPR_OPERATOR_LT] = LIR_OP_TYPE_LT,
    [AST_EXPR_OPERATOR_LTE] = LIR_OP_TYPE_LTE,
    [AST_EXPR_OPERATOR_GT] = LIR_OP_TYPE_GT,
    [AST_EXPR_OPERATOR_GTE] = LIR_OP_TYPE_GTE,
    [AST_EXPR_OPERATOR_EQ_EQ] = LIR_OP_TYPE_EQ_EQ,
    [AST_EXPR_OPERATOR_NOT_EQ] = LIR_OP_TYPE_NOT_EQ,

    [AST_EXPR_OPERATOR_NOT] = LIR_OP_TYPE_NOT,
    [AST_EXPR_OPERATOR_MINUS] = LIR_OP_TYPE_MINUS,
};

int compiler_line = 0;

compiler_closures closure_list = {.count = 0};

/**
 * @param c
 * @param ast
 * @return
 */
compiler_closures compiler(ast_closure_decl *ast) {
  lir_unique_count = 0;
  lir_line = 0;
  compiler_closure(NULL, ast, NULL);
  return closure_list;
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
 * @param ast
 * @return
 */
list_op *compiler_closure(closure *parent, ast_closure_decl *ast, lir_operand *target) {
// 捕获逃逸变量，并放在形参1中,对应的实参需要填写啥吗？
  list_op *parent_list = list_op_new();

  if (parent != NULL) {
    // 处理 env ---------------
    // 1. make env_n by count
    lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, parent->env_name);
    lir_operand *capacity_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast->env_count);
    list_op_push(parent_list, lir_runtime_call(RUNTIME_CALL_MAKE_ENV, NULL, 2, env_name_param, capacity_param));

    // 2. for set ast_ident/ast_access_env to env n
    for (int i = 0; i < ast->env_count; ++i) {
      ast_expr item_expr = ast->env[i];
      lir_operand *expr_target = lir_new_temp_var_operand(item_expr.data_type);
      list_op_append(parent_list, compiler_expr(parent, item_expr, expr_target));
      lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
      lir_op *call_op = lir_runtime_call(
          RUNTIME_CALL_SET_ENV,
          NULL,
          3,
          env_name_param,
          env_index_param,
          expr_target
      );
      list_op_push(parent_list, call_op);
    }

  }

  // new 一个新的 closure ---------------
  closure *c = lir_new_closure(ast);
  c->name = ast->function->name;
  c->parent = parent;
  closure_list.list[closure_list.count++] = c;

  list_op *list = list_op_new();
  // 添加 label 入口
  list_op_push(list, lir_op_label(ast->function->name));

  // 将 label 添加到 target 中
  if (target != NULL) {
    target->type = LIR_OPERAND_TYPE_VAR;
    target->value = LIR_NEW_VAR_OPERAND(ast->function->name);
  }

  // compiler formal param
  for (int i = 0; i < ast->function->formal_param_count; ++i) {
    ast_var_decl *param = ast->function->formal_params[i];
    list_op_append(list, compiler_var_decl(c, param));
  }

  // 编译 body
  list_op *await = compiler_block(c, &ast->function->body);
  list_op_append(list, await);
  c->operates = list;

  return parent_list;
}

list_op *compiler_block(closure *c, ast_block_stmt *block) {
  list_op *list = list_op_new();
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    compiler_line = stmt.line;
    lir_line = stmt.line;
#ifdef DEBUG_COMPILER
    debug_stmt("COMPILER", stmt);
#endif
    list_op *await = compiler_stmt(c, stmt);
    list_op_append(list, await);
  }

  return list;
}

list_op *compiler_stmt(closure *c, ast_stmt stmt) {
  switch (stmt.type) {
    case AST_CLOSURE_DECL: {
      return compiler_closure(c, (ast_closure_decl *) stmt.stmt, NULL);
    }
    case AST_VAR_DECL: {
      return compiler_var_decl(c, (ast_var_decl *) stmt.stmt);
    }
    case AST_STMT_VAR_DECL_ASSIGN: {
      return compiler_var_decl_assign(c, (ast_var_decl_assign_stmt *) stmt.stmt);
    }
    case AST_STMT_ASSIGN: {
      return compiler_assign(c, (ast_assign_stmt *) stmt.stmt);
    }
    case AST_STMT_IF: {
      return compiler_if(c, (ast_if_stmt *) stmt.stmt);
    }
    case AST_STMT_FOR_IN: {
      return compiler_for_in(c, (ast_for_in_stmt *) stmt.stmt);
    }
    case AST_STMT_WHILE: {
      return compiler_while(c, (ast_while_stmt *) stmt.stmt);
    }
    case AST_CALL: {
      return compiler_call(c, (ast_call *) stmt.stmt, NULL);
    }
    case AST_STMT_RETURN: {
      return compiler_return(c, (ast_return_stmt *) stmt.stmt);
    }
    case AST_STMT_TYPE_DECL: {
      return list_op_new();
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
list_op *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt) {
  list_op *list = list_op_new();
  lir_operand *dst = lir_new_var_operand(stmt->var_decl->ident);
  lir_operand *src = lir_new_temp_var_operand(stmt->expr.data_type);
  list_op_append(list, compiler_expr(c, stmt->expr, src));

  list_op_push(list, lir_op_move(dst, src));

  return list;
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * @param c
 * @param stmt
 * @return
 */
list_op *compiler_assign(closure *c, ast_assign_stmt *stmt) {
  lir_operand *left = lir_new_temp_var_operand(stmt->left.data_type);
  list_op *list = compiler_expr(c, stmt->left, left);

  // 如果 left 是 var
  lir_operand *right = lir_new_temp_var_operand(stmt->right.data_type);
  list_op_append(list, compiler_expr(c, stmt->right, right));

  list_op_push(list, lir_op_move(left, right));
  return list;
}

/**
 * @param c
 * @param var_decl
 * @return
 */
list_op *compiler_var_decl(closure *c, ast_var_decl *var_decl) {
  return list_op_new();
}

/**
 * 表达式的返回值都存储在 target, target 一定为 temp var!
 * @param c
 * @param expr
 * @param target
 * @return
 */
list_op *compiler_expr(closure *c, ast_expr expr, lir_operand *target) {
  if (target->type != LIR_OPERAND_TYPE_VAR) {
    error_printf(compiler_line, "compiler_expr target must be lir_operand_type_var");
    exit(0);
  }

  switch (expr.type) {
    case AST_EXPR_BINARY: {
      return compiler_binary(c, expr, target);
    }
    case AST_EXPR_UNARY: {
      return compiler_unary(c, expr, target);
    }
    case AST_EXPR_LITERAL: {
      return compiler_literal(c, (ast_literal *) expr.expr, target);
    }
    case AST_EXPR_IDENT: {
      ast_ident *ident = expr.expr;
      target->type = LIR_OPERAND_TYPE_VAR;
      target->value = LIR_NEW_VAR_OPERAND(ident->literal);
      return list_op_new();
    }
    case AST_CALL: {
      return compiler_call(c, (ast_call *) expr.expr, target);
    }
    case AST_EXPR_ACCESS_LIST: {
      return compiler_access_list(c, expr, target);
    }
    case AST_EXPR_NEW_LIST: {
      return compiler_new_list(c, expr, target);
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
    case AST_CLOSURE_DECL: {
      return compiler_closure(c, (ast_closure_decl *) expr.expr, target);
    }
    default: {
      error_printf(compiler_line, "unknown expr");
      exit(0);
    }
  }
}

list_op *compiler_binary(closure *c, ast_expr expr, lir_operand *result_target) {
  ast_binary_expr *binary_expr = expr.expr;

  lir_op_type type = ast_expr_operator_to_lir_op[binary_expr->operator];

  lir_operand *left_target = lir_new_temp_var_operand(expr.data_type);
  lir_operand *right_target = lir_new_temp_var_operand(expr.data_type);
  list_op *list = compiler_expr(c, binary_expr->left, left_target);
  list_op_append(list, compiler_expr(c, binary_expr->right, right_target));

  lir_op *binary_op = lir_op_new(type, left_target, right_target, result_target);
  list_op_push(list, binary_op);

  return list;
}

/**
 * - (1 + 1)
 * NOT first_param => result_target
 * @param c
 * @param expr
 * @param result_target
 * @return
 */
list_op *compiler_unary(closure *c, ast_expr expr, lir_operand *result_target) {
  list_op *list = list_op_new();
  ast_unary_expr *unary_expr = expr.expr;

  lir_operand *first = lir_new_temp_var_operand(expr.data_type);
  list_op_append(list, compiler_expr(c, unary_expr->operand, first));

  lir_op_type type = ast_expr_operator_to_lir_op[unary_expr->operator];
  lir_op *unary = lir_op_new(type, first, NULL, result_target);

  list_op_push(list, unary);

  return list;
}

list_op *compiler_if(closure *c, ast_if_stmt *if_stmt) {
  // 编译 condition
  lir_operand *condition_target = lir_new_temp_var_operand(if_stmt->condition.data_type);
  list_op *list = compiler_expr(c, if_stmt->condition, condition_target);
  // 判断结果是否为 false, false 对应 else
  lir_operand *false_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false);
  lir_operand *end_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_IF_IDENT));
  lir_operand *alternate_label_operand = lir_new_label_operand(LIR_UNIQUE_NAME(ALTERNATE_IF_IDENT));

  lir_op *cmp_goto;
  if (if_stmt->alternate.count == 0) {
    cmp_goto = lir_op_new(LIR_OP_TYPE_CMP_GOTO, false_target, condition_target, end_label_operand);
  } else {
    cmp_goto = lir_op_new(LIR_OP_TYPE_CMP_GOTO, false_target, condition_target, alternate_label_operand);
  }
  list_op_push(list, cmp_goto);
  list_op_push(list, lir_op_unique_label(CONTINUE_IDENT));

  // 编译 consequent block
  list_op *consequent_list = compiler_block(c, &if_stmt->consequent);
  list_op_push(consequent_list, lir_op_goto(end_label_operand));
  list_op_append(list, consequent_list);

  // 编译 alternate block
  if (if_stmt->alternate.count != 0) {
    list_op_push(list, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, alternate_label_operand));
    list_op *alternate_list = compiler_block(c, &if_stmt->alternate);
    list_op_append(list, alternate_list);
  }

  // 追加 end_if 标签
  list_op_push(list, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, end_label_operand));

  return list;
}

/**
 * 1.0 函数参数使用 param var 存储,按约定从左到右(op.result 为 param, op.first 为实参)
 * 1.0.1 op.operand 模仿 phi body 弄成列表的形式！
 * 2. 目前编译依旧使用 var，所以不需要考虑寄存器溢出
 * 3. 函数返回结果存储在 target 中
 *
 * call name, param => result
 * @param c
 * @param expr
 * @return
 */
list_op *compiler_call(closure *c, ast_call *call, lir_operand *target) {
  // push 指令所有的物理寄存器入栈
//  lir_operand *base_target = NEW(lir_operand);
  lir_operand *base_target = lir_new_temp_var_operand(TYPE_NEW_POINT());

  list_op *list = compiler_expr(c, call->left, base_target);

//  if (base_target->type != LIR_OPERAND_TYPE_VAR) {
//    error_printf(compiler_line, "function call left must confirm label!");
//    exit(0);
//  }

  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;

  for (int i = 0; i < call->actual_param_count; ++i) {
    ast_expr ast_param_expr = call->actual_params[i];

    lir_operand *param_target = lir_new_temp_var_operand(ast_param_expr.data_type);

    list_op *param_list_op = compiler_expr(c, ast_param_expr, param_target);
    list_op_append(list, param_list_op);

    // 写入到 call 指令中
    params_operand->list[params_operand->count++] = param_target;
  }

  lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);

  // return target
  lir_op *call_op = lir_op_new(LIR_OP_TYPE_CALL, base_target, call_params_operand, target);

  list_op_push(list, call_op);

  return list;
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
list_op *compiler_access_list(closure *c, ast_expr expr, lir_operand *target) {
  lir_operand_var *operand_var = target->value;
  symbol_set_temp_ident(operand_var->ident, TYPE_NEW_POINT());

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
  lir_operand *base_target = lir_new_temp_var_operand(ast->left.data_type);
  list_op *list = compiler_expr(c, ast->left, base_target);

  // index 为偏移量, index 值是运行时得出的，所以没有办法在编译时计算出偏移size. 虽然通过 MUL 指令可以租到，不过这种事还是交给 runtime 吧
  lir_operand *index_target = lir_new_temp_var_operand(ast->index.data_type);
  list_op_append(list, compiler_expr(c, ast->index, index_target));

  lir_op *call_op = lir_runtime_call(
      RUNTIME_CALL_LIST_VALUE,
      target,
      2,
      base_target,
      index_target
  );

  list_op_push(list, call_op);

  return list;
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
list_op *compiler_new_list(closure *c, ast_expr expr, lir_operand *base_target) {
  ast_new_list *ast = expr.expr;
  list_op *list = list_op_new();

  // 类型，容量 runtime.make_list(capacity, size)
  lir_operand *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, (int) ast->capacity);
  lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, (int) type_sizeof(ast->type));
  lir_op *call_op = lir_runtime_call(
      RUNTIME_CALL_MAKE_LIST,
      base_target,
      2,
      capacity_operand,
      item_size_operand
  );

  list_op_push(list, call_op);

  // compiler_expr to access_list
  for (int i = 0; i < ast->count; ++i) {
    ast_expr item = ast->values[i];
    lir_operand *value_target = lir_new_temp_var_operand(item.data_type);
    list_op_append(list, compiler_expr(c, item, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand(TYPE_NEW_POINT());
    lir_operand *index_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, i);
    call_op = lir_runtime_call(
        RUNTIME_CALL_LIST_VALUE,
        refer_target,
        2,
        base_target,
        index_target
    );
    list_op_push(list, call_op);
    list_op_push(list, lir_op_move(refer_target, value_target));
  }

  return list;
}

/**
 * 访问环境变量
 * 1. 根据 c->env_name 得到 base_target   call GET_ENV
 * @param c
 * @param ast
 * @param target
 * @return
 */
list_op *compiler_access_env(closure *c, ast_expr expr, lir_operand *target) {
  ast_access_env *ast = expr.expr;
  list_op *list = list_op_new();
  lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, c->env_name);
  lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, ast->index);

  lir_op *call_op = lir_runtime_call(
      RUNTIME_CALL_GET_ENV,
      target,
      2,
      env_name_param,
      env_index_param
  );

  list_op_push(list, call_op);

  return list;
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
list_op *compiler_access_map(closure *c, ast_expr expr, lir_operand *target) {
  lir_operand_var *operand_var = target->value;
  symbol_set_temp_ident(operand_var->ident, TYPE_NEW_POINT());

  ast_access_map *ast = expr.expr;
  // compiler base address left_target
  lir_operand *base_target = lir_new_temp_var_operand(ast->left.data_type);
  list_op *list = compiler_expr(c, ast->left, base_target);

  // compiler key to temp var
  lir_operand *key_target = lir_new_temp_var_operand(ast->key.data_type);
  list_op_append(list, compiler_expr(c, ast->key, key_target));

  // runtime get offset by temp var runtime.map_offset(base, "key")
  lir_op *call_op = lir_runtime_call(
      RUNTIME_CALL_MAP_VALUE,
      target,
      2,
      base_target,
      key_target
  );
  list_op_push(list, call_op);

  return list;
}

/**
 * call runtime.make_map => t1 // 基础地址
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
list_op *compiler_new_map(closure *c, ast_expr expr, lir_operand *base_target) {
  ast_new_map *ast = expr.expr;
  list_op *list = list_op_new();
  lir_operand *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, (int) ast->capacity);

  lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(
      TYPE_INT,
      int_value,
      (int) type_sizeof(ast->key_type) + (int) type_sizeof(ast->value_type));

  lir_op *call_op = lir_runtime_call(
      RUNTIME_CALL_MAKE_MAP,
      base_target,
      2,
      capacity_operand,
      item_size_operand
  );
  list_op_push(list, call_op);

  // 默认值初始化
  for (int i = 0; i < ast->count; ++i) {
    ast_expr key_expr = ast->values[i].key;
    lir_operand *key_target = lir_new_temp_var_operand(key_expr.data_type);
    ast_expr value_expr = ast->values[i].value;
    lir_operand *value_target = lir_new_temp_var_operand(value_expr.data_type);

    list_op_append(list, compiler_expr(c, key_expr, key_target));
    list_op_append(list, compiler_expr(c, value_expr, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand(TYPE_NEW_POINT());
    call_op = lir_runtime_call(
        RUNTIME_CALL_MAP_VALUE,
        refer_target,
        2,
        base_target,
        key_target
    );
    list_op_push(list, call_op);
    list_op_push(list, lir_op_move(refer_target, value_target));

  }

  return list;
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
list_op *compiler_for_in(closure *c, ast_for_in_stmt *ast) {
  lir_operand *base_target = lir_new_temp_var_operand(ast->iterate.data_type);
  list_op *list = compiler_expr(c, ast->iterate, base_target);

  lir_operand *count_target = lir_new_temp_var_operand(TYPE_NEW_INT()); // ?? 这个值特么存在哪里，我现在不可知呀？
  list_op_push(list, lir_runtime_call(
      RUNTIME_CALL_ITERATE_COUNT,
      count_target,
      1,
      base_target));
  // make label
  lir_op *for_label = lir_op_unique_label(FOR_IDENT);
  lir_op *end_for_label = lir_op_unique_label(END_FOR_IDENT);
  list_op_push(list, for_label);

  lir_op *cmp_goto = lir_op_new(
      LIR_OP_TYPE_CMP_GOTO,
      LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 0),
      count_target,
      end_for_label->result);
  list_op_push(list, cmp_goto);

  // 添加 label
  list_op_push(list, lir_op_unique_label(CONTINUE_IDENT));

  // gen key
  // gen value
  lir_operand *key_target = lir_new_var_operand(ast->gen_key->ident);
  lir_operand *value_target = lir_new_var_operand(ast->gen_value->ident);
  list_op_push(list, lir_runtime_call(
      RUNTIME_CALL_ITERATE_GEN_KEY,
      key_target,
      1,
      base_target));
  list_op_push(list, lir_runtime_call(
      RUNTIME_CALL_ITERATE_GEN_VALUE,
      value_target,
      1,
      base_target));

  // block
  list_op_append(list, compiler_block(c, &ast->body));

  // sub count, 1 => count
  lir_op *sub_op = lir_op_new(
      LIR_OP_TYPE_SUB,
      count_target,
      LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int_value, 1),
      count_target);

  list_op_push(list, sub_op);

  // goto for
  list_op_push(list, lir_op_goto(for_label->result));

  list_op_push(list, end_for_label);

  return list;
}

list_op *compiler_while(closure *c, ast_while_stmt *ast) {
  list_op *list = list_op_new();
  lir_op *while_label = lir_op_unique_label(WHILE_IDENT);
  list_op_push(list, while_label);
  lir_operand *end_while_operand = lir_new_label_operand(LIR_UNIQUE_NAME(END_WHILE_IDENT));

  lir_operand *condition_target = lir_new_temp_var_operand(ast->condition.data_type);
  list_op_append(list, compiler_expr(c, ast->condition, condition_target));
  lir_op *cmp_goto = lir_op_new(
      LIR_OP_TYPE_CMP_GOTO,
      LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool_value, false),
      condition_target,
      end_while_operand);
  list_op_push(list, cmp_goto);
  list_op_push(list, lir_op_unique_label(CONTINUE_IDENT));

  list_op_append(list, compiler_block(c, &ast->body));

  list_op_push(list, lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, end_while_operand));

  return list;
}

list_op *compiler_return(closure *c, ast_return_stmt *ast) {
  list_op *list = list_op_new();
  lir_operand *target = lir_new_temp_var_operand(ast->expr.data_type);
  list_op *await = compiler_expr(c, ast->expr, target);
  list_op_append(list, await);

  lir_op *return_op = lir_op_new(LIR_OP_TYPE_RETURN, target, NULL, NULL);
  list_op_push(list, return_op);

  return list;
}

/**
 * mov [base+offset,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
list_op *compiler_select_property(closure *c, ast_expr expr, lir_operand *target) {
  ast_select_property *ast = expr.expr;
  list_op *list = list_op_new();
  // 计算基值
  lir_operand *base_target = lir_new_temp_var_operand(ast->left.data_type);
  list_op_append(list, compiler_expr(c, ast->left, base_target));
  size_t offset = struct_offset(ast->struct_decl, ast->property);

  lir_operand *src = lir_new_memory_operand(base_target, offset, ast->struct_property->length);

  lir_op *move_op = lir_op_move(target, src);
  list_op_push(list, move_op);

  return list;
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
list_op *compiler_new_struct(closure *c, ast_expr expr, lir_operand *base_target) {
  ast_new_struct *ast = expr.expr;
  list_op *list = list_op_new();
  ast_struct_decl *struct_decl = ast->type.value;
  for (int i = 0; i < ast->count; ++i) {
    ast_struct_property struct_property = ast->list[i];

    lir_operand *src = lir_new_temp_var_operand(struct_property.value.data_type);
    list_op_append(list, compiler_expr(c, struct_property.value, src));

    size_t offset = struct_offset(struct_decl, struct_property.key);
    lir_operand *dst = lir_new_memory_operand(base_target, offset, struct_property.length);
    lir_op *move_op = lir_op_move(dst, src);
    list_op_push(list, move_op);
  }

  return list;
}

list_op *compiler_literal(closure *c, ast_literal *literal, lir_operand *target) {
  lir_operand *temp_operand;
  switch (literal->type) {
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
    case TYPE_STRING: {
      temp_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string_value, literal->value);
      break;
    }
    default: {
      error_printf(compiler_line, "cannot compiler literal->type");
      exit(0);
    }
  }

  target->type = temp_operand->type;
  target->value = temp_operand->value;
  return list_op_new();
}

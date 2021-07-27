#include "compiler.h"
#include "symbol.h"
#include "src/lib/error.h"
#include "src/debug/debug.c"

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

/**
 * @param c
 * @param ast
 * @return
 */
list_op *compiler(ast_closure_decl *ast) {
  compiler_line = 0;
  return compiler_closure(NULL, ast);
}

/**
 * 顶层 closure 不需要再次捕获外部变量
 * call make_env
 * call set_env => a target
 * call get_env => t1 target
 * @param parent
 * @param ast
 * @return
 */
list_op *compiler_closure(closure *parent, ast_closure_decl *ast) {
// 捕获逃逸变量，并放在形参1中,对应的实参需要填写啥吗？
  list_op *parent_list = list_op_new();

  if (parent != NULL) {
    // 处理 env ---------------
    // 1. make env_n by count
    // TODO get env name by closure
    lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string, parent->env_name);
    lir_operand *capacity_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, ast->env_count);
    list_op_push(parent_list, lir_runtime_call(RUNTIME_CALL_MAKE_ENV, NULL, 2, env_name_param, capacity_param));

    // 2. for set ast_ident/ast_access_env to env n
    for (int i = 0; i < ast->env_count; ++i) {
      ast_expr item_expr = ast->env[i];
      lir_operand *expr_target = lir_new_temp_var_operand();
      list_op_append(parent_list, compiler_expr(parent, item_expr, expr_target));
      lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, i);
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
  list_op *child_list = list_op_new();

  // 添加 label 入口
  list_op_push(child_list, lir_op_label(ast->function->name));

  // compiler formal param
  for (int i = 0; i < ast->function->formal_param_count; ++i) {
    ast_var_decl *param = ast->function->formal_params[i];
    list_op_append(child_list, compiler_var_decl(c, param));
  }

  // 编译 body
  list_op_append(child_list, compiler_block(c, &ast->function->body));
  c->operates = child_list;

  return parent_list;
}

list_op *compiler_block(closure *c, ast_block_stmt *block) {
  list_op *list = list_op_new();
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    compiler_line = stmt.line;
    list_op_append(list, compiler_stmt(c, stmt));
  }

  return list;
}

list_op *compiler_stmt(closure *c, ast_stmt stmt) {
  switch (stmt.type) {
    case AST_CLOSURE_DECL: {
      return compiler_closure(c, (ast_closure_decl *) stmt.stmt);
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
      lir_operand *temp_target = lir_new_temp_var_operand();
      return compiler_call(c, (ast_call *) stmt.stmt, temp_target);
    }
    default: {
      error_printf(compiler_line, "unknown stmt: %s", ast_stmt_expr_type_to_debug[stmt.type]);
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
  lir_operand *target = lir_new_var_operand(stmt->var_decl->ident);
  return compiler_expr(c, stmt->expr, target);
}

list_op *compiler_assign(closure *c, ast_assign_stmt *stmt) {
  // left 可能是一个数组调用，可能是一个对象访问，也有可能是一个标量赋值
  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, stmt->left, left_target);
  list_op_append(list, compiler_expr(c, stmt->right, right_target));

  lir_op *move_op = lir_op_new(LIR_OP_TYPE_MOVE);
  move_op->result = *left_target;
  move_op->first = *right_target;
  list_op_push(list, move_op);

  return list;
}

list_op *compiler_var_decl(closure *c, ast_var_decl *var_decl) {
  return NULL;
}

/**
 * 表达式的返回值都存储在 target
 * @param c
 * @param expr
 * @param target
 * @return
 */
list_op *compiler_expr(closure *c, ast_expr expr, lir_operand *target) {
  switch (expr.type) {
    case AST_EXPR_BINARY: {
      return compiler_binary(c, (ast_binary_expr *) expr.expr, target);
    }
    case AST_EXPR_UNARY: {
      return compiler_unary(c, (ast_unary_expr *) expr.expr, target);
    }
    case AST_EXPR_LITERAL: {
      // 直接重写 result target，不生成操作
      ast_literal *literal = (ast_literal *) expr.expr;
      if (literal->type == TYPE_INT || literal->type == TYPE_FLOAT
          || literal->type == TYPE_BOOL || literal->type == TYPE_STRING) {
        lir_operand_immediate *immediate = malloc(sizeof(lir_operand_immediate));
        immediate->type = literal->type;
        immediate->value = literal->value;
        target->type = LIR_OPERAND_TYPE_IMMEDIATE;
        target->value = immediate;
      }

      return list_op_new();
    }
    case AST_EXPR_IDENT: {
      ast_ident *ident = expr.expr;
      target->type = LIR_OPERAND_TYPE_VAR;
      target->value = lir_new_var_operand(ident->literal);

      return list_op_new();
    }
    case AST_CALL: {
      return compiler_call(c, (ast_call *) expr.expr, target);
    }
    case AST_EXPR_ACCESS_LIST: {
      return compiler_access_list(c, (ast_access_list *) expr.expr, target);
    }
    case AST_EXPR_NEW_LIST: {
      return compiler_new_list(c, (ast_new_list *) expr.expr, target);
    }
    case AST_EXPR_ACCESS_MAP: {
      return compiler_access_map(c, (ast_access_map *) expr.expr, target);
    }
    case AST_EXPR_NEW_MAP: {
      return compiler_new_map(c, (ast_new_map *) expr.expr, target);
    }
    case AST_EXPR_SELECT_PROPERTY: {
      return compiler_select_property(c, (ast_select_property *) expr.expr, target);
    }
    case AST_EXPR_NEW_STRUCT: {

    }
    case AST_EXPR_ACCESS_ENV: {
      return compiler_access_env(c, (ast_access_env *) expr.expr, target);
    }
    default: {
      error_printf(compiler_line, "unknown expr: %s", ast_stmt_expr_type_to_debug[expr.type]);
      exit(0);
    }
  }
}

list_op *compiler_binary(closure *c, ast_binary_expr *expr, lir_operand *result_target) {
  lir_op_type type = ast_expr_operator_to_lir_op[expr->operator];

  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, expr->left, left_target);
  list_op_append(list, compiler_expr(c, expr->right, right_target));
  lir_op *binary_op = lir_op_new(type);
  binary_op->result = *result_target;
  binary_op->first = *left_target;
  binary_op->second = *right_target;
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
list_op *compiler_unary(closure *c, ast_unary_expr *expr, lir_operand *result_target) {
  lir_op_type type = ast_expr_operator_to_lir_op[expr->operator];
  list_op *list = list_op_new();
  lir_op *not = lir_op_new(type);

  lir_operand *first = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, expr->operand, first));
  not->first = *first;
  not->result = *result_target;
  list_op_push(list, not);

  return list;
}

list_op *compiler_if(closure *c, ast_if_stmt *if_stmt) {
  // 编译 condition
  lir_operand *condition_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, if_stmt->condition, condition_target);
  // 判断结果是否为 false, false 对应 else
  lir_op *cmp_goto = lir_op_new(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool, false);
  cmp_goto->second = *condition_target;

  lir_op *end_label = lir_op_label("end_if");
  lir_op *alternate_label = lir_op_label("alternate_if");
  if (if_stmt->alternate.count == 0) {
    cmp_goto->result = end_label->result;
  } else {
    cmp_goto->result = alternate_label->result;
  }
  list_op_push(list, cmp_goto);
  list_op_push(list, lir_op_label("continue"));

  // 编译 consequent block
  list_op *consequent_list = compiler_block(c, &if_stmt->consequent);
  list_op_push(consequent_list, lir_op_goto(&end_label->result));
  list_op_append(list, consequent_list);

  // 编译 alternate block
  if (if_stmt->alternate.count != 0) {
    list_op_push(list, alternate_label);
    list_op *alternate_list = compiler_block(c, &if_stmt->alternate);
    list_op_append(list, alternate_list);
  }
  // 追加 end_if 标签
  list_op_push(list, end_label);

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
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, call->left, base_target);

  lir_op *call_op = lir_op_new(LIR_OP_TYPE_CALL);
  call_op->first = *base_target;

  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;

  for (int i = 0; i < call->actual_param_count; ++i) {
    ast_expr ast_param_expr = call->actual_params[i];

    lir_operand *param_target = lir_new_temp_var_operand();

    list_op *param_list_op = compiler_expr(c, ast_param_expr, param_target);
    list_op_append(list, param_list_op);

    // 写入到 call 指令中
    params_operand->list[params_operand->count++] = param_target;
  }

  lir_operand call_params_operand = {
      .type= LIR_OPERAND_TYPE_ACTUAL_PARAM,
      .value = params_operand
  };
  call_op->second = call_params_operand; // 函数参数

  // return target
  call_op->result = *target; // 返回结果

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
list_op *compiler_access_list(closure *c, ast_access_list *ast, lir_operand *target) {
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
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->left, base_target);

  // index 为偏移量, index 值是运行时得出的，所以没有办法在编译时计算出偏移size. 虽然通过 MUL 指令可以租到，不过这种事还是交给 runtime 吧
  lir_operand *index_target = lir_new_temp_var_operand();
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
list_op *compiler_new_list(closure *c, ast_new_list *ast, lir_operand *base_target) {
  list_op *list = list_op_new();

  // 类型，容量 runtime.make_list(capacity, size)
  lir_operand *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, (int) ast->capacity);
  lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, (int) type_sizeof(ast->type));
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
    ast_expr expr = ast->values[i];
    lir_operand *value_target = lir_new_temp_var_operand();
    list_op_append(list, compiler_expr(c, expr, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand();
    lir_operand *index_target = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, i);
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
list_op *compiler_access_env(closure *c, ast_access_env *ast, lir_operand *target) {
  list_op *list = list_op_new();
  lir_operand *env_name_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_STRING, string, c->env_name);
  lir_operand *env_index_param = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, ast->index);

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
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
list_op *compiler_access_map(closure *c, ast_access_map *ast, lir_operand *target) {
  // compiler base address left_target
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->left, base_target);

  // compiler key to temp var
  lir_operand *key_target = lir_new_temp_var_operand();
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

  return NULL;
}

/**
 * call runtime.make_map => t1 // 基础地址
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
list_op *compiler_new_map(closure *c, ast_new_map *ast, lir_operand *base_target) {
  list_op *list = list_op_new();
  lir_operand *capacity_operand = LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, (int) ast->capacity);

  lir_operand *item_size_operand = LIR_NEW_IMMEDIATE_OPERAND(
      TYPE_INT,
      int,
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
    lir_operand *key_target = lir_new_temp_var_operand();
    ast_expr value_expr = ast->values[i].value;
    lir_operand *value_target = lir_new_temp_var_operand();

    list_op_append(list, compiler_expr(c, key_expr, key_target));
    list_op_append(list, compiler_expr(c, value_expr, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand();
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
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->iterate, base_target);

  lir_operand *count_target = lir_new_temp_var_operand(); // ?? 这个值特么存在哪里，我现在不可知呀？
  list_op_push(list, lir_runtime_call(
      RUNTIME_CALL_ITERATE_COUNT,
      count_target,
      1,
      base_target));
  // make label
  lir_op *for_label = lir_op_label("for");
  lir_op *end_for_label = lir_op_label("end_for");
  list_op_push(list, for_label);
  lir_op *cmp_goto = lir_op_new(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, 0);
  cmp_goto->second = *count_target;
  cmp_goto->result = end_for_label->result;
  list_op_push(list, cmp_goto);

  // 添加 label
  list_op_push(list, lir_op_label("continue"));

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
  lir_op *sub_op = lir_op_new(LIR_OP_TYPE_SUB);
  sub_op->first = *count_target;
  sub_op->second = *LIR_NEW_IMMEDIATE_OPERAND(TYPE_INT, int, 1);
  sub_op->result = *count_target;
  list_op_push(list, sub_op);

  // goto for
  list_op_push(list, lir_op_goto(&for_label->result));

  list_op_push(list, lir_op_label("end_for"));

  return list;
}

list_op *compiler_while(closure *c, ast_while_stmt *ast) {
  list_op *list = list_op_new();
  lir_op *while_label = lir_op_label("while");
  lir_op *end_while_label = lir_op_label("end_while");
  list_op_push(list, while_label);

  lir_operand *condition_target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->condition, condition_target));
  lir_op *cmp_goto = lir_op_new(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *LIR_NEW_IMMEDIATE_OPERAND(TYPE_BOOL, bool, false);
  cmp_goto->second = *condition_target;
  cmp_goto->result = end_while_label->result;
  list_op_push(list, cmp_goto);
  list_op_push(list, lir_op_label("continue"));

  list_op_append(list, compiler_block(c, &ast->body));

  list_op_push(list, end_while_label);

  return list;
}

list_op *compiler_return(closure *c, ast_return_stmt *ast) {
  list_op *list = list_op_new();
  lir_operand *target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->expr, target));

  lir_op *return_op = lir_op_new(LIR_OP_TYPE_RETURN);
  return_op->result = *target;
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
list_op *compiler_select_property(closure *c, ast_select_property *ast, lir_operand *target) {
  list_op *list = list_op_new();
  // 计算基值
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->left, base_target));
  size_t offset = struct_offset(ast->struct_decl, ast->property);

  lir_operand *src = lir_new_memory_operand(base_target, offset, ast->struct_property->length);

  lir_op *move_op = lir_op_move(target, src);
  list_op_push(list, move_op);

  return list;
}

/**
 * struct 的空间是什么时候申请的？ var a = struct{} // 此时申请的
 * a.test = 1
 * 此时赋值如何知道基址是多少？赋值又是赋到哪里？
 * @param c
 * @param ast
 * @param target
 * @return
 */
list_op *compiler_new_struct(closure *c, ast_new_struct *ast, lir_operand *target) {

  return NULL;
}






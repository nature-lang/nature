#include "optimize.h"
#include "string.h"

void optimize_assign(ast_assign_stmt *assign);
void optimize_if(ast_if_stmt *);
/**
 * 1. unique var
 * 2. type check
 * 3. type inference
 */
void optimize(ast_function_decl *main) {
  // 初始化
  table_init(&type_table);
  table_init(&var_table);

  // main
  ast_closure_decl *closure = optimize_function_decl(main);
}

void optimize_block(ast_block_stmt *block) {
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    // switch 结构导向优化
    switch (stmt.type) {
      case AST_VAR_DECL: {
        optimize_var_decl((ast_var_decl *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        optimize_var_decl_assign((ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_FUNCTION_DECL: {
        ast_closure_decl *closure = optimize_function_decl((ast_function_decl *) stmt.stmt);

        ast_stmt closure_stmt;
        closure_stmt.type = AST_CLOSURE_DECL;
        closure_stmt.stmt = (void *) closure;
        block->list[i] = closure_stmt; // 值复制传递，不用担心指针空悬问题
        break;
      }
      case AST_CALL: {
        optimize_call_function((ast_call *) stmt.stmt);
        break;
      }
      case AST_STMT_ASSIGN: {
        optimize_assign((ast_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_IF: {
        optimize_if((ast_if_stmt *) stmt.stmt);
        break;
      }
    }
  }
}

void optimize_if(ast_if_stmt *if_stmt) {
  optimize_expr(&if_stmt->condition);
  optimize_begin_scope();
  optimize_block(&if_stmt->consequent);
  optimize_end_scope();

  optimize_begin_scope();
  optimize_block(&if_stmt->alternate);
  optimize_end_scope();
}

void optimize_assign(ast_assign_stmt *assign) {
  optimize_expr(&assign->left);
  optimize_expr(&assign->right);
}

void optimize_call_function(ast_call *call) {
  // 函数地址改写
  optimize_expr(&call->target_expr);

  // 实参改写
  for (int i = 0; i < call->actual_param_count; ++i) {
    ast_expr *actual_param = &call->actual_params[i];
    optimize_expr(actual_param);
  }
}

void optimize_var_decl(ast_var_decl *var_decl) {
  // TODO 判断同当前块作用域内是否重复定义

  optimize_local_var *local = optimize_new_local(var_decl->type, var_decl->ident);

  // 改写
  var_decl->ident = local->unique_ident;
}

void optimize_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign) {
  optimize_local_var *local = optimize_new_local(var_decl_assign->type, var_decl_assign->ident);

  // 改写
  var_decl_assign->ident = local->unique_ident;

  optimize_expr(&var_decl_assign->expr);
}

ast_closure_decl *optimize_function_decl(ast_function_decl *function) {
  // 函数名称改写
  optimize_local_var *function_local = optimize_new_local(BASE_TYPE_CLOSURE, function->name);
  function->name = function_local->unique_ident;

  // 开启一个新的 function 作用域
  optimize_function_begin();

  // 函数参数改写, 参数 0 预留给 env
  formal_param *env = &function->formal_params[0];
  env->type = BASE_TYPE_ENV; // env 类型
  env->ident = BASE_TYPE_ENV;
  optimize_local_var *env_local = optimize_new_local(env->type, env->ident);
  env->ident = env_local->unique_ident;
  current_function->env_unique_name = env->ident;

  for (int i = 1; i < function->formal_param_count; ++i) {
    formal_param *param = &function->formal_params[i];
    // 注册并改写
    optimize_local_var *param_local = optimize_new_local(param->type, param->ident);
    param->ident = param_local->unique_ident;
  }

  // 编译 block, 其中进行了自由变量的捕获/改写和局部变量改写
  optimize_block(&function->body);

  ast_closure_decl *closure = malloc(sizeof(ast_closure_decl));

  // 注意，自由变量捕捉是基于 current_function->parent 进行的
  for (int i = 0; i < current_function->free_count; ++i) {
    optimize_free_var free_var = current_function->frees[i];
    ast_expr expr = closure->env[i];
    if (free_var.is_local) {
      // ast_ident 表达式
      expr.type = AST_EXPR_TYPE_IDENT;
      ast_ident ident = current_function->parent->locals[free_var.index]->unique_ident;
      expr.expr = ident;
    } else {
      // ast_env_index 表达式
      expr.type = AST_EXPR_TYPE_ENV_INDEX;
      ast_env_index *env_index = malloc(sizeof(ast_env_index));
      env_index->env = current_function->parent->env_unique_name;
      env_index->index = free_var.index;
      expr.expr = env_index;
    }
  }
  closure->function = function;

  optimize_function_end();
  return closure;
}

void optimize_begin_scope() {
  current_function->scope_depth++;
}

void optimize_end_scope() {
  current_function->scope_depth--;

  // 块内局部变量检查
}

optimize_local_var *optimize_new_local(string type, string ident) {
  // unique
  string unique_ident = unique_var_ident(ident);

  optimize_local_var *local = malloc(sizeof(optimize_local_var));
  local->ident = ident;
  local->unique_ident = unique_ident;
  local->scope_depth = current_function->scope_depth;
  local->type_name = type;

  // 添加 locals
  current_function->locals[current_function->local_count++] = local;

  // 添加 var_table
  table_set(&var_table, unique_ident, (void *) local);

  return local;
}

void optimize_expr(ast_expr *expr) {
  switch (expr->type) {
    case AST_EXPR_TYPE_BINARY: {
      optimize_binary((ast_binary_expr *) expr->expr);
      break;
    };
    case AST_EXPR_TYPE_LITERAL: {
      optimize_literal((ast_literal *) expr->expr);
      break;
    }
    case AST_EXPR_TYPE_IDENT: {
      optimize_ident(expr);
      break;
    };
    case AST_CALL: {
      optimize_call_function((ast_call *) expr->expr);
      break;
    }
    case AST_FUNCTION_DECL: {
      ast_closure_decl *closure = optimize_function_decl((ast_function_decl *) expr->expr);

      ast_expr closure_expr;
      closure_expr.type = AST_CLOSURE_DECL;
      closure_expr.expr = (void *) closure;
      // 重写 expr
      *expr = closure_expr;
      break;
    }
  }
}

void optimize_ident(ast_expr *expr) {
  ast_ident *ident = (ast_ident *) expr->expr;
  // 在本地作用域中查找变量
  for (int i = 0; i < current_function->local_count; ++i) {
    optimize_local_var *local = current_function->locals[i];
    if (strcmp(*ident, local->ident) == 0) {
      // 在本地变量中找到,则进行简单改写
      *ident = local->unique_ident;
      return;
    }
  }

  // 非本地作用域变量则向上查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
  int8_t free_var_index = optimize_resolve_free(current_function, ident);
  // TODO 错误处理
  if (free_var_index == -1) {
    exit(1);
  }
  // 改写
  expr->type = AST_EXPR_TYPE_ENV_INDEX;
  ast_env_index *env_index = malloc(sizeof(ast_env_index));
  env_index->env = current_function->env_unique_name;
  env_index->index = free_var_index;
  expr->expr = env_index;
}

int8_t optimize_resolve_free(optimize_function *f, ast_ident *ident) {
  if (f->parent == NULL) {
    return -1;
  }

  for (int i = 0; i < f->parent->local_count; ++i) {
    optimize_local_var *local = f->parent->locals[i];
    if (strcmp(*ident, local->ident) == 0) {
      f->parent->locals[i]->is_capture = true;

      return optimize_push_free(f, true, i);
    }
  }

  // 继续向上递归查询
  int8_t parent_free_index = optimize_resolve_free(f->parent, ident);
  if (parent_free_index != -1) {
    return optimize_push_free(f, parent_free_index, false);
  }

  return -1;
}




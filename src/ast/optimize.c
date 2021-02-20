#include "optimize.h"
#include "string.h"

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
  optimize_function_decl(main);
}

void optimize_block(ast_block_stmt *block) {
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    // switch 结构导向优化
    switch (stmt.type) {
      case AST_STMT_VAR_DECL: {
        optimize_var_decl((ast_var_decl_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        optimize_var_decl_assign((ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_FUNCTION_DECL: {
        ast_closure_decl *closure = optimize_function_decl((ast_function_decl *) stmt.stmt);
        ast_stmt closure_stmt;
        closure_stmt.type = AST_STMT_CLOSURE_DECL;
        closure_stmt.stmt = (void *) closure;
        block->list[i] = closure_stmt; // 值复制传递，不用担心指针空悬问题
        break;
      }
    }
  }
}

void optimize_var_decl(ast_var_decl_stmt *var_decl) {
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
  optimize_begin_scope();

  // 参数 0 为 env 预留
  formal_param *env = &function->formal_params[0];
  env->type = ENV; // env 类型
  env->ident = ENV;
  optimize_local_var *env_local = optimize_new_local(env->type, env->ident);

  // 改写
  env->ident = env_local->unique_ident;
  current_function->env_unique_name = env->ident;

  for (int i = 1; i < function->formal_param_count; ++i) {
    formal_param *param = &function->formal_params[i];
    // 注册并改写
    optimize_local_var *param_local = optimize_new_local(param->type, param->ident);
    param->ident = param_local->unique_ident;
  }

  // block
  optimize_block(&function->body);
  optimize_end_scope();

  ast_closure_decl *closure = malloc(sizeof(ast_closure_decl));
  closure->function = function;

  // TODO 添加自由变量捕捉

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
  // 错误处理
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

// 无法向上查找
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




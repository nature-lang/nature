#include "analysis.h"
#include "string.h"
#include "src/lib/error.h"
#include "src/lib/table.h"
#include "src/ast/symbol.h"

ast_closure_decl analysis(ast_block_stmt block_stmt) {

}

void analysis_block(ast_block_stmt *block) {
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    // switch 结构导向优化
    switch (stmt.type) {
      case AST_VAR_DECL: {
        analysis_var_decl((ast_var_decl *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        analysis_var_decl_assign((ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_ASSIGN: {
        analysis_assign((ast_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_FUNCTION_DECL: {
        // function + env => closure
        ast_closure_decl *closure = analysis_function_decl((ast_function_decl *) stmt.stmt);

        // 改写
        ast_stmt closure_stmt;
        closure_stmt.type = AST_CLOSURE_DECL;
        closure_stmt.stmt = closure;
        block->list[i] = closure_stmt; // 值复制传递，不用担心指针空悬问题
        break;
      }
      case AST_CALL: {
        analysis_call_function((ast_call *) stmt.stmt);
        break;
      }
      case AST_STMT_IF: {
        analysis_if((ast_if_stmt *) stmt.stmt);
        break;
      }
    }
  }
}

void analysis_if(ast_if_stmt *if_stmt) {
  analysis_expr(&if_stmt->condition);
  analysis_begin_scope();
  analysis_block(&if_stmt->consequent);
  analysis_end_scope();

  analysis_begin_scope();
  analysis_block(&if_stmt->alternate);
  analysis_end_scope();
}

void analysis_assign(ast_assign_stmt *assign) {
  analysis_expr(&assign->left);
  analysis_expr(&assign->right);
}

void analysis_call_function(ast_call *call) {
  // 函数地址改写
  analysis_expr(&call->target_expr);

  // 实参改写
  for (int i = 0; i < call->actual_param_count; ++i) {
    ast_expr *actual_param = &call->actual_params[i];
    analysis_expr(actual_param);
  }
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param var_decl
 */
void analysis_var_decl(ast_var_decl *var_decl) {
  // TODO 判断同当前块作用域内是否重复定义

  analysis_local_var *local = analysis_new_local(var_decl->type, var_decl->ident);

  // 改写
  var_decl->ident = local->unique_ident;
}

void analysis_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign) {
  analysis_local_var *local = analysis_new_local(var_decl_assign->type, var_decl_assign->ident);

  var_decl_assign->ident = local->unique_ident;

  analysis_expr(&var_decl_assign->expr);
}

/**
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 * @param function
 * @return
 */
ast_closure_decl *analysis_function_decl(ast_function_decl *function) {
  ast_closure_decl *closure = malloc(sizeof(ast_closure_decl));

  // 函数名称改写
  analysis_local_var *function_local = analysis_new_local(ast_new_var_type(), function->name);
  function->name = function_local->unique_ident;

  // add to function table
  table_set(symbol_var_table, function->name, function);

  // 开启一个新的 function 作用域(忘记干嘛用的了)
  analysis_function_begin();

  // 函数参数改写, 参数 0 预留给 env
  ast_var_decl *env = &function->formal_params[0];
  env->type = AST_COMPLEX_TYPE_ENV; // env 类型 是哪个啥类型？
  env->ident = unique_var_ident("env"); // TODO new env ident
  analysis_local_var *env_local = analysis_new_local(env->type, env->ident);
  env->ident = env_local->unique_ident;
  current_function->env_unique_name = env->ident;

  for (int i = 0; i < function->formal_param_count; ++i) {
    formal_param *param = &function->formal_params[i];
    // 注册并改写成唯一标识
    analysis_local_var *param_local = analysis_new_local(param->type, param->ident);
    param->ident = param_local->unique_ident;
  }

  // 编译 block, 其中进行了自由变量的捕获/改写和局部变量改写
  analysis_block(&function->body);


  // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
  // free 是在外部环境构建 env 的。
  for (int i = 0; i < current_function->free_count; ++i) {
    analysis_free_var free_var = current_function->frees[i];
    ast_expr expr = closure->env[i];
    // 逃逸变量就在当前环境中
    if (free_var.is_local_in_parent) {
      // ast_ident 表达式
      expr.type = AST_EXPR_TYPE_IDENT;
      ast_ident ident = current_function->parent->locals[free_var.index]->unique_ident;
      expr.expr = ident;
    } else {
      // ast_env_index 表达式
      expr.type = AST_EXPR_TYPE_ENV_INDEX;

      ast_access_env *access_env = malloc(sizeof(ast_access_env));
      access_env->env = current_function->parent->env_unique_name;
      access_env->index = free_var.index;
      // TODO type 该有吧？
      expr.expr = access_env;
    }
  }
  closure->function = function;

  analysis_function_end();
  return closure;
}

/**
 * 只要遇到块级作用域，就增加 scope
 */
void analysis_begin_scope() {
  current_function->scope_depth++;
}

void analysis_end_scope() {
  current_function->scope_depth--;
}

/**
 * type 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
analysis_local_var *analysis_new_local(ast_type type, string ident) {
  // unique ident
  string unique_ident = unique_var_ident(ident);

  analysis_local_var *local = malloc(sizeof(analysis_local_var));
  local->ident = ident;
  local->unique_ident = unique_ident;
  local->scope_depth = current_function->scope_depth;
  local->type = type;

  // 添加 locals
  current_function->locals[current_function->local_count++] = local;

  // 添加 var_table
  table_set(symbol_var_table, unique_ident, local);

  return local;
}

void analysis_expr(ast_expr *expr) {
  switch (expr->type) {
    case AST_EXPR_TYPE_BINARY: {
      analysis_binary((ast_binary_expr *) expr->expr);
      break;
    };
    case AST_EXPR_TYPE_LITERAL: {
      analysis_literal((ast_literal *) expr->expr);
      break;
    }
    case AST_EXPR_TYPE_IDENT: {
      analysis_ident(expr);
      break;
    };
    case AST_CALL: {
      analysis_call_function((ast_call *) expr->expr);
      break;
    }
    case AST_FUNCTION_DECL: {
      ast_closure_decl *closure = analysis_function_decl((ast_function_decl *) expr->expr);

      ast_expr closure_expr;
      closure_expr.type = AST_CLOSURE_DECL;
      closure_expr.expr = (void *) closure;
      // 重写 expr
      *expr = closure_expr;
      break;
    }
  }
}

/**
 * 使用变量
 * @param expr
 */
void analysis_ident(ast_expr *expr) {
  ast_ident *ident = (ast_ident *) expr->expr;

  // 在当前函数作用域中查找变量定义
  for (int i = 0; i < current_function->local_count; ++i) {
    analysis_local_var *local = current_function->locals[i];
    if (strcmp(*ident, local->ident) == 0) {
      // 在本地变量中找到,则进行简单改写 (从而可以在符号表中有唯一名称,方便定位)
      *ident = local->unique_ident;
      return;
    }
  }

  // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
  int8_t free_var_index = analysis_resolve_free(current_function, ident);
  if (free_var_index == -1) {
    error_exit(0, "not found ident");
  }

  // 外部作用域变量改写, 假如 foo 是外部便令，则 foo => env[free_var_index]
  expr->type = AST_EXPR_TYPE_ENV_INDEX;
  ast_access_env *env_index = malloc(sizeof(ast_access_env));
  env_index->env = current_function->env_unique_name;
  env_index->index = free_var_index;
  expr->expr = env_index;
}

/**
 * 返回 index
 * @param current
 * @param ident
 * @return
 */
int8_t analysis_resolve_free(analysis_function *current, ast_ident *ident) {
  if (current->parent == NULL) {
    return -1;
  }

  for (int i = 0; i < current->parent->local_count; ++i) {
    analysis_local_var *local = current->parent->locals[i];
    if (strcmp(*ident, local->ident) == 0) {
      current->parent->locals[i]->is_capture = true; // 被下级作用域引用

      return analysis_push_free(current, true, i);
    }
  }

  // 继续向上递归查询
  int8_t parent_free_index = analysis_resolve_free(current->parent, ident);
  if (parent_free_index != -1) {
    return analysis_push_free(current, false, parent_free_index);
  }

  return -1;
}




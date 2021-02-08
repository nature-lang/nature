#include "optimize.h"

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
        optimize_function_decl((ast_function_decl *) stmt.stmt);
        break;
      }
    }
  }
}

void optimize_var_decl(ast_var_decl_stmt *var_decl) {
  optimize_new_var(current_scope, var_decl->ident);
}

void optimize_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign) {
  optimize_new_var(current_scope, var_decl_assign->ident);
}

void optimize_function_decl(ast_function_decl *function) {
  optimize_begin_scope();
  // 形参
  for (int i = 0; i < function->formal_param_count; ++i) {
    formal_params param = function->formal_params[i];
    optimize_new_var(current_scope, param.ident);
  }

  // block
  optimize_block(&function->body);
  optimize_end_scope();
}

void optimize_begin_scope() {
  // 初始化 current_scope
  optimize_scope *scope = (optimize_scope *) malloc(sizeof(optimize_scope));
  scope->var_count = 0;
  scope->parent = current_scope;
  current_scope = scope;
}

void optimize_end_scope() {
  current_scope = current_scope->parent;
}

void optimize_new_var(optimize_scope *scope, string ident) {
  optimize_var var = {
      .ident = ident,
      .unique_ident = unique_var_ident(ident)
  };

  scope->vars[scope->var_count++] = var;
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
      optimize_ident((ast_ident *) expr->expr);
      break;
    };
  }
}

void optimize_ident(ast_ident *ident) {
  // 查找并改名

  // 找不到就报错
}


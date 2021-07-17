#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "analysis.h"
#include "src/lib/error.h"
#include "src/lib/table.h"
#include "src/symbol.h"

ast_closure_decl analysis(ast_block_stmt block_stmt) {
  // init
  unique_name_count = 0;

  // block 封装进 function,再封装到 closure 中
  ast_function_decl *function_decl = malloc(sizeof(ast_function_decl));
  function_decl->name = MAIN_FUNCTION_NAME;
  function_decl->body = block_stmt;
  function_decl->return_type = ast_new_type(TYPE_VOID);
  function_decl->formal_param_count = 0;

  ast_closure_decl *closure_decl = analysis_function_decl(function_decl);
  return *closure_decl;
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
      case AST_STMT_WHILE: {
        analysis_while((ast_while_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_FOR_IN: {
        analysis_for_in((ast_for_in_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_RETURN: {
        analysis_return((ast_return_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_TYPE_DECL: {
        analysis_type_decl((ast_type_decl_stmt *) stmt.stmt);
        break;
      }
      default:return;
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
  analysis_expr(&call->left);

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
  analysis_redeclared_check(var_decl->ident);

  analysis_type(&var_decl->type);

  analysis_local_ident *local = analysis_new_local(SYMBOL_TYPE_VAR, var_decl->type, var_decl->ident);

  // 改写
  var_decl->ident = local->unique_ident;
}

void analysis_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign) {
  analysis_redeclared_check(var_decl_assign->ident);

  analysis_type(&var_decl_assign->type);

  analysis_local_ident *local = analysis_new_local(SYMBOL_TYPE_VAR, var_decl_assign->type, var_decl_assign->ident);

  var_decl_assign->ident = local->unique_ident;

  analysis_expr(&var_decl_assign->expr);
}

/**
 * w
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 * @param function
 * @return
 */
ast_closure_decl *analysis_function_decl(ast_function_decl *function) {
  analysis_function_init();

  ast_closure_decl *closure = malloc(sizeof(ast_closure_decl));

  analysis_redeclared_check(function->name);
  analysis_type(&function->return_type);

  // 函数名称改写
  analysis_local_ident *local = analysis_new_local(
      SYMBOL_TYPE_FUNCTION,
      ast_new_type(TYPE_FUNCTION),
      function->name);
  function->name = local->unique_ident;

  // add to function table
  table_set(symbol_function_table, function->name, function);

  // 开启一个新的 function 作用域(忘记干嘛用的了)
  analysis_function_begin();

  // 函数形参处理
  for (int i = 0; i < function->formal_param_count; ++i) {
    ast_var_decl *param = function->formal_params[i];
    // 注册
    analysis_local_ident *param_local = analysis_new_local(SYMBOL_TYPE_VAR, param->type, param->ident);
    // 改写
    param->ident = param_local->unique_ident;

    analysis_type(&param->type);
  }

  // 分析请求体 block, 其中进行了自由变量的捕获/改写和局部变量改写
  analysis_block(&function->body);

  // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
  // free 是在外部环境构建 env 的。
//  current_function->env_unique_name = unique_var_ident(ENV_IDENT);
  closure->env_name = current_function->env_unique_name;

  // 构造 env
  for (int i = 0; i < current_function->free_count; ++i) {
    analysis_free_ident free_var = current_function->frees[i];
    ast_expr expr = closure->env[i];

    // 逃逸变量就在当前环境中
    if (free_var.is_local) {
      // ast_ident 表达式
      expr.type = AST_EXPR_IDENT;
      ast_ident ident = current_function->parent->locals[free_var.index]->unique_ident;
      expr.expr = ident;
    } else {
      // ast_env_index 表达式
      expr.type = AST_EXPR_ACCESS_ENV;
      ast_access_env *access_env = malloc(sizeof(ast_access_env));
      access_env->env = current_function->parent->env_unique_name;
      access_env->index = free_var.index;
      expr.expr = access_env;
    }
  }
  closure->env_count = current_function->free_count;
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
analysis_local_ident *analysis_new_local(symbol_type belong, ast_type type, string ident) {
  // unique ident
  string unique_ident = unique_var_ident(ident);

  analysis_local_ident *local = malloc(sizeof(analysis_local_ident));
  local->ident = ident;
  local->unique_ident = unique_ident;
  local->scope_depth = current_function->scope_depth;
  local->type = type;
  local->belong = belong;

  // 添加 locals
  current_function->locals[current_function->local_count++] = local;

  table_set(symbol_ident_table, unique_ident, local);

  return local;
}

void analysis_expr(ast_expr *expr) {
  switch (expr->type) {
    case AST_EXPR_BINARY: {
      analysis_binary((ast_binary_expr *) expr->expr);
      break;
    };
    case AST_EXPR_UNARY: {
      analysis_unary((ast_unary_expr *) expr->expr);
      break;
    };
    case AST_EXPR_NEW_STRUCT: {
      analysis_new_struct((ast_new_struct *) expr->expr);
      break;
    }
    case AST_EXPR_NEW_MAP: {
      analysis_new_map((ast_new_map *) expr->expr);
      break;
    }
    case AST_EXPR_NEW_LIST: {
      analysis_new_list((ast_new_list *) expr->expr);
      break;
    }
    case AST_EXPR_ACCESS: {
      analysis_access((ast_access *) expr->expr);
      break;
    };
    case AST_EXPR_SELECT_PROPERTY: {
      analysis_select_property((ast_select_property *) expr->expr);
      break;
    };
    case AST_EXPR_IDENT: {
      // 核心改写逻辑
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
      closure_expr.expr = closure;
      // 重写 expr
      *expr = closure_expr;
      break;
    }
    default: {
      error_exit(0, "unknown expr type");
    }
  }
}

/**
 * @param expr
 */
void analysis_ident(ast_expr *expr) {
  ast_ident *ident = (ast_ident *) expr->expr;

  // 在当前函数作用域中查找变量定义
  for (int i = 0; i < current_function->local_count; ++i) {
    analysis_local_ident *local = current_function->locals[i];
    if (strcmp(*ident, local->ident) == 0) {
      // 在本地变量中找到,则进行简单改写 (从而可以在符号表中有唯一名称,方便定位)
      *ident = local->unique_ident;
      return;
    }
  }

  // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
  int8_t free_var_index = analysis_resolve_free(current_function, *ident);
  if (free_var_index == -1) {
    error_exit(0, "not found ident");
  }

  // 外部作用域变量改写, 假如 foo 是外部便令，则 foo => env[free_var_index]
  expr->type = AST_EXPR_ACCESS_ENV;
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
int8_t analysis_resolve_free(analysis_function *current, string ident) {
  if (current->parent == NULL) {
    return -1;
  }

  for (int i = 0; i < current->parent->local_count; ++i) {
    analysis_local_ident *local = current->parent->locals[i];
    if (strcmp(ident, local->ident) == 0) {
      current->parent->locals[i]->is_capture = true; // 被下级作用域引用

      return (int8_t) analysis_push_free(current, true, (int8_t) i);
    }
  }

  // 继续向上递归查询
  int8_t parent_free_index = analysis_resolve_free(current->parent, ident);
  if (parent_free_index != -1) {
    return (int8_t) analysis_push_free(current, false, parent_free_index);
  }

  return -1;
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。直接定位唯一名称即可
 * @param type
 */
void analysis_type(ast_type *type) {
  // 如果只是简单的 ident,又应该如何改写呢？
  if (type->category == TYPE_DECL_IDENT) {
    // 向上查查查
    string unique_name = analysis_resolve_type(current_function, (string) type->value);
    type->value = unique_name;
    return;
  }

  if (type->category == TYPE_MAP) {
    ast_map_decl *map_decl = type->value;
    analysis_type(&map_decl->key_type);
    analysis_type(&map_decl->value_type);
    return;
  }

  if (type->category == TYPE_LIST) {
    ast_list_decl *map_decl = type->value;
    analysis_type(&map_decl->type);
    return;
  }

  if (type->category == TYPE_FUNCTION) {
    ast_function_type_decl *function_type_decl = type->value;
    analysis_type(&function_type_decl->return_type);
    for (int i = 0; i < function_type_decl->formal_param_count; ++i) {
      ast_var_decl *param = function_type_decl->formal_params[i];
      analysis_type(&param->type);
    }
  }

  if (type->category == TYPE_STRUCT) {
    ast_struct_decl *struct_decl = type->value;
    for (int i = 0; i < struct_decl->count; ++i) {
      ast_struct_property item = struct_decl->list[i];
      analysis_type(&item.type);
    }
  }

}

void analysis_access(ast_access *access) {
  analysis_expr(&access->left);
  analysis_expr(&access->key);
}

void analysis_select_property(ast_select_property *select) {
  analysis_expr(&select->left);
}

void analysis_binary(ast_binary_expr *expr) {
  analysis_expr(&expr->left);
  analysis_expr(&expr->right);
}

void analysis_unary(ast_unary_expr *expr) {
  analysis_expr(&expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
void analysis_new_struct(ast_new_struct *expr) {
  for (int i = 0; i < expr->count; ++i) {
    analysis_expr(&expr->list[i].value);
  }
}

void analysis_new_map(ast_new_map *expr) {
  for (int i = 0; i < expr->count; ++i) {
    analysis_expr(&expr->values[i].key);
    analysis_expr(&expr->values[i].value);
  }
}

void analysis_new_list(ast_new_list *expr) {
  for (int i = 0; i < expr->count; ++i) {
    analysis_expr(&expr->values[i]);
  }
}

void analysis_while(ast_while_stmt *stmt) {
  analysis_expr(&stmt->condition);

  analysis_begin_scope();
  analysis_block(&stmt->body);
  analysis_end_scope();
}

void analysis_for_in(ast_for_in_stmt *stmt) {
  analysis_expr(&stmt->iterate);

  analysis_begin_scope();
  analysis_var_decl(stmt->gen_key);
  analysis_var_decl(stmt->gen_value);
  analysis_block(&stmt->body);
  analysis_end_scope();
}

void analysis_return(ast_return_stmt *stmt) {
  analysis_expr(&stmt->expr);
}

// unique name
void analysis_type_decl(ast_type_decl_stmt *stmt) {
  analysis_redeclared_check(stmt->ident);
  analysis_type(&stmt->type);

  analysis_local_ident *local = analysis_new_local(SYMBOL_TYPE_CUSTOM_TYPE, ast_new_type(TYPE_NULL), stmt->ident);
  stmt->ident = local->unique_ident;

  table_set(symbol_custom_type_table, stmt->ident, stmt);
}
char *analysis_resolve_type(analysis_function *current, string ident) {
  for (int i = 0; i < current->local_count; ++i) {
    analysis_local_ident *local = current_function->locals[i];
    if (strcmp(ident, local->ident) == 0) {
      return local->unique_ident;
    }
  }

  if (current->parent == NULL) {
    error_exit(0, "type not found");
  }

  return analysis_resolve_type(current->parent, ident);
}

uint8_t analysis_push_free(analysis_function *current, bool is_local, int8_t index) {
  analysis_free_ident free = {
      .is_local = is_local,
      .index = index
  };
  uint8_t free_index = current->free_count++;
  current->frees[free_index] = free;

  return free_index;
}

bool analysis_redeclared_check(char *ident) {
  for (int i = 0; i < current_function->local_count; ++i) {
    analysis_local_ident *local = current_function->locals[i];
    if (strcmp(ident, local->ident) == 0) {
      error_exit(0, "redeclared ident");
      return false;
    }
  }
  return true;
}

/**
 * @param name
 * @return
 */
char *unique_var_ident(char *name) {
  char *unique_name = (char *) malloc(strlen(name) + 1 + sizeof(int));
  sprintf(unique_name, "%s_%d", name, unique_name_count++);
  return unique_name;
}

void analysis_function_begin() {
  analysis_begin_scope();
}

void analysis_function_end() {
  analysis_end_scope();
}

analysis_function *analysis_function_init() {
  analysis_function *new = malloc(sizeof(analysis_function));
  new->local_count = 0;
  new->free_count = 0;
  new->scope_depth = 0;
  new->env_unique_name = unique_var_ident(ENV_IDENT);

  // 继承关系
  new->parent = current_function;
  current_function = new;

  return current_function;
}





#include "infer.h"
#include "src/lib/error.h"
#include "src/symbol.h"
#include "analysis.h"

void infer(ast_closure_decl closure_decl) {

}

ast_type infer_closure_decl(ast_closure_decl *closure_decl) {
  infer_current_init(closure_decl);

  ast_function_decl *function_decl = closure_decl->function;
  ast_type result = analysis_function_to_type(function_decl);

  infer_block(&function_decl->body);

  infer_current = infer_current->parent;

  return result;
}

void infer_block(ast_block_stmt *block) {
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    // switch 结构导向优化
    switch (stmt.type) {
      case AST_VAR_DECL: {
        infer_var_decl((ast_var_decl *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        infer_var_decl_assign((ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_ASSIGN: {
        infer_assign((ast_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_CLOSURE_DECL: {
        infer_closure_decl((ast_closure_decl *) stmt.stmt);
        break;
      }
      case AST_CALL: {
        infer_call((ast_call *) stmt.stmt);
        break;
      }
      case AST_STMT_IF: {
        infer_if((ast_if_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_WHILE: {
        infer_while((ast_while_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_FOR_IN: {
        infer_for_in((ast_for_in_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_RETURN: {
        infer_return((ast_return_stmt *) stmt.stmt);
        break;
      }
//      case AST_STMT_TYPE_DECL: {
//        infer_type_decl((ast_type_decl_stmt *) stmt.stmt);
//        break;
//      }
      default:return;
    }
  }
}

/**
 * 表达式推断核心逻辑
 * @param expr
 * @return
 */
ast_type infer_expr(ast_expr *expr) {
  switch (expr->type) {
    case AST_EXPR_BINARY: {
      return infer_binary((ast_binary_expr *) expr->expr);
    }
    case AST_EXPR_UNARY: {
      return infer_unary((ast_unary_expr *) expr->expr);
    }
    case AST_EXPR_IDENT: {
      return infer_ident((ast_ident *) expr->expr);
    }
    case AST_EXPR_NEW_LIST: {
      return infer_new_list((ast_new_list *) expr->expr);
    }
    case AST_EXPR_NEW_MAP: {
      return infer_new_map((ast_new_map *) expr->expr);
    }
    case AST_EXPR_NEW_STRUCT: {
      return infer_new_struct((ast_new_struct *) expr->expr);
    }
    case AST_EXPR_ACCESS: {
      // 需要做类型改写，所以传递整个表达式
      return infer_access(expr);
    }
    case AST_EXPR_SELECT_PROPERTY: {
      return infer_select_property((ast_select_property *) expr->expr);
    }
    case AST_CALL: {
      return infer_call((ast_call *) expr->expr);
    }
    case AST_CLOSURE_DECL: {
      return infer_closure_decl((ast_closure_decl *) expr->expr);
    }
    default: {
      error_exit(0, "unknown expr");
    }
  }
}

/**
 *
 * @param expr
 * @return
 */
ast_type infer_binary(ast_binary_expr *expr) {
  // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
  ast_type left_type = infer_expr(&expr->left);
  ast_type right_type = infer_expr(&expr->right);

  if (left_type.category != right_type.category) {
    error_exit(0, "type exception");
  }

  switch (expr->operator) {
    case AST_EXPR_OPERATOR_ADD:
    case AST_EXPR_OPERATOR_SUB:
    case AST_EXPR_OPERATOR_MUL:
    case AST_EXPR_OPERATOR_DIV: {
      return left_type;
    }
    case AST_EXPR_OPERATOR_LT:
    case AST_EXPR_OPERATOR_LTE:
    case AST_EXPR_OPERATOR_GT:
    case AST_EXPR_OPERATOR_GTE:
    case AST_EXPR_OPERATOR_EQ_EQ:
    case AST_EXPR_OPERATOR_NOT_EQ: {
      return ast_new_simple_type(TYPE_BOOL);
    }
    default: {
      error_exit(0, "unexpect operator type");
    }
  }
}

/**
 * unary
 * @param expr
 * @return
 */
ast_type infer_unary(ast_unary_expr *expr) {
  ast_type operand_type = infer_expr(&expr->operand);
  if (expr->operator == AST_EXPR_OPERATOR_NOT && operand_type.category != TYPE_BOOL) {
    error_exit(0, "!expr, expr must be bool type");
  }

  if ((expr->operator == AST_EXPR_OPERATOR_MINUS) && operand_type.category != TYPE_INT
      && operand_type.category != TYPE_FLOAT) {
    error_exit(0, "!expr, expr must be int or float");
  }

  return operand_type;
}

/**
 * @param expr
 * @return
 */
ast_type infer_ident(ast_ident *expr) {
  string unique_ident = *expr;
  analysis_local_ident *local_ident = symbol_get_type(unique_ident);
  if (local_ident->belong != TYPE_VAR) {
    error_exit(0, "type error in infer ident");
  }

  ast_var_decl *var_decl = local_ident->decl;
  return var_decl->type;
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
ast_type infer_new_list(ast_new_list *new_list) {
  ast_type result = {
      .category = TYPE_LIST,
  };
  ast_list_decl *list_decl = malloc(sizeof(ast_list_decl));
  list_decl->type = ast_new_simple_type(TYPE_VAR);

  for (int i = 0; i < new_list->count; ++i) {
    ast_type item_type = infer_expr(&new_list->values[i]);
    // 初始化赋值
    if (list_decl->type.category == TYPE_VAR) {
      list_decl->type = item_type;
    } else {
      if (item_type.category != list_decl->type.category) {
        list_decl->type = ast_new_simple_type(TYPE_ANY);
        break;
      }
    }
  }

  result.value = list_decl;

  return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param new_map
 * @return
 */
ast_type infer_new_map(ast_new_map *new_map) {
  ast_type result = {
      .category = TYPE_MAP,
  };
  ast_map_decl *map_decl = malloc(sizeof(ast_map_decl));
  map_decl->key_type = ast_new_simple_type(TYPE_VAR);
  map_decl->value_type = ast_new_simple_type(TYPE_VAR);
  for (int i = 0; i < new_map->count; ++i) {
    ast_type key_type = infer_expr(&new_map->values[i].key);
    ast_type value_type = infer_expr(&new_map->values[i].value);

    // key
    if (map_decl->key_type.category == TYPE_VAR) {
      map_decl->key_type = key_type;
    } else {
      if (key_type.category != map_decl->key_type.category) {
        map_decl->key_type = ast_new_simple_type(TYPE_ANY);
        break;
      }
    }

    // value
    if (map_decl->value_type.category == TYPE_VAR) {
      map_decl->value_type = value_type;
    } else {
      if (value_type.category != map_decl->value_type.category) {
        map_decl->value_type = ast_new_simple_type(TYPE_ANY);
        break;
      }
    }
  }

  return result;
}

/**
 * person {
 *  age = 1
 * }
 *
 * struct {
 *   int age
 * } {
 *  age = 1
 * }
 * @param new_struct
 * @return
 */
ast_type infer_new_struct(ast_new_struct *new_struct) {
  ast_type result = new_struct->type;

  ast_struct_decl *struct_decl;
  if (result.category == TYPE_STRUCT) {
    struct_decl = (ast_struct_decl *) result.value;
  } else if (result.category == TYPE_DECL_IDENT) {
    string ident = result.value;
    struct_decl = symbol_struct(ident);
  } else {
    error_exit(0, "new struct type exception");
  }

  for (int i = 0; i < new_struct->count; ++i) {
    ast_struct_property struct_property = new_struct->list[i];

    ast_type expect_type = infer_struct_property_type(struct_decl, struct_property.key);
    ast_type actual_type = infer_expr(&struct_property.value);

    // expect type type 并不允许为 var
    if (!infer_compare_type(actual_type, expect_type)) {
      error_exit(0, "struct property type exception");
    }
  }

  return result;
}

/**
 * @param expr
 * @return
 */
ast_type infer_access(ast_expr *expr) {
  ast_type result;
  ast_access *access = expr->expr;
  ast_type left_type = infer_expr(&access->left);
  ast_type key_type = infer_expr(&access->key);

  if (left_type.category == TYPE_MAP) {
    ast_access_map *access_map = malloc(sizeof(ast_access_map));
    ast_map_decl *map_decl = left_type.value;

    // 参数改写
    access_map->left = access->left;
    access_map->key = access->key;

    // access_map 冗余字段处理
    access_map->key_type = map_decl->key_type;
    access_map->value_type = map_decl->value_type;
    expr->type = AST_EXPR_ACCESS_MAP;
    expr->expr = access_map;


    // 返回值
    result = map_decl->value_type;
  } else if (left_type.category == TYPE_LIST) {
    if (key_type.category != TYPE_INT) {
      error_exit(0, "access list, index type must by int");
    }

    ast_access_list *access_list = malloc(sizeof(ast_access_map));
    ast_list_decl *list_decl = left_type.value;

    // 参数改写
    access_list->left = access->left;
    access_list->index = access->key;
    access_list->type = list_decl->type;
    expr->type = AST_EXPR_ACCESS_LIST;
    expr->expr = access_list;

    result = list_decl->type;
  } else {
    error_exit(0, "type must map or list");
  };

  return result;
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * @param select_property
 * @return
 */
ast_type infer_select_property(ast_select_property *select_property) {
  ast_type result;
  ast_type left_type = infer_expr(&select_property->left);
  // left_type must be

  return result;
}

/**
 * @param call
 * @return
 */
ast_type infer_call(ast_call *call) {
  ast_type result;

  ast_type left_type = infer_expr(&call->left);

  if (left_type.category != TYPE_FUNCTION) {
    error_exit(0, "call.left type must be function");
  }

  ast_function_type_decl *function_type_decl = left_type.value;
  // call param check
  for (int i = 0; i < function_type_decl->formal_param_count; ++i) {
    ast_var_decl *formal_param = function_type_decl->formal_params[i];
    ast_type actual_param_type = infer_expr(&call->actual_params[i]);
    if (!infer_compare_type(formal_param->type, actual_param_type)) {
      error_exit(0, "function param type not match");
    }
  }

  result = function_type_decl->return_type;
  return result;
}

/**
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(ast_var_decl *var_decl) {
  ast_type type = var_decl->type;
  if (type.category == TYPE_VAR) {
    error_exit(0, "var decl must statement type");
  }
}

/**
 * 仅使用了 var 关键字的地方才需要进行类型推断，好像就这里需要！
 * var a = 1
 * var b = 2.0
 * var c = true
 * var d = void (int a, int b) {}
 * var e = [1, 2, 3] // ?
 * var f = {"a": 1, "b": 2} // ?
 * var h = call();
 */
void infer_var_decl_assign(ast_var_decl_assign_stmt *stmt) {
  ast_type expr_type = infer_expr(&stmt->expr);

  // 类型推断
  if (stmt->var_decl->type.category == TYPE_VAR && expr_type.category != TYPE_VAR) {
    stmt->var_decl->type = expr_type;

    analysis_local_ident *local_ident = table_get(symbol_ident_table, stmt->var_decl->ident);
    if (local_ident->belong != SYMBOL_TYPE_VAR) {
      error_exit(0, "ident should var type");
    }
    // 类型改写
    ast_var_decl *var_decl = local_ident->decl;
    var_decl->type = expr_type;
  }

  // 判断类型是否一致 compare
  if (infer_compare_type(stmt->var_decl->type, expr_type)) {
    error_exit(0, "type not match");
  }
}

/**
 * @param stmt
 */
void infer_assign(ast_assign_stmt *stmt) {
  ast_type left_type = infer_expr(&stmt->left);
  ast_type right_type = infer_expr(&stmt->left);

  if (!infer_compare_type(left_type, right_type)) {
    error_exit(0, "type not match");
  }
}

void infer_if(ast_if_stmt *stmt) {
  ast_type condition_type = infer_expr(&stmt->condition);
  if (condition_type.category != TYPE_BOOL) {
    error_exit(0, "if stmt condition must bool");
  }

  infer_block(&stmt->consequent);
  infer_block(&stmt->alternate);
}

void infer_while(ast_while_stmt *stmt) {
  ast_type condition_type = infer_expr(&stmt->condition);
  if (condition_type.category != TYPE_BOOL) {
    error_exit(0, "while stmt condition must bool");
  }
  infer_block(&stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
void infer_for_in(ast_for_in_stmt *stmt) {
  ast_type iterate_type = infer_expr(&stmt->iterate);
  if (iterate_type.category != TYPE_MAP && iterate_type.category != TYPE_LIST) {
    error_exit(0, "for in iterate type must be map or list");
  }

  // 类型推断
  analysis_local_ident *key_ident = table_get(symbol_ident_table, stmt->gen_key->ident);
  analysis_local_ident *value_ident = table_get(symbol_ident_table, stmt->gen_value->ident);
  ast_var_decl *key_decl = key_ident->decl;
  ast_var_decl *value_decl = key_ident->decl;
  if (iterate_type.category == TYPE_MAP) {
    ast_map_decl *map_decl = iterate_type.value;
    key_decl->type = map_decl->key_type;
    value_decl->type = map_decl->value_type;
  } else {
    ast_list_decl *list_decl = iterate_type.value;
    key_decl->type = ast_new_simple_type(TYPE_INT);
    value_decl->type = list_decl->type;

  }

  infer_block(&stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure 里面？
 * @param stmt
 */
void infer_return(ast_return_stmt *stmt) {
  ast_type return_type = infer_expr(&stmt->expr);

  ast_type expect_type = infer_current->closure_decl->function->return_type;
  if (infer_compare_type(expect_type, return_type)) {
    error_exit(0, "type not match");
  }
}

infer_closure *infer_current_init(ast_closure_decl *closure_decl) {
  infer_closure *new = malloc(sizeof(infer_closure));
  new->closure_decl = closure_decl;
  new->parent = infer_current;

  infer_current = new;
  return infer_current;
}




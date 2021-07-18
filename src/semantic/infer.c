#include "infer.h"
#include "src/lib/error.h"
#include "src/symbol.h"

void infer(ast_closure_decl closure_decl) {

}

ast_type infer_closure_decl(ast_closure_decl *closure_decl) {

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
      case AST_FUNCTION_DECL: {
        // function + env => closure
        infer_function_decl((ast_function_decl *) stmt.stmt);
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
      case AST_STMT_TYPE_DECL: {
        infer_type_decl((ast_type_decl_stmt *) stmt.stmt);
        break;
      }
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
      error_exit(0, "unexpect expr");
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
 * TODO 去符号表查询这个变量的类型吧？
 * @param expr
 * @return
 */
ast_type infer_ident(ast_ident *expr) {
  ast_type result;
  string ident = *expr;

  return result;
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

    ast_type expect_type = infer_struct_property_type(struct_property.key);
    ast_type actual_type = infer_expr(&struct_property.value);

    // expect type type 并不允许为 var
    if (actual_type.category != expect_type.category) {
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
 * to_string() // 如果只是这样简单的函数调用呢？符号表会注册该函数的所有信息
 *
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
  result = function_type_decl->return_type;
  return result;
}




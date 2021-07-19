#include "infer.h"
#include "src/lib/error.h"
#include "src/symbol.h"
#include "analysis.h"
#include "string.h"

void infer(ast_closure_decl closure_decl) {

}

ast_type infer_closure_decl(ast_closure_decl *closure_decl) {
  ast_function_decl *function_decl = closure_decl->function;

  // 类型还原
  function_decl->return_type = infer_type(function_decl->return_type);
  for (int i = 0; i < function_decl->formal_param_count; ++i) {
    function_decl->formal_params[i]->type = infer_type(function_decl->formal_params[i]->type);
  }

  infer_current_init(closure_decl);
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
      exit_error(0, "unknown expr");
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
    exit_error(0, "type exception");
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
      exit_error(0, "unexpect operator type");
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
    exit_error(0, "!expr, expr must be bool type");
  }

  if ((expr->operator == AST_EXPR_OPERATOR_MINUS) && operand_type.category != TYPE_INT
      && operand_type.category != TYPE_FLOAT) {
    exit_error(0, "!expr, expr must be int or float");
  }

  return operand_type;
}

/**
 * func main() {
 *  a = 1
 *  fmt.Println(a)
 *}
 * var a int
 *
 * 参考 golang，声明是可能在使用之后的
 * @param expr
 * @return
 */
ast_type infer_ident(ast_ident *expr) {
  string unique_ident = *expr;
  analysis_local_ident *local_ident = symbol_get_type(unique_ident);
  if (local_ident->belong != TYPE_VAR) {
    exit_error(0, "type error in infer ident");
  }

  // 类型还原，并回写到 local_ident
  ast_var_decl *var_decl = local_ident->decl;
  var_decl->type = infer_type(var_decl->type);

  return var_decl->type;
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
ast_type infer_new_list(ast_new_list *new_list) {
  ast_type result = {
      .is_origin = false,
      .category = TYPE_LIST,
  };
  ast_list_decl *list_decl = malloc(sizeof(ast_list_decl));
  list_decl->type = ast_new_simple_type(TYPE_VAR);

  for (int i = 0; i < new_list->count; ++i) {
    ast_type item_type = infer_expr(&new_list->values[i]);
    if (list_decl->type.category == TYPE_VAR) {
      // 初始化赋值
      list_decl->type = item_type;
    } else {
      if (!infer_compare_type(item_type, list_decl->type)) {
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
      .is_origin = false,
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
      if (!infer_compare_type(key_type, map_decl->key_type)) {
        map_decl->key_type = ast_new_simple_type(TYPE_ANY);
        break;
      }
    }

    // value
    if (map_decl->value_type.category == TYPE_VAR) {
      map_decl->value_type = value_type;
    } else {
      if (!infer_compare_type(value_type, map_decl->value_type)) {
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
  // 类型还原, struct ident 一定会被还原回 struct 原始结构
  // 如果本身已经是 struct 结构，那么期中的 struct property type 也会被还原成原始类型
  new_struct->type = infer_type(new_struct->type);

  if (new_struct->type.category != TYPE_STRUCT) {
    exit_error(0, "new struct type exception");
  }

  ast_struct_decl *struct_decl = (ast_struct_decl *) new_struct->type.value;
  for (int i = 0; i < new_struct->count; ++i) {
    ast_struct_property struct_property = new_struct->list[i];
    // struct_decl 已经是被还原过的类型了
    ast_type expect_type = infer_struct_property_type(struct_decl, struct_property.key);
    ast_type actual_type = infer_expr(&struct_property.value);

    // expect type type 并不允许为 var
    if (!infer_compare_type(actual_type, expect_type)) {
      exit_error(0, "struct property type exception");
    }
  }

  return new_struct->type;
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
      exit_error(0, "access list, index type must by int");
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
    exit_error(0, "type must map or list");
    return result;
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
  ast_type left_type = infer_expr(&select_property->left);

  if (left_type.category != TYPE_STRUCT) {
    exit_error(0, "struct type exception");
  }
  ast_struct_decl *struct_decl = left_type.value;
  for (int i = 0; i < struct_decl->count; ++i) {
    if (strcmp(struct_decl->list[i].key, select_property->property) == 0) {
      return struct_decl->list[i].type;
    }
  }

  exit_error(0, "cannot get property");
}

/**
 * @param call
 * @return
 */
ast_type infer_call(ast_call *call) {
  ast_type result;

  ast_type left_type = infer_expr(&call->left);

  if (left_type.category != TYPE_FUNCTION) {
    exit_error(0, "call.left type must be function");
  }

  ast_function_type_decl *function_type_decl = left_type.value;

  // call param check
  for (int i = 0; i < function_type_decl->formal_param_count; ++i) {
    ast_var_decl *formal_param = function_type_decl->formal_params[i];

    ast_type actual_param_type = infer_expr(&call->actual_params[i]);
    if (!infer_compare_type(formal_param->type, actual_param_type)) {
      exit_error(0, "function param type not match");
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
    exit_error(0, "var decl must statement type");
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
  }

  // 类型还原
  stmt->var_decl->type = infer_type(stmt->var_decl->type);

  // 判断类型是否一致 compare
  if (infer_compare_type(stmt->var_decl->type, expr_type)) {
    exit_error(0, "type not match");
  }
}

/**
 * @param stmt
 */
void infer_assign(ast_assign_stmt *stmt) {
  ast_type left_type = infer_expr(&stmt->left);
  ast_type right_type = infer_expr(&stmt->left);

  if (!infer_compare_type(left_type, right_type)) {
    exit_error(0, "type not match");
  }
}

void infer_if(ast_if_stmt *stmt) {
  ast_type condition_type = infer_expr(&stmt->condition);
  if (condition_type.category != TYPE_BOOL) {
    exit_error(0, "if stmt condition must bool");
  }

  infer_block(&stmt->consequent);
  infer_block(&stmt->alternate);
}

void infer_while(ast_while_stmt *stmt) {
  ast_type condition_type = infer_expr(&stmt->condition);
  if (condition_type.category != TYPE_BOOL) {
    exit_error(0, "while stmt condition must bool");
  }
  infer_block(&stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
void infer_for_in(ast_for_in_stmt *stmt) {
  // 经过 infer_expr 的类型一定是已经被还原过的
  ast_type iterate_type = infer_expr(&stmt->iterate);
  if (iterate_type.category != TYPE_MAP && iterate_type.category != TYPE_LIST) {
    exit_error(0, "for in iterate type must be map or list");
  }

  // 类型推断
  ast_var_decl *key_decl = stmt->gen_key;
  ast_var_decl *value_decl = stmt->gen_value;
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
    exit_error(0, "type not match");
  }
}

infer_closure *infer_current_init(ast_closure_decl *closure_decl) {
  infer_closure *new = malloc(sizeof(infer_closure));
  new->closure_decl = closure_decl;
  new->parent = infer_current;

  infer_current = new;
  return infer_current;
}

/**
 * 比较前都已经还原为原始类型了
 * @param left
 * @param right
 * @return
 */
bool infer_compare_type(ast_type left, ast_type right) {
  if (!left.is_origin || !right.is_origin) {
    exit_error(0, "type not origin");
    return false;
  }

  if (left.category == TYPE_VAR && right.category == TYPE_VAR) {
    exit_error(0, "type cannot infer");
    return false;
  }

  if (left.category != right.category) {
    return false;
  }

  if (left.category == TYPE_MAP) {
    ast_map_decl *left_map_decl = left.value;
    ast_map_decl *right_map_decl = right.value;

    if (!infer_compare_type(left_map_decl->key_type, right_map_decl->value_type)) {
      return false;
    }

    if (!infer_compare_type(left_map_decl->value_type, right_map_decl->value_type)) {
      return false;
    }
  }

  if (left.category == TYPE_LIST) {
    ast_list_decl *left_list_decl = left.value;
    ast_list_decl *right_list_decl = right.value;
    if (!infer_compare_type(left_list_decl->type, right_list_decl->type)) {
      return false;
    }
  }

  if (left.category == TYPE_FUNCTION) {
    ast_function_type_decl *left_function = left.value;
    ast_function_type_decl *right_function = right.value;
    if (!infer_compare_type(left_function->return_type, right_function->return_type)) {
      return false;
    }

    if (left_function->formal_param_count != right_function->formal_param_count) {
      return false;
    }

    for (int i = 0; i < left_function->formal_param_count; ++i) {
      if (!infer_compare_type(
          left_function->formal_params[i]->type,
          right_function->formal_params[i]->type
      )) {
        return false;
      }
    }
  }

  if (left.category == TYPE_STRUCT) {
    ast_struct_decl *left_struct_decl = left.value;
    ast_struct_decl *right_struct_decl = right.value;
    if (left_struct_decl->count != right_struct_decl->count) {
      return false;
    }

    for (int i = 0; i < left_struct_decl->count; ++i) {
      // key 比较
      if (strcmp(
          left_struct_decl->list[i].key,
          right_struct_decl->list[i].key) != 0) {
        return false;
      }

      // type 比较
      if (!infer_compare_type(
          left_struct_decl->list[i].type,
          right_struct_decl->list[i].type
      )) {
        return false;
      }
    }
  }

  return true;
}


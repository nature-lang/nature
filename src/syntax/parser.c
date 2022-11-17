#include "parser.h"
#include "utils/error.h"
#include "utils/slice.h"
#include <stdio.h>
#include "src/debug/debug.h"
#include <string.h>

//parser_cursor_t path->p_cursor;

ast_expr_operator token_to_ast_expr_operator[] = {
        [TOKEN_PLUS] = AST_EXPR_OPERATOR_ADD,
        [TOKEN_MINUS] = AST_EXPR_OPERATOR_SUB,
        [TOKEN_STAR] = AST_EXPR_OPERATOR_MUL,
        [TOKEN_SLASH] = AST_EXPR_OPERATOR_DIV,
        [TOKEN_EQUAL_EQUAL] = AST_EXPR_OPERATOR_EQ_EQ,
        [TOKEN_NOT_EQUAL] = AST_EXPR_OPERATOR_NOT_EQ,
        [TOKEN_GREATER_EQUAL] = AST_EXPR_OPERATOR_GTE,
        [TOKEN_RIGHT_ANGLE] = AST_EXPR_OPERATOR_GT,
        [TOKEN_LESS_EQUAL] = AST_EXPR_OPERATOR_LTE,
        [TOKEN_LEFT_ANGLE] = AST_EXPR_OPERATOR_LT,
};

type_base_t token_to_type_category[] = {
        // literal
        [TOKEN_TRUE] = TYPE_BOOL,
        [TOKEN_FALSE] = TYPE_BOOL,
        [TOKEN_LITERAL_FLOAT] = TYPE_FLOAT,
        [TOKEN_LITERAL_INT] = TYPE_INT,
        [TOKEN_LITERAL_STRING] = TYPE_STRING,

        // code
        [TOKEN_BOOL] = TYPE_BOOL,
        [TOKEN_FLOAT] = TYPE_FLOAT,
        [TOKEN_INT] = TYPE_INT,
        [TOKEN_STRING] = TYPE_STRING,
        [TOKEN_VOID] = TYPE_VOID,
        [TOKEN_NULL] = TYPE_NULL,
        [TOKEN_VAR] = TYPE_UNKNOWN,
        [TOKEN_ANY] = TYPE_ANY
};

parser_rule rules[] = {
        // infix: test()、void test() {}, void () {}
        [TOKEN_LEFT_PAREN] = {parser_grouping, parser_call_expr, PRECEDENCE_CALL},
        // map["foo"] list[0]
        [TOKEN_LEFT_SQUARE] = {parser_new_list, parser_access, PRECEDENCE_CALL},
        [TOKEN_LEFT_CURLY] = {parser_new_map, NULL, PRECEDENCE_NULL},
        [TOKEN_DOT] = {NULL, parser_select_property, PRECEDENCE_CALL},
        [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
        [TOKEN_PLUS] = {NULL, parser_binary, PRECEDENCE_TERM},
        [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_STAR] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_OR_OR] = {NULL, parser_binary, PRECEDENCE_OR},
        [TOKEN_AND_AND] = {NULL, parser_binary, PRECEDENCE_AND},
        [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
        [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_EQUALITY},
        [TOKEN_RIGHT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_GREATER_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LESS_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LITERAL_STRING] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_INT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_FLOAT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_TRUE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_FALSE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_IDENT] = {parser_ident_expr, NULL, PRECEDENCE_NULL},

        // 表达式的前缀是一个类型
        [TOKEN_VOID] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_ANY] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_INT] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_FLOAT] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_BOOL] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_STRING] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_FUNCTION] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_MAP] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_ARRAY] = {parser_direct_type_expr, NULL, PRECEDENCE_NULL},
        // code decl 不会出现在表达式这里
        [TOKEN_STRUCT] = {parser_struct_type_expr, NULL, PRECEDENCE_NULL},

        [TOKEN_EOF] = {NULL, NULL, PRECEDENCE_NULL},
};

slice_t *parser(module_t *m, list *token_list) {
    parser_cursor_init(m, token_list);

    slice_t *block_stmt = slice_new();

    ast_stmt_expr_type stmt_type = -1;

    while (!parser_is(m, TOKEN_EOF)) {

#ifdef DEBUG_PARSER
        if (stmt_type != -1) {
            debug_parser_stmt(stmt_type);
        }
#endif

        ast_stmt *stmt = parser_stmt(m);

        slice_push(block_stmt, stmt);
        parser_must_stmt_end(m);
    }

    return block_stmt;
}

slice_t *parser_block(module_t *m) {
    slice_t *stmt_list = slice_new();

    parser_must(m, TOKEN_LEFT_CURLY); // 必须是
    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_stmt *stmt = parser_stmt(m);
        parser_must_stmt_end(m);

        slice_push(stmt_list, stmt);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);

    return stmt_list;
}

ast_stmt *parser_stmt(module_t *m) {
    if (parser_is_direct_type(m) || parser_is_custom_type_var(m)) {
        return parser_var_or_fn_decl(m);
    } else if (parser_is(m, TOKEN_LITERAL_IDENT)) {
        return parser_ident_stmt(m);
    } else if (parser_is(m, TOKEN_IF)) {
        return parser_if_stmt(m);
    } else if (parser_is(m, TOKEN_FOR)) {
        return parser_for_stmt(m);
    } else if (parser_is(m, TOKEN_WHILE)) {
        return parser_while_stmt(m);
    } else if (parser_is(m, TOKEN_RETURN)) {
        return parser_return_stmt(m);
    } else if (parser_is(m, TOKEN_IMPORT)) {
        return parser_import_stmt(m);
    } else if (parser_is(m, TOKEN_TYPE)) {
        return parser_type_decl_stmt(m);
    }

    error_printf(parser_line(m), "cannot parser stmt");
    return parser_new_stmt();
}

/**
 * code foo = int
 * @return
 */
ast_stmt *parser_type_decl_stmt(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    ast_type_decl_stmt *type_decl_stmt = malloc(sizeof(ast_type_decl_stmt));
    parser_must(m, TOKEN_TYPE); // code
    type_decl_stmt->ident = parser_advance(m)->literal; // ident
    parser_must(m, TOKEN_EQUAL); // =
    // 类型解析
    type_decl_stmt->type = parser_type(m); // int

    result->assert_type = AST_STMT_TYPE_DECL;
    result->value = type_decl_stmt;

    return result;
}

/**
 * var foo = expr
 * @return
 */
ast_stmt *parser_auto_infer_decl(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));

    parser_must(m, TOKEN_VAR);
    // 变量名称
    token *t = parser_must(m, TOKEN_LITERAL_IDENT);
    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = parser_type(m);
    var_decl->ident = t->literal;
    stmt->expr = parser_expr(m);
    stmt->var_decl = var_decl;

    result->assert_type = AST_STMT_VAR_DECL_ASSIGN;
    result->value = stmt;

    return result;
}

ast_expr parser_precedence_expr(module_t *m, parser_precedence precedence) {
// 读取表达式前缀
    parser_prefix_fn prefix_fn = parser_get_rule(parser_peek(m)->type)->prefix;
    if (prefix_fn == NULL) {
        error_printf(parser_line(m), "cannot parser ident: %s", parser_peek(m)->literal);
    }

    ast_expr expr = prefix_fn(m); // advance

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则处理
    token_type type = parser_peek(m)->type;
    parser_rule *infix_rule = parser_get_rule(type);
    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;
        expr = infix_fn(m, expr);

        infix_rule = parser_get_rule(parser_peek(m)->type);
    }

    return expr;
}

/**
 * 表达式优先级处理方式
 * @return
 */
ast_expr parser_expr(module_t *m) {
    return parser_precedence_expr(m, PRECEDENCE_ASSIGN);
}

/**
 * int foo = 12;
 * int foo;
 * int foo() {};
 * void foo() {};
 * int () {};
 * @return
 */
ast_stmt *parser_var_or_fn_decl(module_t *m) {
    ast_stmt *result = parser_new_stmt();

    type_t type = parser_type(m);

    // 声明时函数名称仅占用一个 token
    if (parser_is(m, TOKEN_LEFT_PAREN) || parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
        result->assert_type = AST_NEW_FN;
        result->value = parser_fn_decl(m, type);
        return result;
    }

    token *ident_token = parser_advance(m);

    // int foo = 12;
    if (parser_consume(m, TOKEN_EQUAL)) {
        ast_var_decl_assign_stmt *stmt = malloc(sizeof(ast_var_decl_assign_stmt));
        ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
        var_decl->type = type;
        var_decl->ident = ident_token->literal;
        stmt->expr = parser_expr(m);
        stmt->var_decl = var_decl;
        result->assert_type = AST_STMT_VAR_DECL_ASSIGN;
        result->value = stmt;
        return result;
    }

    // int foo;
    ast_var_decl *stmt = malloc(sizeof(ast_var_decl));
    stmt->type = type;
    stmt->ident = ident_token->literal;
    result->assert_type = AST_VAR_DECL;
    result->value = stmt;

    return result;
}

ast_expr parser_binary(module_t *m, ast_expr left) {
    ast_expr result = parser_new_expr(m);

    token *operator_token = parser_advance(m);

    // right expr
    parser_precedence precedence = parser_get_rule(operator_token->type)->infix_precedence;
    ast_expr right = parser_precedence_expr(m, precedence + 1);

    ast_binary_expr *binary_expr = malloc(sizeof(ast_binary_expr));

    binary_expr->operator = token_to_ast_expr_operator[operator_token->type];
    binary_expr->left = left;
    binary_expr->right = right;

    result.assert_type = AST_EXPR_BINARY;
    result.value = binary_expr;

//  printf("code: %s\n", operator_token->literal);

    return result;
}

/**
 * ! 取反
 * - 取负数
 * @return
 */
ast_expr parser_unary(module_t *m) {
    ast_expr result = parser_new_expr(m);
    token *operator_token = parser_advance(m);
    ast_expr operand = parser_precedence_expr(m, PRECEDENCE_UNARY);

    ast_unary_expr *unary_expr = malloc(sizeof(ast_unary_expr));

    if (operator_token->type == TOKEN_NOT) {
        unary_expr->operator = AST_EXPR_OPERATOR_NOT;
    } else if (operator_token->type == TOKEN_MINUS) {
        unary_expr->operator = AST_EXPR_OPERATOR_NEG;
    } else {
        error_exit("unknown operator code");
    }

    unary_expr->operand = operand;

    result.assert_type = AST_EXPR_UNARY;
    result.value = unary_expr;

    return result;
}

/**
 * (1 + 1)
 * (int a, int b) {}
 */
ast_expr parser_grouping(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_expr expr = parser_expr(m);
    parser_must(m, TOKEN_RIGHT_PAREN);
    return expr;
}

ast_expr parser_literal(module_t *m) {
    ast_expr result = parser_new_expr(m);
    token *literal_token = parser_advance(m);
    ast_literal *literal_expr = malloc(sizeof(ast_literal));
    literal_expr->type = token_to_type_category[literal_token->type];
    literal_expr->value = literal_token->literal; // 具体数值

    result.assert_type = AST_EXPR_LITERAL;
    result.value = literal_expr;

    return result;
}

/**
 * 右值是 ident 开头的处理
 * ident is custom code or variable
 * ident 如果是 code 则需要特殊处理
 * @return
 */
ast_expr parser_ident_expr(module_t *m) {
    ast_expr result = parser_new_expr(m);
    token *ident_token = parser_advance(m);

    // type ident 通常表示自定义类型，如 code foo = int, 其就是 foo 类型。
    // 所以应该使用 type 保存这种类型。这种类型保存在 type 中，其 category 应该是什么呢？
    // 在没有进行类型还原之前，可以使用 type_decl_ident 保存，具体的字符名称则保存在 .value 中即可
    type_t type_decl_ident = {
            .is_origin = false,
            .base = TYPE_DECL_IDENT,
            .value = ast_new_ident(ident_token->literal)
    };

    /**
      * 请注意这里是实例化一个结构体,而不是声明一个结构体
      * 声明 code person = struct{int a, int b}
      * 实例化
      * var a =  person {
      *              a = 1
      *              b = 2
      * }
      * var a= struct {int a, int b} {b = 1, b = 2}
      **/
    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        return parser_new_struct(m, type_decl_ident);
    }

    // foo_type a() {}
    if (parser_is(m, TOKEN_LITERAL_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
        return parser_fn_decl_expr(m, type_decl_ident);
    };
    // code () {}
    if (parser_is(m, TOKEN_LEFT_PAREN) && parser_is_fn_decl(m, parser_next(m, 0))) {
        return parser_fn_decl_expr(m, type_decl_ident);
    }

    // call() or other ident prefix expr
    result.assert_type = AST_EXPR_IDENT;
    result.value = ast_new_ident(ident_token->literal);

    return result;
}

/**
 * 暂时无法确定 foo 的类型是 list 还是 map
 * foo[expr]
 * @param left
 * @return
 */
ast_expr parser_access(module_t *m, ast_expr left) {
    ast_expr result = parser_new_expr(m);

    parser_must(m, TOKEN_LEFT_SQUARE);
    ast_expr key = parser_precedence_expr(m, PRECEDENCE_CALL);
    parser_must(m, TOKEN_RIGHT_SQUARE);
    ast_access *access_expr = malloc(sizeof(ast_access));
    access_expr->left = left;
    access_expr->key = key;
    result.assert_type = AST_EXPR_ACCESS;
    result.value = access_expr;

    return result;
}

/**
 * foo.bar[car]
 * for.bar.car
 * @param left
 * @return
 */
ast_expr parser_select_property(module_t *m, ast_expr left) {
    ast_expr result = parser_new_expr(m);
    parser_must(m, TOKEN_DOT);

    token *property_token = parser_must(m, TOKEN_LITERAL_IDENT);
    ast_select_property *select_property_expr = malloc(sizeof(ast_select_property));
    select_property_expr->left = left;
    select_property_expr->property = property_token->literal;

    result.assert_type = AST_EXPR_SELECT_PROPERTY;
    result.value = select_property_expr;

    return result;
}

ast_expr parser_call_expr(module_t *m, ast_expr left_expr) {
    ast_expr result = parser_new_expr(m);

    ast_call *call_stmt = malloc(sizeof(ast_call));
    call_stmt->left = left_expr;

    // param handle
    parser_actual_param(m, call_stmt);

    result.assert_type = AST_CALL;
    result.value = call_stmt;
    return result;
}

//ast_stmt *parser_call_stmt(module_t *m){
//  ast_stmt *result;
//  // left_expr
//  ast_expr name_expr = parser_expr(PRECEDENCE_NULL);
//
//  ast_call *call_stmt = malloc(sizeof(ast_call));
//  call_stmt->left = name_expr;
//
//  // param handle
//  parser_actual_param(call_stmt);
//
//  result->code = AST_CALL;
//  result->stmt = call_stmt;
//
//  return result;
//}

void parser_actual_param(module_t *m, ast_call *call) {
    parser_must(m, TOKEN_LEFT_PAREN);

    if (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        do {
            if (parser_consume(m, TOKEN_ELLIPSIS)) {
                call->spread_param = true;
            }
            // 参数解析 call(1 + 1, param_a)
            call->actual_params[call->actual_param_count++] = parser_expr(m);
            if (call->spread_param) {
                assertf(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
                break;
            }
        } while (parser_consume(m, TOKEN_COMMA));
    }

    parser_must(m, TOKEN_RIGHT_PAREN);
}

ast_var_decl *parser_var_decl(module_t *m) {
    type_t var_type = parser_type(m);
    token *var_ident = parser_advance(m);
    if (var_ident->type != TOKEN_LITERAL_IDENT) {
        error_printf(parser_line(m), "parser variable definitions error, '%s' not a ident", var_ident->literal);
    }
    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = var_type;
    var_decl->ident = var_ident->literal;
    return var_decl;
}

void parser_type_function_formal_param(module_t *m, type_fn_t *type_fn) {
    parser_must(m, TOKEN_LEFT_PAREN);

    if (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
        var_decl->ident = "";
        var_decl->type = parser_type(m);
        // formal parameter handle code + ident
        type_fn->formal_param_types[0] = parser_type(m);
        type_fn->formal_param_count = 1;

        while (parser_consume(m, TOKEN_COMMA)) {
            uint8_t count = type_fn->formal_param_count++;
            var_decl = malloc(sizeof(ast_var_decl));
            type_fn->formal_param_types[count] = parser_type(m);
        }
    }
    parser_must(m, TOKEN_RIGHT_PAREN);
}

void parser_formal_param(module_t *m, ast_new_fn *fn_decl) {
    parser_must(m, TOKEN_LEFT_PAREN);
    if (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        do {
            if (parser_consume(m, TOKEN_ELLIPSIS)) {
                fn_decl->rest_param = true;
            }

            uint8_t count = fn_decl->formal_param_count++;
            fn_decl->formal_params[count] = parser_var_decl(m);

            if (fn_decl->rest_param) {
                assertf(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
                break;
            }
        } while (parser_consume(m, TOKEN_COMMA));
    }

    parser_must(m, TOKEN_RIGHT_PAREN);
}

/**
 * 兼容 var
 * @return
 */
type_t parser_type(module_t *m) {
    type_t result = {
            .is_origin = false
    };

    // int/float/bool/string/void/var/any
    if (parser_is_simple_type(m)) {
        token *type_token = parser_advance(m);
        result.base = token_to_type_category[type_token->type];
        result.value = type_token->literal;
        return result;
    }

    if (parser_consume(m, TOKEN_LEFT_SQUARE)) {
        ast_array_decl *type_array_decl = malloc(sizeof(ast_array_decl));
//        parser_must(m, TOKEN_LEFT_SQUARE);

        type_array_decl->type = parser_type(m);

        if (parser_consume(m, TOKEN_COMMA)) {
            token *token = parser_advance(m);
            assertf(token->type == TOKEN_LITERAL_INT, "array count literal parser err, not int token, actual %d",
                    token->type);

            int count = atoi(token->literal);
            type_array_decl->count = count;
        }

        parser_must(m, TOKEN_RIGHT_SQUARE);

        result.base = TYPE_ARRAY;
        result.value = type_array_decl;
        return result;
    }

    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        ast_map_decl *type_map_decl = malloc(sizeof(ast_map_decl));
//        parser_must(m, TOKEN_LEFT_CURLY);
        type_map_decl->key_type = parser_type(m);
        parser_must(m, TOKEN_COLON);
        type_map_decl->value_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_CURLY);

        result.base = TYPE_MAP;
        result.value = type_map_decl;
        return result;
    }

    if (parser_consume(m, TOKEN_STRUCT)) {
        ast_struct_decl *type_struct_decl = malloc(sizeof(ast_struct_decl));
        type_struct_decl->count = 0;
        parser_must(m, TOKEN_LEFT_CURLY);
        while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
            // default value
            ast_struct_property item;
            item.type = parser_type(m);
            item.key = parser_advance(m)->literal;

            type_struct_decl->list[type_struct_decl->count++] = item;
            parser_must_stmt_end(m);
        }

        parser_must(m, TOKEN_RIGHT_CURLY);

        result.base = TYPE_STRUCT;
        result.value = type_struct_decl;
        return result;
    }

    if (parser_consume(m, TOKEN_FUNCTION)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_fn_t *type_function = NEW(type_fn_t);
        type_function->return_type = parser_type(m);
        parser_type_function_formal_param(m, type_function);
        parser_must(m, TOKEN_RIGHT_ANGLE);

        result.base = TYPE_FN;
        result.value = type_function;
        return result;
    }

    assertf(parser_is(m, TOKEN_LITERAL_IDENT), "line: %d, parser error,code must base/custom code ident",
            parser_line(m));


    // 神奇的 ident
    token *type_token = parser_advance(m);
    result.base = TYPE_DECL_IDENT;
    result.value = ast_new_ident(type_token->literal);
    return result;
}

/**
 * var a = int [fn_name]() {
 * }
 *
 * int [fn_name]() {
 * }
 * @return
 */
ast_new_fn *parser_fn_decl(module_t *m, type_t type) {
    ast_new_fn *fn_decl = malloc(sizeof(ast_new_fn));

    // 必选 return type
    fn_decl->return_type = type;
    fn_decl->name = ""; // 避免随机值干扰

    // 可选函数名称
    if (parser_is(m, TOKEN_LITERAL_IDENT)) {
        token *name_token = parser_advance(m);
        fn_decl->name = name_token->literal;
    }

    parser_formal_param(m, fn_decl);

    fn_decl->body = parser_block(m);

    return fn_decl;
}

/**
 * else if 逻辑优化
 * if () {
 *
 * } else (if() {
 *
 * } else {
 *
 * })
 *
 * ↓
 * if () {
 *
 * } else {
 *     if () {
 *
 *     }
 * }
 *
 * @return
 */
ast_stmt *parser_if_stmt(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    ast_if_stmt *if_stmt = malloc(sizeof(ast_if_stmt));
    if_stmt->alternate = slice_new();
    if_stmt->consequent = slice_new();
    if_stmt->consequent->count = 0;
    if_stmt->alternate->count = 0;

    parser_must(m, TOKEN_IF);
    parser_must(m, TOKEN_LEFT_PAREN);
    if_stmt->condition = parser_expr(m);
    parser_must(m, TOKEN_RIGHT_PAREN);
    if_stmt->consequent = parser_block(m);

    if (parser_consume(m, TOKEN_ELSE)) {
        if (parser_is(m, TOKEN_IF)) {
            if_stmt->alternate = parser_else_if(m);
        } else {
            if_stmt->alternate = parser_block(m);
        }
    }

    result->assert_type = AST_STMT_IF;
    result->value = if_stmt;

    return result;
}

/**
 * if () {
 * } else if {
 * } else if {
 * ...
 * } else {
 * }
 * @return
 */
slice_t *parser_else_if(module_t *m) {
    slice_t *stmt_list = slice_new();
    slice_push(stmt_list, parser_if_stmt(m));

    return stmt_list;
}

/**
 * for (key,value in list) {
 *
 * }
 * @return
 */
ast_stmt *parser_for_stmt(module_t *m) {
    parser_advance(m);
    ast_stmt *result = parser_new_stmt();

    ast_for_in_stmt *for_in_stmt = malloc(sizeof(ast_for_in_stmt));
    for_in_stmt->gen_key = malloc(sizeof(ast_var_decl));

    parser_must(m, TOKEN_LEFT_PAREN);
    type_t type_var = parser_type(m);
    for_in_stmt->gen_key->type = type_var;
    for_in_stmt->gen_key->ident = parser_advance(m)->literal;

    // 可选参数 val
    if (parser_consume(m, TOKEN_COMMA)) {
        for_in_stmt->gen_value = malloc(sizeof(ast_var_decl));
        for_in_stmt->gen_value->type = type_var;
        for_in_stmt->gen_value->ident = parser_advance(m)->literal;
    }

    parser_must(m, TOKEN_IN);
    for_in_stmt->iterate = parser_expr(m);
    parser_must(m, TOKEN_RIGHT_PAREN);

    for_in_stmt->body = parser_block(m);

    result->assert_type = AST_STMT_FOR_IN;
    result->value = for_in_stmt;

    return result;
}

ast_stmt *parser_while_stmt(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    ast_while_stmt *while_stmt = malloc(sizeof(ast_while_stmt));
    parser_advance(m);
    while_stmt->condition = parser_expr(m);
    while_stmt->body = parser_block(m);

    result->assert_type = AST_STMT_WHILE;
    result->value = while_stmt;

    return result;
}

/**
 * @param type
 * @return
 */
parser_rule *parser_get_rule(token_type type) {
    return &rules[type];
}

ast_stmt *parser_assign(module_t *m, ast_expr left) {
    ast_stmt *result = parser_new_stmt();
    ast_assign_stmt *assign_stmt = malloc(sizeof(ast_assign_stmt));
    assign_stmt->left = left;
    // invalid: foo;
    parser_must(m, TOKEN_EQUAL);
    assign_stmt->right = parser_expr(m);

    result->assert_type = AST_STMT_ASSIGN;
    result->value = assign_stmt;

    return result;
}

void parser_cursor_init(module_t *module, list *token_list) {
    list_node *first = token_list->front;
    module->p_cursor.current = first;
}

token *parser_advance(module_t *m) {
    if (m->p_cursor.current->succ == NULL) {
        error_exit("next token is null");
    }
    token *t = m->p_cursor.current->value;
    m->p_cursor.current = m->p_cursor.current->succ;
#ifdef DEBUG_PARSER
    debug_parser(t->line, t->literal);
#endif
    return t;
}

token *parser_peek(module_t *m) {
    return m->p_cursor.current->value;
}

bool parser_is(module_t *m, token_type expect) {
    token *t = m->p_cursor.current->value;
    return t->type == expect;
}

bool parser_consume(module_t *m, token_type expect) {
    token *t = m->p_cursor.current->value;
    if (t->type == expect) {
        parser_advance(m);
        return true;
    }
    return false;
}

/**
 * ident 开头的表达式情况
 *
 * custom_type baz = 1;
 * custom_type baz;
 *
 * custom_type f() {
 *
 * }
 * call();
 * code() {
 * }
 *
 * foo = 1
 * foo.bar = 1
 * call().as = 2
 * @return
 */
ast_stmt *parser_ident_stmt(module_t *m) {

    // 消费左边的 ident
    ast_expr left = parser_expr(m);
    if (left.assert_type == AST_CALL) {
        if (parser_is(m, TOKEN_EQUAL)) {
            error_printf(parser_line(m), "function call cannot assign value");
            exit(0);
        }
        ast_stmt *stmt = parser_new_stmt();
        stmt->assert_type = AST_CALL;
        stmt->value = left.value;
        return stmt;
    }

    if (left.assert_type == AST_NEW_FN) {
        if (parser_is(m, TOKEN_EQUAL)) {
            error_printf(parser_line(m), "function decl cannot assign value");
            exit(0);
        }

        ast_stmt *stmt = parser_new_stmt();
        stmt->assert_type = AST_NEW_FN;
        stmt->value = left.value;
        return stmt;
    }

    // foo = 1 、foo.bar = 1 、foo[1] = 1、foo().as = 1;
    // foo = test();
    return parser_assign(m, left);
}

ast_expr parser_fn_decl_expr(module_t *m, type_t type) {
    ast_expr result = parser_new_expr(m);
    result.assert_type = AST_NEW_FN;
    result.value = parser_fn_decl(m, type);

    return result;
}

/**
 * not contains var
 * @return
 */
bool parser_is_direct_type(module_t *m) {
    if (parser_is_simple_type(m)) {
        return true;
    }

    // 复合类型前缀 [int] a; {int:int} b;
    if (parser_is(m, TOKEN_LEFT_SQUARE) || parser_is(m, TOKEN_LEFT_CURLY)) {
        return true;
    }

    if (parser_is(m, TOKEN_FUNCTION)
        || parser_is(m, TOKEN_STRUCT)
        || parser_is(m, TOKEN_MAP)
        || parser_is(m, TOKEN_ARRAY)) {
        return true;
    }

    if (parser_is(m, TOKEN_VAR)) {
        return true;
    }

    return false;
}

ast_stmt *parser_return_stmt(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    parser_advance(m);
    ast_return_stmt *stmt = malloc(sizeof(ast_return_stmt));

    // return } 或者 ;
    stmt->expr = NULL;
    if (!parser_is(m, TOKEN_EOF) &&
        !parser_is(m, TOKEN_STMT_EOF) &&
        !parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_expr temp = parser_expr(m);
        stmt->expr = NEW(ast_expr);
        memcpy(stmt->expr, &temp, sizeof(ast_expr));
    }
    result->assert_type = AST_STMT_RETURN;
    result->value = stmt;

    return result;
}

ast_stmt *parser_import_stmt(module_t *m) {
    ast_stmt *result = parser_new_stmt();
    parser_advance(m);
    ast_import *stmt = malloc(sizeof(ast_import));
    stmt->path = NULL;
    stmt->as = NULL;
    stmt->full_path = NULL;
    stmt->module_ident = NULL;

    token *token = parser_advance(m);
    if (token->type != TOKEN_LITERAL_STRING) {
        error_exit("[parser_import_stmt] import token not a string");
    }

    stmt->path = token->literal;
    if (parser_consume(m, TOKEN_AS)) {
        token = parser_advance(m);
        if (token->type != TOKEN_LITERAL_IDENT) {
            error_exit("[parser_import_stmt] import module_name not a TOKEN ident");
        }
        stmt->as = token->literal;
    }
    result->assert_type = AST_STMT_IMPORT;
    result->value = stmt;

    return result;
}


bool parser_is_simple_type(module_t *m) {
    if (parser_is(m, TOKEN_VOID)
        || parser_is(m, TOKEN_NULL)
        || parser_is(m, TOKEN_VAR)
        || parser_is(m, TOKEN_ANY)
        || parser_is(m, TOKEN_INT)
        || parser_is(m, TOKEN_FLOAT)
        || parser_is(m, TOKEN_BOOL)
        || parser_is(m, TOKEN_STRING)) {
        return true;
    }
    return false;
}

token *parser_must(module_t *m, token_type expect) {
    token *t = m->p_cursor.current->value;
    assertf(t->type == expect, "line: %d, parser error: expect '%s' token actual '%s' token",
            parser_line(m),
            token_type_to_string[expect],
            token_type_to_string[t->type]);

    parser_advance(m);
    return t;
}

bool parser_next_is(module_t *m, int step, token_type expect) {
    list_node *current = m->p_cursor.current;

    while (step > 0) {
        if (current->succ == NULL) {
            return false;
        }
        current = current->succ;
        step--;
    }

    token *t = current->value;
    return t->type == expect;
}

list_node *parser_next(module_t *m, int step) {
    list_node *current = m->p_cursor.current;

    while (step > 0) {
        if (current->succ == NULL) {
            return NULL;
        }

        current = current->succ;
        step--;
    }

    return current;
}

/**
 * [a, 1, call(), foo[1]]
 * @return
 */
ast_expr parser_new_list(module_t *m) {
    ast_expr result = parser_new_expr(m);
    ast_new_list *expr = malloc(sizeof(ast_new_list));
    expr->count = 0;
    parser_must(m, TOKEN_LEFT_SQUARE);

    if (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        expr->values[expr->count++] = parser_expr(m);
        while (parser_consume(m, TOKEN_COMMA)) {
            expr->values[expr->count++] = parser_expr(m);
        }
    }
    parser_must(m, TOKEN_RIGHT_SQUARE);

    result.assert_type = AST_EXPR_NEW_ARRAY;
    result.value = expr;

    return result;
}

static ast_map_item parser_map_item(module_t *m) {
    ast_map_item map_item = {
            .key = parser_expr(m)
    };
    parser_must(m, TOKEN_COLON);
    map_item.value = parser_expr(m);
    return map_item;
}

/**
 * {
 *     foo : bar,
 *     foo[1]: 12,
 * }
 * @return
 */
ast_expr parser_new_map(module_t *m) {
    ast_expr result = parser_new_expr(m);
    ast_new_map *expr = malloc(sizeof(ast_new_map));
    expr->count = 0;
    expr->capacity = 0;

    parser_must(m, TOKEN_LEFT_CURLY);
    if (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        expr->values[expr->count++] = parser_map_item(m);
        while (parser_consume(m, TOKEN_COMMA)) {
            expr->values[expr->count++] = parser_map_item(m);
        }
    }

    parser_must(m, TOKEN_RIGHT_CURLY);
    expr->capacity = expr->count;

    result.assert_type = AST_EXPR_NEW_MAP;
    result.value = expr;

    return result;
}

bool parser_must_stmt_end(module_t *m) {
    if (parser_is(m, TOKEN_EOF) || parser_is(m, TOKEN_RIGHT_CURLY)) {
        return true;
    }
    if (parser_is(m, TOKEN_STMT_EOF)) {
        parser_advance(m);
        return true;
    }

    assertf(false, "line: %d, except ; or } in stmt end", parser_line(m));
    return false;
}

/**
 * 左边 left 需要已经被消费到才行
 * (int a, custom b, map[a], list[c], fn<d>){
 * @return
 */
bool parser_is_fn_decl(module_t *m, list_node *current) {
    token *t = current->value;
    if (t->type != TOKEN_LEFT_PAREN) {
        error_exit("parser_is_fn_decl param must be TOKEN_LEFT_PAREN");
        return false;
    }

    // param is left paren, so close + 1 = 1,
    int close = 1;

    while (t->type != TOKEN_EOF) {
        current = current->succ;
        t = current->value;

        if (t->type == TOKEN_LEFT_PAREN) {
            close++;
        }

        if (t->type == TOKEN_RIGHT_PAREN) {
            close--;
            if (close == 0) {
                break;
            }
        }
    }

    if (close > 0) {
        return false;
    }

    // next is '{' ?
    t = current->succ->value;
    if (t->type != TOKEN_LEFT_CURLY) {
        return false;
    }

    return true;
}

ast_stmt *parser_new_stmt(module_t *m) {
    ast_stmt *result = NEW(ast_stmt);
    result->line = parser_line(m);
    return result;
}

ast_expr parser_new_expr(module_t *m) {
    ast_expr result = {
            .line = parser_line(m)
    };
    return result;
}

int parser_line(module_t *m) {
    return parser_peek(m)->line;
}

ast_expr parser_new_struct(module_t *m, type_t type) {
    ast_expr result;
    ast_new_struct *new_struct = malloc(sizeof(ast_new_struct));
    new_struct->count = 0;
    new_struct->type = type;

    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        // ident 类型
        ast_struct_property item;
        item.key = parser_must(m, TOKEN_LITERAL_IDENT)->literal;
        parser_must(m, TOKEN_EQUAL);
        item.value = parser_expr(m);
        new_struct->list[new_struct->count++] = item;
        parser_must_stmt_end(m);
    }

    parser_must(m, TOKEN_RIGHT_CURLY);

    result.assert_type = AST_EXPR_NEW_STRUCT;
    result.value = new_struct;
    return result;
}

/**
 * 直接已类型开头，一定是函数声明!
 * @return
 */
ast_expr parser_direct_type_expr(module_t *m) {
    type_t type = parser_type(m);
    return parser_fn_decl_expr(m, type);
}

ast_expr parser_struct_type_expr(module_t *m) {
    type_t struct_type = parser_type(m);
    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        return parser_new_struct(m, struct_type);
    }
    return parser_fn_decl_expr(m, struct_type);
}

bool parser_is_custom_type_var(module_t *m) {
    if (!parser_is(m, TOKEN_LITERAL_IDENT)) {
        return false;
    }

    if (!parser_next_is(m, 1, TOKEN_LITERAL_IDENT)) {
        return false;
    }

    if (parser_next_is(m, 2, TOKEN_EQUAL) ||
        parser_next_is(m, 2, TOKEN_STMT_EOF) ||
        parser_next_is(m, 2, TOKEN_EOF)) {
        return true;
    }

    return false;
}

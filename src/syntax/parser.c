#include "parser.h"
#include "token.h"
#include "utils/error.h"
#include <stdio.h>
#include "src/debug/debug.h"
#include <string.h>


static token_t *parser_advance(module_t *m) {
    if (m->p_cursor.current->succ == NULL) {
        error_exit("next token_t is null");
    }
    token_t *t = m->p_cursor.current->value;
    m->p_cursor.current = m->p_cursor.current->succ;
#ifdef DEBUG_PARSER
    debug_parser(t->line, t->literal);
#endif
    return t;
}

static token_t *parser_peek(module_t *m) {
    return m->p_cursor.current->value;
}

static bool parser_is(module_t *m, token_e expect) {
    token_t *t = m->p_cursor.current->value;
    return t->type == expect;
}

static bool parser_consume(module_t *m, token_e expect) {
    token_t *t = m->p_cursor.current->value;
    if (t->type == expect) {
        parser_advance(m);
        return true;
    }
    return false;
}

static int parser_line(module_t *m) {
    return parser_peek(m)->line;
}

static token_t *parser_must(module_t *m, token_e expect) {
    token_t *t = m->p_cursor.current->value;
    assertf(t->type == expect, "line: %d, parser error: expect '%s' token_t actual '%s' token_t",
            parser_line(m),
            token_str[expect],
            token_str[t->type]);

    parser_advance(m);
    return t;
}

static bool parser_next_is(module_t *m, int step, token_e expect) {
    linked_node *current = m->p_cursor.current;

    while (step > 0) {
        if (current->succ == NULL) {
            return false;
        }
        current = current->succ;
        step--;
    }

    token_t *t = current->value;
    return t->type == expect;
}

static linked_node *parser_next(module_t *m, int step) {
    linked_node *current = m->p_cursor.current;

    while (step > 0) {
        if (current->succ == NULL) {
            return NULL;
        }

        current = current->succ;
        step--;
    }

    return current;
}

static bool parser_is_custom_type_var(module_t *m) {
    if (!parser_is(m, TOKEN_IDENT)) {
        return false;
    }

    if (!parser_next_is(m, 1, TOKEN_IDENT)) {
        return false;
    }

    if (parser_next_is(m, 2, TOKEN_EQUAL) ||
        parser_next_is(m, 2, TOKEN_STMT_EOF) ||
        parser_next_is(m, 2, TOKEN_EOF)) {
        return true;
    }

    return false;
}


static ast_stmt *stmt_new(module_t *m) {
    ast_stmt *result = NEW(ast_stmt);
    result->line = parser_line(m);
    return result;
}

static ast_expr expr_new(module_t *m) {
    ast_expr result = {
            .line = parser_line(m)
    };
    return result;
}


static bool parser_must_stmt_end(module_t *m) {
    if (parser_is(m, TOKEN_EOF) || parser_is(m, TOKEN_RIGHT_CURLY)) {
        return true;
    }
    // ;
    if (parser_is(m, TOKEN_STMT_EOF)) {
        parser_advance(m);
        return true;
    }

    assertf(false, "line: %d, except ; or } in stmt end", parser_line(m));
    return false;
}

static bool parser_basic_token_type(module_t *m) {
    if (parser_is(m, TOKEN_VAR)
        || parser_is(m, TOKEN_SELF)
        || parser_is(m, TOKEN_ANY)
        || parser_is(m, TOKEN_INT)
        || parser_is(m, TOKEN_I8)
        || parser_is(m, TOKEN_I16)
        || parser_is(m, TOKEN_I32)
        || parser_is(m, TOKEN_I64)
        || parser_is(m, TOKEN_UINT)
        || parser_is(m, TOKEN_U8)
        || parser_is(m, TOKEN_U16)
        || parser_is(m, TOKEN_U32)
        || parser_is(m, TOKEN_U64)
        || parser_is(m, TOKEN_FLOAT)
        || parser_is(m, TOKEN_F32)
        || parser_is(m, TOKEN_F64)
        || parser_is(m, TOKEN_BOOL)
        || parser_is(m, TOKEN_STRING)) {
        return true;
    }
    return false;
}

static slice_t *parser_block(module_t *m) {
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

/**
 * 兼容 var
 * @return
 */
static type_t parser_typeuse(module_t *m) {
    type_t result = {
            .status = REDUCTION_STATUS_UNDO
    };

    // int/float/bool/string/void/var/any/self
    if (parser_basic_token_type(m)) {
        token_t *type_token = parser_advance(m);
        result.kind = token_to_kind[type_token->type];
        result.value = NULL;
        return result;
    }

    // ptr<int>
    if (parser_consume(m, TOKEN_POINTER)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_pointer_t *type_pointer = NEW(type_pointer_t);
        type_pointer->value_type = parser_typeuse(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);

        result.kind = TYPE_POINTER;
        result.pointer = type_pointer;
        return result;
    }

    // [int] a = []
    if (parser_consume(m, TOKEN_LEFT_SQUARE)) {
        type_list_t *type_list = NEW(type_list_t);
        type_list->element_type = parser_typeuse(m);

        parser_must(m, TOKEN_RIGHT_SQUARE);
        result.kind = TYPE_LIST;
        result.list = type_list;
        return result;
    }

    // (int, float)
    if (parser_consume(m, TOKEN_LEFT_PAREN)) {
        type_tuple_t *tuple = NEW(type_tuple_t);
        tuple->elements = ct_list_new(sizeof(type_t));
        do {
            type_t t = parser_typeuse(m);
            ct_list_push(tuple->elements, &t);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_PAREN);
    }

    // {int:int} or {int}
    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        type_t key_type = parser_typeuse(m);
        if (parser_consume(m, TOKEN_COLON)) {
            // map
            type_map_t *map = NEW(type_map_t);
            map->key_type = key_type;
            map->value_type = parser_typeuse(m);
            parser_must(m, TOKEN_RIGHT_CURLY);
            result.kind = TYPE_MAP;
            result.map = map;
            return result;
        } else {
            // set
            type_set_t *set = NEW(type_set_t);
            set->key_type = key_type;
            parser_must(m, TOKEN_RIGHT_CURLY);
            result.kind = TYPE_MAP;
            result.set = set;
            return result;
        }
    }

    // struct {
    //   int a
    //   int b
    // }
    if (parser_consume(m, TOKEN_STRUCT)) {
        type_struct_t *type_struct = NEW(type_struct_t);
        type_struct->properties = ct_list_new(sizeof(struct_property_t));
        parser_must(m, TOKEN_LEFT_CURLY);
        while (!parser_consume(m, TOKEN_RIGHT_CURLY)) {
            // default value
            struct_property_t item = {0};
            item.type = parser_typeuse(m);
            item.key = parser_advance(m)->literal;

            if (parser_consume(m, TOKEN_EQUAL)) {
                ast_expr *temp_expr = NEW(ast_expr);
                *temp_expr = parser_expr(m);
                item.right = temp_expr;
            }

            ct_list_push(type_struct->properties, &item);
            parser_must_stmt_end(m);
        }

        result.kind = TYPE_STRUCT;
        result.struct_ = type_struct;
        return result;
    }

    // fn(int, int):int f
    if (parser_consume(m, TOKEN_FN)) {
        type_fn_t *typeuse_fn = NEW(type_fn_t);
        typeuse_fn->formal_types = ct_list_new(sizeof(type_t));

        parser_must(m, TOKEN_LEFT_PAREN);
        if (!parser_consume(m, TOKEN_RIGHT_PAREN)) {
            // 包含参数类型
            do {
                type_t t = parser_typeuse(m);
                ct_list_push(typeuse_fn->formal_types, &t);
            } while (parser_consume(m, TOKEN_COMMA));
            parser_consume(m, TOKEN_RIGHT_PAREN);
        }

        parser_must(m, TOKEN_COLON);
        typeuse_fn->return_type = parser_typeuse(m);
        result.kind = TYPE_FN;
        result.fn = typeuse_fn;
        return result;
    }

    // person a
    if (parser_is(m, TOKEN_IDENT)) {
        token_t *type_token = parser_advance(m);
        result.kind = TYPE_IDENT;
        result.ident = typeuse_ident_new(type_token->literal);
        return result;
    }

    assertf(false, "line: %d, parser typedecl failed", parser_line(m));
    exit(1);
}

/**
 * type foo = int
 * @return
 */
static ast_stmt *parser_typedef_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    ast_typedef_stmt *type_decl_stmt = malloc(sizeof(ast_typedef_stmt));
    parser_must(m, TOKEN_TYPE); // code
    type_decl_stmt->ident = parser_must(m, TOKEN_IDENT)->literal; // ident
    parser_must(m, TOKEN_EQUAL); // =
    // 类型解析
    type_decl_stmt->type = parser_typeuse(m); // int

    result->assert_type = AST_STMT_TYPEDEF;
    result->value = type_decl_stmt;

    return result;
}

static ast_var_decl *parser_var_decl(module_t *m) {
    type_t var_type = parser_typeuse(m);

    // 变量名称必须为 ident
    token_t *var_ident = parser_advance(m);
    assertf(var_ident->type == TOKEN_IDENT, "parser variable definitions error, '%s' not a ident",
            var_ident->literal);

    ast_var_decl *var_decl = malloc(sizeof(ast_var_decl));
    var_decl->type = var_type;
    var_decl->ident = var_ident->literal;
    return var_decl;
}


static void parser_formals(module_t *m, ast_fndef_t *fn_decl) {
    parser_must(m, TOKEN_LEFT_PAREN);
    fn_decl->formals = ct_list_new(sizeof(ast_var_decl));
    // not formal params
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        return;
    }

    do {
        if (parser_consume(m, TOKEN_ELLIPSIS)) {
            fn_decl->rest_param = true;
        }

        // ref 本身就是堆上的地址，所以只需要把堆上的地址交给数组就可以了
        ast_var_decl *ref = parser_var_decl(m);
        ct_list_push(fn_decl->formals, ref);

        if (fn_decl->rest_param) {
            assertf(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
        }
    } while (parser_consume(m, TOKEN_COMMA));

    parser_must(m, TOKEN_RIGHT_PAREN);
}

static ast_expr parser_binary(module_t *m, ast_expr left) {
    ast_expr result = expr_new(m);

    token_t *operator_token = parser_advance(m);

    parser_precedence precedence = find_rule(operator_token->type)->infix_precedence;
    ast_expr right = parser_precedence_expr(m, precedence + 1);

    ast_binary_expr *binary_expr = NEW(ast_binary_expr);

    binary_expr->operator = token_to_ast_op[operator_token->type];
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
static ast_expr parser_unary(module_t *m) {
    ast_expr result = expr_new(m);
    token_t *operator_token = parser_advance(m);
    ast_expr operand = parser_precedence_expr(m, PRECEDENCE_UNARY);

    ast_unary_expr *unary_expr = malloc(sizeof(ast_unary_expr));

    if (operator_token->type == TOKEN_NOT) { // !true
        unary_expr->operator = AST_OP_NOT;
    } else if (operator_token->type == TOKEN_MINUS) { // -2
        unary_expr->operator = AST_OP_NEG;
    } else if (operator_token->type == TOKEN_TILDE) { // ~0b2
        unary_expr->operator = AST_OP_BNOT;
    } else if (operator_token->type == TOKEN_AND) { // &a
        unary_expr->operator = AST_OP_LA;
    } else if (operator_token->type == TOKEN_STAR) { // *a
        unary_expr->operator = AST_OP_IA;
    } else {
        assertf(false, "unknown unary operator=%d", token_str[operator_token->type]);
    }


    unary_expr->operand = operand;

    result.assert_type = AST_EXPR_UNARY;
    result.value = unary_expr;

    return result;
}

/**
 * 普通表达式 (1 + 1)
 * tuple (1, true, false)
 */
static ast_expr parser_left_paren_expr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_expr expr = parser_expr(m); // 算术表达式中的 ()
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        return expr;
    }

    ast_tuple_new *tuple = NEW(ast_tuple_new);
    tuple->elements = ct_list_new(sizeof(ast_expr));
    ct_list_push(tuple->elements, &expr);
    do {
        expr = parser_expr(m);
        ct_list_push(tuple->elements, &expr);
    } while (parser_consume(m, TOKEN_COMMA));
    parser_must(m, TOKEN_RIGHT_PAREN);

    expr = expr_new(m);
    expr.assert_type = AST_EXPR_TUPLE_NEW;
    expr.value = tuple;

    return expr;
}

static ast_expr parser_literal(module_t *m) {
    ast_expr result = expr_new(m);
    token_t *literal_token = parser_advance(m);
    ast_literal *literal_expr = malloc(sizeof(ast_literal));
    literal_expr->kind = token_to_kind[literal_token->type];
    literal_expr->value = literal_token->literal; // 具体数值

    result.assert_type = AST_EXPR_LITERAL;
    result.value = literal_expr;

    return result;
}

static bool parser_is_tuple_typedecl(module_t *m, linked_node *current) {
    token_t *t = current->value;
    assertf(t->type == TOKEN_LEFT_PAREN, "tuple typedecl start left param");

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

    // (...) ident; ) 的 下一符号如果是 ident 就表示 (...) 里面是 tuple typedecl
    t = current->succ->value;
    if (t->type != TOKEN_IDENT) {
        return false;
    }

    return true;
}

/**
 * person {
 *   a = 1,
 *   b = 2,
 *   c = 3
 * }
 * @param m
 * @param type
 * @return
 */
static ast_expr parser_struct_new(module_t *m, type_t type) {
    ast_expr result;
    ast_struct_new_t *struct_new = NEW(ast_struct_new_t);
    struct_new->properties = ct_list_new(sizeof(struct_property_t));

    if (parser_consume(m, TOKEN_RIGHT_CURLY)) {
        do {
            // ident 类型
            struct_property_t item;
            item.key = parser_must(m, TOKEN_IDENT)->literal;
            parser_must(m, TOKEN_EQUAL);
            item.right = NEW(ast_expr);
            *((ast_expr *) item.right) = parser_expr(m);

            ct_list_push(struct_new->properties, &item);
        } while (parser_consume(m, TOKEN_COMMA));
    }

    result.assert_type = AST_EXPR_STRUCT_NEW;
    result.value = struct_new;
    return result;
}


/**
 * 右值以 ident 开头的表达式处理
 * call();
 * foo.bar();
 * foo[bar]()
 *
 * foo
 * foo[bar]
 * foo.bar
 *
 * person {
 * }
 *
 *
 * @return
 */
static ast_expr parser_ident_expr(module_t *m) {
    ast_expr result = expr_new(m);

    token_t *ident_token = parser_must(m, TOKEN_IDENT);

    /**
      * 请注意这里是实例化一个结构体,而不是声明一个结构体
      * 声明 type person = struct{int a, int b}
      * 实例化
      * var a =  person {
      *              a = 1
      *              b = 2
      * }
      **/
    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        type_t typeuse_ident = {
                .kind = TYPE_IDENT,
                .status = REDUCTION_STATUS_UNDO,
                .ident = typeuse_ident_new(ident_token->literal)
        };
        return parser_struct_new(m, typeuse_ident);
    }

    // call()  a.b a[b], a.call() other ident prefix right
    result.assert_type = AST_EXPR_IDENT;
    result.value = ast_new_ident(ident_token->literal);

    return result;
}

/**
 * 暂时无法确定 foo 的类型是 list 还是 map 还是 tuple
 * foo[right]
 * @param left
 * @return
 */
static ast_expr parser_access(module_t *m, ast_expr left) {
    ast_expr result = expr_new(m);

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
static ast_expr parser_select(module_t *m, ast_expr left) {
    ast_expr result = expr_new(m);

    parser_must(m, TOKEN_DOT);

    token_t *property_token = parser_must(m, TOKEN_IDENT);
    ast_select *select = NEW(ast_select);

    select->left = left;
    select->key = property_token->literal; // struct 的 property 不能是运行时计算的结果，必须是具体的值

    result.assert_type = AST_EXPR_ACCESS;
    result.value = select;

    return result;
}


static void parser_actual_param(module_t *m, ast_call *call) {
    parser_must(m, TOKEN_LEFT_PAREN);

    if (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        do {
            // call(...a)
//            if (parser_consume(m, TOKEN_ELLIPSIS)) {
//                call->spread_param = true;
//            }
            // 参数解析 call(1 + 1, param_a)
            ast_expr expr = parser_expr(m);
            ct_list_push(call->actual_params, &expr);
//            if (call->spread_param) {
//                assertf(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
//                break;
//            }
        } while (parser_consume(m, TOKEN_COMMA));
    }

    parser_must(m, TOKEN_RIGHT_PAREN);
}

static ast_expr parser_call_expr(module_t *m, ast_expr left_expr) {
    ast_expr result = expr_new(m);

    ast_call *call_stmt = NEW(ast_call);
    call_stmt->actual_params = ct_list_new(sizeof(ast_expr));
    call_stmt->left = left_expr;

    // param handle
    parser_actual_param(m, call_stmt);

    // 如果 left_expr 是 ident ,且 == set, 那么将其转化成 ast_set_new
    if (left_expr.assert_type == AST_EXPR_IDENT &&
        str_equal(((ast_ident *) left_expr.value)->literal, RT_CALL_SET_CALL_IDENT)) {
        ast_set_new *set_new = NEW(ast_set_new);
        set_new->keys = call_stmt->actual_params;
        result.assert_type = AST_EXPR_MAP_NEW;
        result.value = set_new;
        return result;
    }

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

/**
 * if () {
 * } else if {
 * } else if {
 * ...
 * } else {
 * }
 * @return
 */
static slice_t *parser_else_if(module_t *m) {
    slice_t *stmt_list = slice_new();
    slice_push(stmt_list, parser_if_stmt(m));

    return stmt_list;
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
static ast_stmt *parser_if_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
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
 * 只有变量声明是以类型开头
 * var a = xxx
 * int a = xxx
 * bool a = xxx
 * string a = xxx
 * {x:x} a = xxx
 * {x} a = xxx
 * [x] a = xxx
 * (x, x, x) a = xxx
 * fn(x):x a = xxx // 区分 fn a(x): x {}
 * custom_x a = xxx # 连续两个 ident 判定就能判定出来
 * @return
 */
static bool is_typedecl(module_t *m) {
    // var/any/int/float/bool/string
    if (parser_basic_token_type(m)) {
        return true;
    }

    if (parser_is(m, TOKEN_LEFT_CURLY) || // {int}/{int:int}
        parser_is(m, TOKEN_LEFT_SQUARE)) { // [int]
        return true;
    }

    if (parser_is(m, TOKEN_POINTER)) {
        return true;
    }

    // fndef type (stmt 维度禁止了匿名 fndef, 所以这里一定是 fndef type)
    if (parser_is(m, TOKEN_FN) && parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
        return true;
    }

    // person a 连续两个 ident， 第一个 ident 一定是类型 ident
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_IDENT)) {
        return true;
    }

    // (var_a, var_b) = (1, 2)
    // (custom, int, int, (int, int), map) a = xxx
    if (parser_is(m, TOKEN_LEFT_PAREN) && parser_is_tuple_typedecl(m, m->p_cursor.current)) {
        return true;
    }

    return false;
}


/**
 * for (key,value in list) {
 *
 * }
 * @return
 */
static ast_stmt *parser_for_stmt(module_t *m) {
    parser_advance(m);
    ast_stmt *result = stmt_new(m);
    parser_consume(m, TOKEN_FOR);
    parser_must(m, TOKEN_LEFT_PAREN);

    // for (int a = 1....)
    if (is_typedecl(m)) {
        ast_for_tradition_stmt *for_tradition_stmt = NEW(ast_for_iterator_stmt);
        for_tradition_stmt->init = parser_stmt(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->cond = parser_expr(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->update = parser_stmt(m);
        parser_must(m, TOKEN_RIGHT_PAREN);
        for_tradition_stmt->body = parser_block(m);

        result->assert_type = AST_STMT_FOR_TRADITION;
        result->value = for_tradition_stmt;
        return result;
    }

    // for (k,v in map) {}
    if (parser_is(m, TOKEN_IDENT) && (parser_next_is(m, 1, TOKEN_COMMA) || parser_next_is(m, 1, TOKEN_IN))) {
        ast_for_iterator_stmt *for_iterator_stmt = NEW(ast_for_iterator_stmt);
        for_iterator_stmt->key.type = type_basic_new(TYPE_UNKNOWN);
        for_iterator_stmt->key.ident = parser_must(m, TOKEN_IDENT)->literal;

        if (parser_consume(m, TOKEN_COMMA)) {
            for_iterator_stmt->value = NEW(ast_var_decl);
            for_iterator_stmt->value->type = type_basic_new(TYPE_UNKNOWN);
            for_iterator_stmt->value->ident = parser_must(m, TOKEN_IDENT)->literal;
        }

        parser_must(m, TOKEN_IN);
        for_iterator_stmt->iterate = parser_expr(m);
        parser_must(m, TOKEN_RIGHT_PAREN);
        for_iterator_stmt->body = parser_block(m);

        result->assert_type = AST_STMT_FOR_ITERATOR;
        result->value = for_iterator_stmt;
        return result;
    }

    // for (condition) {}
    ast_for_cond_stmt *for_cond = NEW(ast_for_cond_stmt);
    for_cond->condition = parser_expr(m);
    for_cond->body = parser_block(m);
    result->assert_type = AST_STMT_FOR_COND;
    result->value = for_cond;
    return result;
}

/**
 * @param m
 * @param left
 * @return
 */
static ast_stmt *parser_assign(module_t *m, ast_expr left) {
    ast_stmt *result = stmt_new(m);
    ast_assign_stmt *assign_stmt = NEW(ast_assign_stmt);
    assign_stmt->left = left;

    if (parser_consume(m, TOKEN_EQUAL)) {
        assign_stmt->right = parser_expr(m);
        result->assert_type = AST_STMT_ASSIGN;
        result->value = assign_stmt;
        return result;
    }


    token_t *t = parser_advance(m);
    assertf(token_complex_assign(t->type), "assign=%v token exception", token_str[t->type]);

    // 转换成逻辑运算符
    ast_binary_expr *binary_expr = NEW(ast_binary_expr);
    binary_expr->right = parser_expr(m);
    binary_expr->operator = token_to_ast_op[t->type];
    binary_expr->left = left;

    assign_stmt->right = (ast_expr) {
            .assert_type = AST_EXPR_BINARY,
            .value = binary_expr
    };
    result->assert_type = AST_STMT_ASSIGN;
    result->value = assign_stmt;
    return result;
}

static void parser_cursor_init(module_t *module, linked_t *token_list) {
    linked_node *first = token_list->front;
    module->p_cursor.current = first;
}

/**
 * ident 开头的表达式情况, 最终的 stmt 只有 assign 和 call 两种形式
 * call();
 * foo.bar();
 * foo[bar]()
 *
 * foo = 1
 * foo[bar] = 1
 * foo.bar = 1
 *
 * bar += 1
 *
 * @return
 */
static ast_stmt *parser_ident_begin_stmt(module_t *m) {
    ast_expr left = parser_expr(m);
    if (left.assert_type == AST_CALL) {
        assertf(!parser_is(m, TOKEN_EQUAL), "fn call cannot assign");
        // call right to call stamt
        ast_stmt *stmt = stmt_new(m);
        stmt->assert_type = AST_CALL;
        stmt->value = left.value;
        return stmt;
    }

    // 不是 call 那接下来一定就是 assign 了
    // foo = 1 、foo.bar = 1 、foo[1] = 1、foo().as = 1;
    return parser_assign(m, left);
}


static ast_stmt *parser_return_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
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

static ast_stmt *parser_import_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    parser_advance(m);
    ast_import *stmt = malloc(sizeof(ast_import));
    stmt->path = NULL;
    stmt->as = NULL;
    stmt->full_path = NULL;
    stmt->module_ident = NULL;

    token_t *token = parser_advance(m);
    assertf(token->type == TOKEN_LITERAL_STRING, "import token must string");

    stmt->path = token->literal;
    if (parser_consume(m, TOKEN_AS)) {
        token = parser_advance(m);
        assertf(token->type == TOKEN_IDENT, "import as must ident");
        stmt->as = token->literal;
    }
    result->assert_type = AST_STMT_IMPORT;
    result->value = stmt;

    return result;
}


/**
 * [a, 1, call(), foo[1]]
 * @return
 */
static ast_expr parser_list_new(module_t *m) {
    ast_expr result = expr_new(m);
    ast_list_new *list_new = NEW(ast_list_new);
    list_new->values = ct_list_new(sizeof(ast_expr));
    parser_must(m, TOKEN_LEFT_SQUARE);

    if (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        do {
            ast_expr expr = parser_expr(m);
            ct_list_push(list_new->values, &expr);
        } while (parser_consume(m, TOKEN_COMMA));
    }
    parser_must(m, TOKEN_RIGHT_SQUARE);

    result.assert_type = AST_EXPR_LIST_NEW;
    result.value = list_new;

    return result;
}

static ast_map_element parser_map_item(module_t *m) {
    ast_map_element map_item = {
            .key = parser_expr(m)
    };
    parser_must(m, TOKEN_COLON);
    map_item.value = parser_expr(m);
    return map_item;
}

/**
 * var a =  {xx: xx, xx: xx} // map
 * var b = {xx, xx, xx} // set
 * @return
 */
static ast_expr parser_left_curly_expr(module_t *m) {
    ast_expr result = expr_new(m);

    // xxx a = {}
    parser_must(m, TOKEN_LEFT_CURLY);
    if (parser_consume(m, TOKEN_RIGHT_CURLY)) {
        // {} 默认是字典
        ast_map_new *map_new = NEW(ast_map_new);
        map_new->elements = ct_list_new(sizeof(ast_map_element));

        result.assert_type = AST_EXPR_MAP_NEW;
        result.value = map_new;
        return result;
    }

    ast_expr key_expr = parser_expr(m);
    if (parser_consume(m, TOKEN_COLON)) {
        // map
        ast_map_new *map_new = NEW(ast_map_new);
        map_new->elements = ct_list_new(sizeof(ast_map_element));
        ast_map_element element = {.key = key_expr, .value = parser_expr(m)};
        ct_list_push(map_new->elements, &element);
        while (parser_consume(m, TOKEN_COMMA)) {
            element.key = parser_expr(m);
            parser_must(m, TOKEN_COLON);
            element.value = parser_expr(m);
            ct_list_push(map_new->elements, &element);
        }
        parser_must(m, TOKEN_RIGHT_CURLY);
        result.assert_type = AST_EXPR_MAP_NEW;
        result.value = map_new;
        return result;
    }

    // set
    ast_set_new *expr = NEW(ast_set_new);
    expr->keys = ct_list_new(sizeof(ast_expr));
    ct_list_push(expr->keys, &key_expr);
    while (parser_consume(m, TOKEN_COMMA)) {
        key_expr = parser_expr(m);
        ct_list_push(expr->keys, &key_expr);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);
    result.assert_type = AST_EXPR_SET_NEW;
    result.value = expr;

    return result;
}

/**
 * fn def 如果是右值定义时，是允许匿名的
 * @param m
 * @return
 */
static ast_expr parser_fndef_expr(module_t *m) {
    ast_expr result = expr_new(m);
    ast_fndef_t *fn_decl = NEW(ast_fndef_t);
    fn_decl->name = "";

    parser_must(m, TOKEN_FN);
    if (parser_is(m, TOKEN_IDENT)) {
        token_t *name_token = parser_advance(m);
        fn_decl->name = name_token->literal;
    }

    parser_formals(m, fn_decl);

    if (parser_consume(m, TOKEN_COLON)) {
        fn_decl->return_type = parser_typeuse(m);
    } else {
        fn_decl->return_type = type_basic_new(TYPE_VOID);
    }

    fn_decl->body = parser_block(m);

    result.assert_type = AST_FNDEF;
    result.value = fn_decl;

    return result;
}

static ast_expr parser_catch_expr(module_t *m) {
    ast_expr result = expr_new(m);
    parser_must(m, TOKEN_CATCH);

    ast_expr call_expr = parser_expr(m);
    assertf(call_expr.assert_type == AST_CALL, "the catch target must be call operand");

    ast_catch *catch = NEW(ast_catch);
    catch->call = call_expr.value;
    catch->call->catch = true;

    result.assert_type = AST_EXPR_CATCH;
    result.value = catch;
    return result;
}

// (a, (b, c)) = (1, (2, 3))
static ast_tuple_destr *parser_tuple_destr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);

    ast_tuple_destr *result = NEW(ast_tuple_destr);
    result->elements = ct_list_new(sizeof(ast_expr));
    do {
        ast_expr expr = expr_new(m);

        if (parser_is(m, TOKEN_LEFT_PAREN)) {
            ast_tuple_destr *t = parser_tuple_destr(m);
            expr.assert_type = AST_EXPR_TUPLE_DESTR;
            expr.value = t;
        } else {
            expr = parser_expr(m);
            // a a[0], a["b"] a.b
            assertf(can_assign(expr.assert_type), "tuple destr src must can assign operand");
        }

        ct_list_push(result->elements, &expr);
    } while (parser_consume(m, TOKEN_COMMA));
    parser_must(m, TOKEN_RIGHT_PAREN);

    return result;
}


// var (a, (b, c)) = (1, (2, 3))
static ast_tuple_destr *parser_var_tuple_destr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);

    ast_tuple_destr *result = NEW(ast_tuple_destr);
    result->elements = ct_list_new(sizeof(ast_expr));

    do {
        ast_expr expr = expr_new(m);
        // ident or tuple destr
        if (parser_is(m, TOKEN_LEFT_PAREN)) {
            ast_tuple_destr *t = parser_var_tuple_destr(m);
            expr.assert_type = AST_EXPR_TUPLE_DESTR;
            expr.value = t;
        } else {
            token_t *ident_token = parser_must(m, TOKEN_IDENT);
            ast_var_decl *var_decl = NEW(ast_var_decl);
            var_decl->type = type_basic_new(TYPE_UNKNOWN);
            var_decl->ident = ident_token->literal;
            expr.assert_type = AST_VAR_DECL;
            expr.value = var_decl;
        }

        ct_list_push(result->elements, &expr);
    } while (parser_consume(m, TOKEN_COMMA));
    parser_must(m, TOKEN_RIGHT_PAREN);

    return result;
}

/**
 * var a = xxx
 * var (a, b) = xx
 * @param m
 * @return
 */
static ast_stmt *parser_var_begin_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    type_t typedecl = parser_typeuse(m);

    // var (a, b)
    if (parser_is(m, TOKEN_LEFT_PAREN)) {
        ast_var_tuple_def_stmt *stmt = NEW(ast_var_tuple_def_stmt);
        stmt->tuple_destr = parser_var_tuple_destr(m);
        parser_must(m, TOKEN_EQUAL);
        stmt->right = parser_expr(m);
        result->assert_type = AST_STMT_VAR_TUPLE_DESTR;
        result->value = stmt;
        return result;
    }

    // var a = 1 这样的标准情况
    ast_vardef_stmt *var_assign = NEW(ast_vardef_stmt);
    token_t *ident_token = parser_must(m, TOKEN_IDENT);
    var_assign->var_decl.type = typedecl;
    var_assign->var_decl.ident = ident_token->literal;
    parser_must(m, TOKEN_EQUAL);
    var_assign->right = parser_expr(m);
    result->assert_type = AST_STMT_VAR_DEF;
    result->value = var_assign;

    return result;
}

// int a = 1
static ast_stmt *parser_typeuse_begin_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    // 类型解析
    type_t typedecl = parser_typeuse(m);
    token_t *ident_token = parser_advance(m);

    // next param 不能是 (a, b) 这样的形式, 不支持
    assertf(!parser_is(m, TOKEN_LEFT_PAREN), "only support var (a, b) this form decl assign");

    ast_var_decl *var_decl = NEW(ast_var_decl);
    var_decl->type = typedecl;
    var_decl->ident = ident_token->literal;

    // var a = 1
    if (parser_consume(m, TOKEN_EQUAL)) {
        ast_vardef_stmt *stmt = NEW(ast_vardef_stmt);
        stmt->right = parser_expr(m);
        stmt->var_decl = *var_decl;
        result->assert_type = AST_STMT_VAR_DEF;
        result->value = stmt;
        return result;
    }

    result->assert_type = AST_VAR_DECL;
    result->value = var_decl;
    return result;
}

/**
 * // name 不可省略，暂时不支持匿名函数
 * fn name(int a, int b): int {
 * }
 * @param m
 * @return
 */
static ast_stmt *parser_fndef_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    ast_fndef_t *fn_decl = NEW(ast_fndef_t);

    parser_must(m, TOKEN_FN);
    // stmt 中 name 不允许省略
    token_t *name_token = parser_must(m, TOKEN_IDENT);
    fn_decl->name = name_token->literal;
    parser_formals(m, fn_decl);
    // 可选返回参数
    if (parser_consume(m, TOKEN_COLON)) {
        fn_decl->return_type = parser_typeuse(m);
    } else {
        fn_decl->return_type = type_basic_new(TYPE_VOID);
    }

    fn_decl->body = parser_block(m);

    result->assert_type = AST_FNDEF;
    result->value = fn_decl;
    return result;
}

static ast_stmt *parser_throw_stmt(module_t *m) {
    parser_must(m, TOKEN_THROW);
    ast_stmt *result = stmt_new(m);
    ast_throw_stmt *throw_stmt = NEW(ast_throw_stmt);
    throw_stmt->error = parser_expr(m);
    result->assert_type = AST_STMT_THROW;
    result->value = throw_stmt;
    return result;
}

// (var_a, var_b) = xxx
static ast_stmt *parser_tuple_destr_stmt(module_t *m) {
    ast_stmt *result = stmt_new(m);
    ast_assign_stmt *assign_stmt = NEW(ast_assign_stmt);

    // assign_stmt
    ast_expr left_expr = {
            .assert_type = AST_EXPR_TUPLE_DESTR,
            .value = parser_tuple_destr(m)
    };

    assign_stmt->left = left_expr;
    // tuple destr 必须立刻赋值
    parser_must(m, TOKEN_EQUAL);
    assign_stmt->right = parser_expr(m);

    result->value = assign_stmt;
    result->assert_type = AST_STMT_ASSIGN;

    return result;
}

/**
 * // var decl

 *
 * // fn decl
 * fn a(x): x {}
 *
 * // var assign
 * a = xxx
 * a[b] = xxx
 * a.b = xxx
 *
 * // call()
 * a()
 *
 *
 * @param m
 * @return
 */
static ast_stmt *parser_stmt(module_t *m) {
    if (parser_is(m, TOKEN_VAR)) {
        // 更快的发现类型推断上的问题
        return parser_var_begin_stmt(m);
    } else if (is_typedecl(m)) {
        return parser_typeuse_begin_stmt(m);
    } else if (parser_is(m, TOKEN_LEFT_PAREN)) {
        // 已 left param 开头的类型推断已经在 is_typedecl 中完成了，这里就是 tuple destr assign 的情况了
        return parser_tuple_destr_stmt(m);
    } else if (parser_is(m, TOKEN_THROW)) {
        return parser_throw_stmt(m);
    } else if (parser_is(m, TOKEN_IDENT)) {
        return parser_ident_begin_stmt(m);
    } else if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_stmt(m);
    } else if (parser_is(m, TOKEN_IF)) {
        return parser_if_stmt(m);
    } else if (parser_is(m, TOKEN_FOR)) {
        return parser_for_stmt(m);
    } else if (parser_is(m, TOKEN_RETURN)) {
        return parser_return_stmt(m);
    } else if (parser_is(m, TOKEN_IMPORT)) {
        return parser_import_stmt(m);
    } else if (parser_is(m, TOKEN_TYPE)) {
        // type a = xxx
        return parser_typedef_stmt(m);
    }

    assertf(false, "line=%d, cannot parser stmt", parser_line(m));
    exit(1);
}

static parser_rule rules[] = {
        [TOKEN_LEFT_PAREN] = {parser_left_paren_expr, parser_call_expr, PRECEDENCE_CALL},
        [TOKEN_LEFT_SQUARE] = {parser_list_new, parser_access, PRECEDENCE_CALL},
        [TOKEN_LEFT_CURLY] = {parser_left_curly_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_DOT] = {NULL, parser_select, PRECEDENCE_CALL},
        [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
        [TOKEN_NOT] = {parser_unary, NULL, PRECEDENCE_NULL},
        [TOKEN_TILDE] = {parser_unary, NULL, PRECEDENCE_NULL},
        [TOKEN_AND] = {parser_unary, parser_binary, PRECEDENCE_AND},
        [TOKEN_OR] = {NULL, parser_binary, PRECEDENCE_OR},
        [TOKEN_XOR] = {NULL, parser_binary, PRECEDENCE_XOR},
        [TOKEN_LEFT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
        [TOKEN_RIGHT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
        [TOKEN_STAR] = {parser_unary, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_PLUS] = {NULL, parser_binary, PRECEDENCE_TERM},
        [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_OR_OR] = {NULL, parser_binary, PRECEDENCE_OR_OR},
        [TOKEN_AND_AND] = {NULL, parser_binary, PRECEDENCE_AND_AND},
        [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},
        [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},
        [TOKEN_RIGHT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_GREATER_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LESS_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LITERAL_STRING] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_INT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_FLOAT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_TRUE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_FALSE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_CATCH] = {parser_catch_expr, NULL, PRECEDENCE_NULL},

        // 以 ident 开头的前缀表达式
        [TOKEN_IDENT] = {parser_ident_expr, NULL, PRECEDENCE_NULL},

        // var a = fn [name](int a, int b): bool {
        // }
        [TOKEN_FN] = {parser_fndef_expr, NULL, PRECEDENCE_NULL},

        [TOKEN_EOF] = {NULL, NULL, PRECEDENCE_NULL},
};

/**
 * @param type
 * @return
 */
static parser_rule *find_rule(token_e type) {
    return &rules[type];
}


static ast_expr parser_precedence_expr(module_t *m, parser_precedence precedence) {
    // 读取表达式前缀
    parser_prefix_fn prefix_fn = find_rule(parser_peek(m)->type)->prefix;
    assertf(prefix_fn, "line=%d, cannot parser ident=%s", parser_line(m), parser_peek(m)->literal);

    ast_expr expr = prefix_fn(m); // advance

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则按表达式优先级进行处理, 如果 +/-/*// /. /[]  等
    token_e type = parser_peek(m)->type;
    parser_rule *infix_rule = find_rule(type);
    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;
        expr = infix_fn(m, expr);

        infix_rule = find_rule(parser_peek(m)->type);
    }

    return expr;
}

/**
 * 表达式优先级处理方式
 * @return
 */
static ast_expr parser_expr(module_t *m) {
    return parser_precedence_expr(m, PRECEDENCE_ASSIGN);
}

slice_t *parser(module_t *m, linked_t *token_list) {
    parser_cursor_init(m, token_list);

    slice_t *block_stmt = slice_new();

    ast_type_t stmt_type = -1;

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

#include "parser.h"
#include "token.h"
#include "utils/error.h"
#include <stdio.h>
#include "src/debug/debug.h"
#include <string.h>
#include "src/error.h"

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

static token_t *parser_must(module_t *m, token_e expect) {
    token_t *t = m->p_cursor.current->value;

    PARSER_ASSERTF(t->type == expect, "parser error expect '%s' actual '%s'", token_str[expect], t->literal);


    parser_advance(m);
    return t;
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

static ast_stmt_t *stmt_new(module_t *m) {
    ast_stmt_t *result = NEW(ast_stmt_t);
    result->line = parser_peek(m)->line;
    result->column = parser_peek(m)->column;
    return result;
}


static ast_expr_t *expr_new_ptr(module_t *m) {
    ast_expr_t *result = NEW(ast_expr_t);
    result->line = parser_peek(m)->line;
    result->column = parser_peek(m)->column;
    return result;
}

static ast_expr_t expr_new(module_t *m) {
    ast_expr_t result = {
            .line = parser_peek(m)->line,
            .column = parser_peek(m)->column,
    };
    return result;
}

static bool parser_must_stmt_end(module_t *m) {
    if (parser_is(m, TOKEN_EOF) || parser_is(m, TOKEN_RIGHT_CURLY)) {
        return true;
    }

    // ; (scanner 时主动添加)
    if (parser_is(m, TOKEN_STMT_EOF)) {
        parser_advance(m);
        return true;
    }

    PARSER_ASSERTF(false, "except ; or } in stmt end");
    return false;
}

static bool parser_basic_token_type(module_t *m) {
    if (parser_is(m, TOKEN_VAR)
        || parser_is(m, TOKEN_CPTR)
        || parser_is(m, TOKEN_NULL)
        || parser_is(m, TOKEN_SELF)
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
        ast_stmt_t *stmt = parser_stmt(m);
        parser_must_stmt_end(m);

        slice_push(stmt_list, stmt);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);

    return stmt_list;
}

/**
 * example
 *
 * - i8|i16|[i32]
 * - ident<i8>
 * - [i8]
 *
 * union type 没有特殊引导,
 * parser_type 不包含 generic 的处理, generic 只能在 type xxx = generic 中声明
 * @param m
 * @return
 */
static type_t parser_type(module_t *m) {
    type_t t = parser_single_type(m);
    if (!parser_is(m, TOKEN_OR)) {
        return t;
    }

    parser_must(m, TOKEN_OR);
    type_t union_type = {
            .status = REDUCTION_STATUS_UNDO,
            .kind = TYPE_UNION,
    };
    union_type.union_ = NEW(type_union_t);
    union_type.union_->elements = ct_list_new(sizeof(type_t));
    ct_list_push(union_type.union_->elements, &t);

    do {
        t = parser_single_type(m);
        ct_list_push(union_type.union_->elements, &t);
    } while (parser_consume(m, TOKEN_OR));

    return union_type;
}

/**
 * - 兼容 var
 * - 兼容 i8|i16... 这样的形式
 * @return
 */
static type_t parser_single_type(module_t *m) {
    type_t result = {
            .status = REDUCTION_STATUS_UNDO,
            .line = parser_peek(m)->line,
            .column = parser_peek(m)->column,
    };

    // any 特殊处理
    if (parser_consume(m, TOKEN_ANY)) {
        type_t union_type = {
                .status = REDUCTION_STATUS_UNDO,
                .kind = TYPE_UNION,
        };
        union_type.union_ = NEW(type_union_t);
        union_type.union_->elements = ct_list_new(sizeof(type_t));
        union_type.union_->any = true;
        return union_type;
    }

    // int/float/bool/string/void/var/self
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
        type_pointer->value_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);

        result.kind = TYPE_POINTER;
        result.pointer = type_pointer;
        return result;
    }

    // [int] a = []
    if (parser_consume(m, TOKEN_LEFT_SQUARE)) {
        type_list_t *type_list = NEW(type_list_t);
        type_list->element_type = parser_type(m);

//        if (parser_consume(m, TOKEN_COMMA)) {
//            token_t *t = parser_must(m, TOKEN_LITERAL_INT);
//            int length = atoi(t->literal);
//
//            PARSER_ASSERTF(length > 0, "list len must > 0")
//        }

        parser_must(m, TOKEN_RIGHT_SQUARE);
        result.kind = TYPE_LIST;
        result.list = type_list;
        return result;
    }

    // array<int>
    if (parser_consume(m, TOKEN_ARRAY)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_array_t *type_array = NEW(type_array_t);
        type_array->element_type = parser_type(m);
        parser_consume(m, TOKEN_COMMA);
        token_t *t = parser_must(m, TOKEN_LITERAL_INT);
        int length = atoi(t->literal);
        PARSER_ASSERTF(length > 0, "array len must > 0")
        type_array->length = length;
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_ARRAY;
        result.array = type_array;
        return result;
    }

    // (int, float)
    if (parser_consume(m, TOKEN_LEFT_PAREN)) {
        type_tuple_t *tuple = NEW(type_tuple_t);
        tuple->elements = ct_list_new(sizeof(type_t));
        do {
            type_t t = parser_type(m);
            ct_list_push(tuple->elements, &t);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_PAREN);
        result.kind = TYPE_TUPLE;
        result.tuple = tuple;
        return result;
    }

    // {int:int} or {int}
    if (parser_consume(m, TOKEN_LEFT_CURLY)) {
        type_t key_type = parser_type(m);
        if (parser_consume(m, TOKEN_COLON)) {
            // map
            type_map_t *map = NEW(type_map_t);
            map->key_type = key_type;
            map->value_type = parser_type(m);
            parser_must(m, TOKEN_RIGHT_CURLY);
            result.kind = TYPE_MAP;
            result.map = map;
            return result;
        } else {
            // set
            type_set_t *set = NEW(type_set_t);
            set->element_type = key_type;
            parser_must(m, TOKEN_RIGHT_CURLY);
            result.kind = TYPE_SET;
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
            struct_property_t item = {
                    .type = parser_type(m),
                    .key = parser_advance(m)->literal
            };

            if (parser_consume(m, TOKEN_EQUAL)) {
                ast_expr_t *temp_expr = expr_new_ptr(m);
                *temp_expr = parser_expr(m);
                if (temp_expr->assert_type == AST_FNDEF) {
                    ast_fndef_t *fn = temp_expr->value;
                    PARSER_ASSERTF(fn->symbol_name == NULL,
                                   "fn defined in struct cannot contain name");
                }

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
        typeuse_fn->param_types = ct_list_new(sizeof(type_t));

        parser_must(m, TOKEN_LEFT_PAREN);
        if (!parser_consume(m, TOKEN_RIGHT_PAREN)) {
            // 包含参数类型
            do {
                type_t t = parser_type(m);
                ct_list_push(typeuse_fn->param_types, &t);
            } while (parser_consume(m, TOKEN_COMMA));
            parser_consume(m, TOKEN_RIGHT_PAREN);
        }

        if (parser_consume(m, TOKEN_COLON)) {
            typeuse_fn->return_type = parser_type(m);
        } else {
            typeuse_fn->return_type = type_kind_new(TYPE_VOID);
        }
        result.kind = TYPE_FN;
        result.fn = typeuse_fn;
        return result;
    }

    if (parser_is(m, TOKEN_IDENT)) {
        token_t *first = parser_advance(m);

        // type formal 快速处理, foo<formal1, formal2>
        if (m->parser_type_params && table_exist(m->parser_type_params, first->literal)) {
            result.kind = TYPE_PARAM;
            result.param = type_formal_new(first->literal);
            return result;
        }

        // a.b
        token_t *second = NULL;
        if (parser_consume(m, TOKEN_DOT)) {
            second = parser_advance(m);
        }

        result.kind = TYPE_ALIAS;
        if (second) {
            result.alias = type_alias_new(second->literal, first->literal);
        } else {
            result.alias = type_alias_new(first->literal, NULL);
        }

        // 参数处理
        if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
            // parser actual params
            result.alias->args = ct_list_new(sizeof(type_t));
            do {
                type_t item = parser_type(m);
                ct_list_push(result.alias->args, &item);
            } while (parser_consume(m, TOKEN_COMMA));

            parser_must(m, TOKEN_RIGHT_ANGLE);
        }

        return result;
    }

    PARSER_ASSERTF(false, "ident '%s' is not a type", parser_peek(m)->literal);
    exit(1);
}

/**
 * type foo = int
 * type foo<i8> = [i8]
 * type foo = gen i8|u8|i16
 * @return
 */
static ast_stmt_t *parser_type_alias_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    ast_type_alias_stmt_t *type_alias_stmt = NEW(ast_type_alias_stmt_t);
    parser_must(m, TOKEN_TYPE); // code
    type_alias_stmt->ident = parser_must(m, TOKEN_IDENT)->literal; // ident

    // <arg1, arg2>
    if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_RIGHT_ANGLE), "type alias params cannot empty");

        type_alias_stmt->params = ct_list_new(sizeof(ast_ident));
        m->parser_type_params = table_new();

        do {
            token_t *ident = parser_advance(m);
            ast_ident *temp = ast_new_ident(ident->literal);
            ct_list_push(type_alias_stmt->params, temp);

            table_set(m->parser_type_params, ident->literal, ident);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_consume(m, TOKEN_RIGHT_ANGLE);
    }

    parser_must(m, TOKEN_EQUAL); // =
    result->assert_type = AST_STMT_TYPE_ALIAS;
    result->value = type_alias_stmt;

    // type number = generic i8|i16|i32|i64
    if (parser_consume(m, TOKEN_GEN)) {
        type_t gen = {
                .status = REDUCTION_STATUS_UNDO,
                .kind = TYPE_GEN,
        };
        gen.gen = NEW(type_gen_t);
        gen.gen->elements = ct_list_new(sizeof(type_t));
        do {
            type_t item = parser_single_type(m); // 至少包含一个约束
            if (item.kind == TYPE_UNION && item.union_->any) {
                gen.gen->any = true;
            }
            ct_list_push(gen.gen->elements, &item);
        } while (parser_consume(m, TOKEN_OR));

        if (gen.gen->any) {
            PARSER_ASSERTF(gen.gen->elements->length == 1, "generic any must only one constraint");
        }
        type_alias_stmt->type = gen;
        return result;
    }

    type_alias_stmt->type = parser_type(m);
    m->parser_type_params = NULL; // 右值解析完成后需要及时清空
    return result;
}

static ast_var_decl_t *parser_var_decl(module_t *m) {
    type_t var_type = parser_type(m);

    // 变量名称必须为 ident
    token_t *var_ident = parser_advance(m);
    PARSER_ASSERTF(var_ident->type == TOKEN_IDENT, "parser variable definitions error, '%s' not a ident",
                   var_ident->literal);

    ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
    var_decl->type = var_type;
    var_decl->ident = var_ident->literal;
    return var_decl;
}

static void parser_formals(module_t *m, ast_fndef_t *fn_decl) {
    parser_must(m, TOKEN_LEFT_PAREN);
    fn_decl->params = ct_list_new(sizeof(ast_var_decl_t));
    // not formal params
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        return;
    }

    do {
        if (parser_consume(m, TOKEN_ELLIPSIS)) {
            fn_decl->rest_param = true;
        }

        // ref 本身就是堆上的地址，所以只需要把堆上的地址交给数组就可以了
        ast_var_decl_t *ref = parser_var_decl(m);
        ct_list_push(fn_decl->params, ref);

        if (fn_decl->rest_param) {
            PARSER_ASSERTF(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
        }
    } while (parser_consume(m, TOKEN_COMMA));

    parser_must(m, TOKEN_RIGHT_PAREN);
}

static ast_expr_t parser_binary(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);

    token_t *operator_token = parser_advance(m);

    parser_precedence precedence = find_rule(operator_token->type)->infix_precedence;
    ast_expr_t right = parser_precedence_expr(m, precedence + 1);

    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);

    binary_expr->operator = token_to_ast_op[operator_token->type];
    binary_expr->left = left;
    binary_expr->right = right;

    result.assert_type = AST_EXPR_BINARY;
    result.value = binary_expr;

    return result;
}

/**
 * ! 取反
 * - 取负数
 * @return
 */
static ast_expr_t parser_unary(module_t *m) {
    ast_expr_t result = expr_new(m);
    token_t *operator_token = parser_advance(m);

    ast_unary_expr_t *unary_expr = malloc(sizeof(ast_unary_expr_t));
    if (operator_token->type == TOKEN_NOT) { // !true
        unary_expr->operator = AST_OP_NOT;
    } else if (operator_token->type == TOKEN_MINUS) { // -2
        // 推断下一个 token 是不是一个数字 literal, 如果是直接合并成 ast_literal 即可
        if (parser_is(m, TOKEN_LITERAL_INT)) {
            token_t *int_token = parser_advance(m);
            ast_literal_t *literal = NEW(ast_literal_t);
            literal->kind = TYPE_INT;
            literal->value = str_connect("-", int_token->literal);
            result.assert_type = AST_EXPR_LITERAL;
            result.value = literal;
            return result;
        }

        if (parser_is(m, TOKEN_LITERAL_FLOAT)) {
            token_t *float_token = parser_advance(m);
            ast_literal_t *literal = NEW(ast_literal_t);
            literal->kind = TYPE_FLOAT;
            literal->value = str_connect("-", float_token->literal);
            result.assert_type = AST_EXPR_LITERAL;
            result.value = literal;
            return result;
        }

        unary_expr->operator = AST_OP_NEG;
    } else if (operator_token->type == TOKEN_TILDE) { // ~0b2
        unary_expr->operator = AST_OP_BNOT;
    } else if (operator_token->type == TOKEN_AND) { // &a
        unary_expr->operator = AST_OP_LA;
    } else if (operator_token->type == TOKEN_STAR) { // *a
        unary_expr->operator = AST_OP_IA;
    } else {
        PARSER_ASSERTF(false, "unknown unary operator '%d'", token_str[operator_token->type]);
    }

    ast_expr_t operand = parser_precedence_expr(m, PRECEDENCE_UNARY);
    unary_expr->operand = operand;

    result.assert_type = AST_EXPR_UNARY;
    result.value = unary_expr;

    return result;
}

static ast_expr_t parser_as_expr(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_AS);
    ast_as_expr_t *as_expr = NEW(ast_as_expr_t);
    as_expr->target_type = parser_single_type(m);
    as_expr->src = left;
    result.assert_type = AST_EXPR_AS;
    result.value = as_expr;
    return result;
}

static ast_expr_t parser_is_expr(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_IS);
    ast_is_expr_t *is_expr = NEW(ast_is_expr_t);
    is_expr->target_type = parser_single_type(m);
    is_expr->src_operand = left;
    result.assert_type = AST_EXPR_IS;
    result.value = is_expr;
    return result;
}

/**
 * 普通表达式 (1 + 1)
 * tuple (1, true, false)
 */
static ast_expr_t parser_left_paren_expr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_expr_t expr = parser_expr(m); // 算术表达式中的 ()
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        // ast 本身已经包含了 group 的含义，所以这里不需要特别再包一层 group 了
        return expr;
    }

    // tuple new
    parser_must(m, TOKEN_COMMA);
    // 下一个是逗号才能判断为 tuple
    ast_tuple_new_t *tuple = NEW(ast_tuple_new_t);
    tuple->elements = ct_list_new(sizeof(ast_expr_t));
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


    return expr;
}

static ast_expr_t parser_literal(module_t *m) {
    ast_expr_t result = expr_new(m);
    token_t *literal_token = parser_advance(m);
    ast_literal_t *literal_expr = NEW(ast_literal_t);
    literal_expr->kind = token_to_kind[literal_token->type];
    literal_expr->value = literal_token->literal; // 具体数值

    result.assert_type = AST_EXPR_LITERAL;
    result.value = literal_expr;

    return result;
}

static bool parser_is_tuple_typedecl(module_t *m, linked_node *current) {
    token_t *t = current->value;
    PARSER_ASSERTF(t->type == TOKEN_LEFT_PAREN, "tuple typedecl start left param");

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
 * person<a, b> {
 * }
 *
 * import_as.person { } 只包含一层解析，就不放在 select 处理了，直接在 ident 进行延伸处理
 *
 * @return
 */
static ast_expr_t parser_ident_expr(module_t *m) {
    // 这里传递一个 precedence 参数, 基于该参数判断是否
    ast_expr_t result = expr_new(m);
    token_t *ident_token = parser_must(m, TOKEN_IDENT);

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
static ast_expr_t parser_access(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);

    parser_must(m, TOKEN_LEFT_SQUARE);
    ast_expr_t key = parser_expr(m);
    parser_must(m, TOKEN_RIGHT_SQUARE);
    ast_access_t *access_expr = malloc(sizeof(ast_access_t));
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
static ast_expr_t parser_select(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);

    parser_must(m, TOKEN_DOT);

    token_t *property_token = parser_must(m, TOKEN_IDENT);
    ast_select_t *select = NEW(ast_select_t);

    select->left = left;
    select->key = property_token->literal; // struct 的 property 不能是运行时计算的结果，必须是具体的值

    result.assert_type = AST_EXPR_SELECT;
    result.value = select;

    return result;
}


static void parser_arg(module_t *m, ast_call_t *call) {
    parser_must(m, TOKEN_LEFT_PAREN);

    // 无调用参数
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        return;
    }

    do {
        if (parser_consume(m, TOKEN_ELLIPSIS)) {
            call->spread = true;
        }

        ast_expr_t expr = parser_expr(m);
        ct_list_push(call->args, &expr);

        if (call->spread) {
            PARSER_ASSERTF(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
        }

    } while (parser_consume(m, TOKEN_COMMA));

    parser_must(m, TOKEN_RIGHT_PAREN);
}

static ast_expr_t parser_call_expr(module_t *m, ast_expr_t left_expr) {
    ast_expr_t result = expr_new(m);

    ast_call_t *call_stmt = NEW(ast_call_t);
    call_stmt->args = ct_list_new(sizeof(ast_expr_t));
    call_stmt->left = left_expr;

    // param handle
    parser_arg(m, call_stmt);

    // 如果 left_expr 是 ident ,且 == set, 那么将其转化成 ast_set_new
    if (left_expr.assert_type == AST_EXPR_IDENT &&
        str_equal(((ast_ident *) left_expr.value)->literal, RT_CALL_SET_CALL_IDENT)) {
        ast_set_new_t *set_new = NEW(ast_set_new_t);
        set_new->elements = call_stmt->args;
        result.assert_type = AST_EXPR_SET_NEW;
        result.value = set_new;
        return result;
    }

    result.assert_type = AST_CALL;
    result.value = call_stmt;


    return result;
}


/**
 * if () {
 * } else if() {
 * } else if() {
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
static ast_stmt_t *parser_if_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    ast_if_stmt_t *if_stmt = malloc(sizeof(ast_if_stmt_t));
    if_stmt->alternate = slice_new();
    if_stmt->consequent = slice_new();
    if_stmt->consequent->count = 0;
    if_stmt->alternate->count = 0;

    parser_must(m, TOKEN_IF);
//    parser_must(m, TOKEN_LEFT_PAREN);
    if_stmt->condition = parser_expr_with_precedence(m);
//    parser_must(m, TOKEN_RIGHT_PAREN);
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

static bool prev_token_is_type(token_t *prev) {
    return prev->type == TOKEN_LEFT_PAREN ||
           prev->type == TOKEN_LEFT_CURLY ||
           prev->type == TOKEN_COLON ||
           prev->type == TOKEN_COMMA;
}

// for {{}};{};{{}}. {for ; ; }
static bool is_for_tradition_stmt(module_t *m) {
    int semicolon_count = 0;
    int close = 0;
    linked_node *current = m->p_cursor.current;
    int current_line = ((token_t *) current->value)->line;
    while (current->value) {
        token_t *t = current->value;

        PARSER_ASSERTF(t->type != TOKEN_EOF, "unexpected EOF")

        if (close == 0 && t->type == TOKEN_SEMICOLON) {
            semicolon_count++;
        }

        if (t->type == TOKEN_LEFT_CURLY) {
            close++;
        }

        if (t->type == TOKEN_RIGHT_CURLY) {
            close--;
        }

        if (t->line != current_line) {
            break;
        }

        current = current->succ;
    }

    PARSER_ASSERTF(semicolon_count == 0 || semicolon_count == 2, "for stmt exception");

    return semicolon_count == 2;
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
static bool is_type_begin_stmt(module_t *m) {
    // var/any/int/float/bool/string
    if (parser_basic_token_type(m)) {
        return true;
    }

    if (parser_is(m, TOKEN_ANY)) {
        return true;
    }

    if (parser_is(m, TOKEN_LEFT_CURLY) || // {int}/{int:int}
        parser_is(m, TOKEN_LEFT_SQUARE)) { // [int]
        return true;
    }

    if (parser_is(m, TOKEN_POINTER)) {
        return true;
    }

    if (parser_is(m, TOKEN_ARRAY)) {
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

    // package.ident foo = xxx
    if (parser_is(m, TOKEN_IDENT) &&
        parser_next_is(m, 1, TOKEN_DOT) &&
        parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_IDENT)) {
        return true;
    }

    // person|i8 a
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_OR)) {
        return true;
    }

    // package.ident|i8 foo = xxx
    if (parser_is(m, TOKEN_IDENT) &&
        parser_next_is(m, 1, TOKEN_DOT) &&
        parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_OR)) {
        return true;
    }

    // person<[i8]> foo
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_ANGLE)) {
        return true;
    }

    // person.foo<[i8]>
    if (parser_is(m, TOKEN_IDENT) &&
        parser_next_is(m, 1, TOKEN_DOT) &&
        parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_LEFT_ANGLE)) {
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
static ast_stmt_t *parser_for_stmt(module_t *m) {
    parser_advance(m);

    ast_stmt_t *result = stmt_new(m);
    parser_consume(m, TOKEN_FOR);
//    parser_must(m, TOKEN_LEFT_PAREN);

    // 通过找 ; 号的形式判断, 必须要有两个 ; 才会是 tradition
    // for int i = 1; i <= 10; i+=1
    if (is_for_tradition_stmt(m)) {
        ast_for_tradition_stmt_t *for_tradition_stmt = NEW(ast_for_iterator_stmt_t);
        for_tradition_stmt->init = parser_stmt(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->cond = parser_expr_with_precedence(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->update = parser_stmt(m);

        for_tradition_stmt->body = parser_block(m);

        result->assert_type = AST_STMT_FOR_TRADITION;
        result->value = for_tradition_stmt;
        return result;
    }

    // for k,v in map {}
    if (parser_is(m, TOKEN_IDENT) && (parser_next_is(m, 1, TOKEN_COMMA) || parser_next_is(m, 1, TOKEN_IN))) {
        ast_for_iterator_stmt_t *for_iterator_stmt = NEW(ast_for_iterator_stmt_t);
        for_iterator_stmt->first.type = type_kind_new(TYPE_UNKNOWN);
        for_iterator_stmt->first.ident = parser_must(m, TOKEN_IDENT)->literal;

        if (parser_consume(m, TOKEN_COMMA)) {
            for_iterator_stmt->second = NEW(ast_var_decl_t);
            // 需要根据 iterator 的类型对 key 和 value type 进行类型判断
            for_iterator_stmt->second->type = type_kind_new(TYPE_UNKNOWN);
            for_iterator_stmt->second->ident = parser_must(m, TOKEN_IDENT)->literal;
        }

        parser_must(m, TOKEN_IN);
        for_iterator_stmt->iterate = parser_expr_with_precedence(m);
        for_iterator_stmt->body = parser_block(m);

        result->assert_type = AST_STMT_FOR_ITERATOR;
        result->value = for_iterator_stmt;
        return result;
    }

    // for (condition) {}
    ast_for_cond_stmt_t *for_cond = NEW(ast_for_cond_stmt_t);
    for_cond->condition = parser_expr_with_precedence(m);
//    parser_must(m, TOKEN_RIGHT_PAREN);
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
static ast_stmt_t *parser_assign(module_t *m, ast_expr_t left) {
    ast_stmt_t *result = stmt_new(m);
    ast_assign_stmt_t *assign_stmt = NEW(ast_assign_stmt_t);
    assign_stmt->left = left;

    // 简单 assign
    if (parser_consume(m, TOKEN_EQUAL)) {
        assign_stmt->right = parser_expr(m);
        result->assert_type = AST_STMT_ASSIGN;
        result->value = assign_stmt;
        return result;
    }


    // complex assign
    token_t *t = parser_advance(m);
    PARSER_ASSERTF(token_complex_assign(t->type), "assign=%v token exception", token_str[t->type]);

    // 转换成逻辑运算符
    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);
    binary_expr->right = parser_precedence_expr(m, PRECEDENCE_STRUCT_NEW + 1);
    binary_expr->operator = token_to_ast_op[t->type];
    binary_expr->left = left;

    assign_stmt->right = expr_new(m);
    assign_stmt->right.assert_type = AST_EXPR_BINARY;
    assign_stmt->right.value = binary_expr;

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
static ast_stmt_t *parser_ident_begin_stmt(module_t *m) {
    ast_expr_t left = parser_expr(m);

    if (left.assert_type == AST_CALL) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_EQUAL), "fn call cannot assign");
        // call right to call stamt
        ast_stmt_t *stmt = stmt_new(m);
        stmt->assert_type = AST_CALL;
        stmt->value = left.value;
        return stmt;
    }

    // 不是 call 那接下来一定就是 assign 了
    // foo = 1 、foo.bar = 1 、foo[1] = 1、foo().as = 1;
    return parser_assign(m, left);
}

static ast_stmt_t *parser_break_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_BREAK);

    result->value = result;
    result->assert_type = AST_STMT_BREAK;
    return result;
}

static ast_stmt_t *parser_continue_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_CONTINUE);

    result->value = result;
    result->assert_type = AST_STMT_CONTINUE;
    return result;
}

static ast_stmt_t *parser_return_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_advance(m);
    ast_return_stmt_t *stmt = malloc(sizeof(ast_return_stmt_t));

    // return } 或者 ;
    stmt->expr = NULL;
    if (!parser_is(m, TOKEN_EOF) &&
        !parser_is(m, TOKEN_STMT_EOF) &&
        !parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_expr_t temp = parser_expr(m);

        stmt->expr = expr_new_ptr(m);
        memcpy(stmt->expr, &temp, sizeof(ast_expr_t));
    }
    result->assert_type = AST_STMT_RETURN;
    result->value = stmt;

    return result;
}

static ast_stmt_t *parser_import_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_advance(m);
    ast_import_t *stmt = malloc(sizeof(ast_import_t));
    stmt->file = NULL;
    stmt->package = slice_new();
    stmt->as = NULL;
    stmt->full_path = NULL;
    stmt->module_ident = NULL;

    token_t *token = parser_advance(m);
    if (token->type == TOKEN_LITERAL_STRING) {
        stmt->file = token->literal;
    } else {
        PARSER_ASSERTF(token->type == TOKEN_IDENT, "import token must string");
        slice_push(stmt->package, token->literal);
        while (parser_consume(m, TOKEN_DOT)) {
            token = parser_must(m, TOKEN_IDENT);
            slice_push(stmt->package, token->literal);
        }
    }

    if (parser_consume(m, TOKEN_AS)) { // 可选 as
        token = parser_advance(m);
        PARSER_ASSERTF(token->type == TOKEN_IDENT || token->type == TOKEN_STAR, "import as must ident");
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
static ast_expr_t parser_list_new(module_t *m) {
    ast_expr_t result = expr_new(m);
    ast_list_new_t *list_new = NEW(ast_list_new_t);
    list_new->elements = ct_list_new(sizeof(ast_expr_t));
    parser_must(m, TOKEN_LEFT_SQUARE);

    if (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        do {
            ast_expr_t expr = parser_expr(m);
            ct_list_push(list_new->elements, &expr);
        } while (parser_consume(m, TOKEN_COMMA));
    }
    parser_must(m, TOKEN_RIGHT_SQUARE);

    result.assert_type = AST_EXPR_LIST_NEW;
    result.value = list_new;

    return result;
}

/**
 * var a =  {xx: xx, xx: xx} // map
 * var b = {xx, xx, xx} // set
 * @return
 */
static ast_expr_t parser_left_curly_expr(module_t *m) {
    ast_expr_t result = expr_new(m);

    // xxx a = {}
    parser_must(m, TOKEN_LEFT_CURLY);
    if (parser_consume(m, TOKEN_RIGHT_CURLY)) {
        ast_empty_curly_new_t *empty_new = NEW(ast_empty_curly_new_t);
        empty_new->elements = ct_list_new(sizeof(ast_map_element_t));

        result.assert_type = AST_EXPR_EMPTY_CURLY_NEW;
        result.value = empty_new;
        return result;
    }

    ast_expr_t key_expr = parser_expr(m);
    if (parser_consume(m, TOKEN_COLON)) {
        // map
        ast_map_new_t *map_new = NEW(ast_map_new_t);
        map_new->elements = ct_list_new(sizeof(ast_map_element_t));
        ast_map_element_t element = {.key = key_expr, .value = parser_expr(m)};
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
    ast_set_new_t *expr = NEW(ast_set_new_t);
    expr->elements = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(expr->elements, &key_expr);
    while (parser_consume(m, TOKEN_COMMA)) {
        key_expr = parser_expr(m);
        ct_list_push(expr->elements, &key_expr);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);
    result.assert_type = AST_EXPR_SET_NEW;
    result.value = expr;

    return result;
}

/**
 * fn def 如果是右值定义时，必须使用匿名函数
 * @param m
 * @return
 */
static ast_expr_t parser_fndef_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    ast_fndef_t *fndef = ast_fndef_new(parser_peek(m)->line, parser_peek(m)->column);

    parser_must(m, TOKEN_FN);

    if (parser_is(m, TOKEN_IDENT)) {
        token_t *name_token = parser_advance(m);
        fndef->symbol_name = name_token->literal;
    }

    parser_formals(m, fndef);

    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);
    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
    }

    fndef->body = parser_block(m);

    result.assert_type = AST_FNDEF;
    result.value = fndef;

    return result;
}

static ast_expr_t parser_new_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_NEW);
    ast_new_expr_t *new_expr = NEW(ast_new_expr_t);
    new_expr->type = parser_type(m);
    result.assert_type = AST_EXPR_NEW;
    result.value = new_expr;
    return result;
}

static ast_expr_t parser_try_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_TRY);

    ast_expr_t expr = parser_expr(m);

    ast_try_t *try = NEW(ast_try_t);
    try->expr = expr;

    result.assert_type = AST_EXPR_TRY;
    result.value = try;
    return result;
}

static ast_expr_t parser_boom_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_BOOM);
    ast_expr_t expr = parser_expr(m);

    ast_boom_t *boom = NEW(ast_boom_t);
    boom->expr = expr;

    result.assert_type = AST_EXPR_BOOM;
    result.value = boom;
    return result;
}

// (a, (b, c)) = (1, (2, 3))
static ast_tuple_destr_t *parser_tuple_destr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);

    ast_tuple_destr_t *result = NEW(ast_tuple_destr_t);
    result->elements = ct_list_new(sizeof(ast_expr_t));
    do {
        ast_expr_t expr = expr_new(m);

        if (parser_is(m, TOKEN_LEFT_PAREN)) {
            ast_tuple_destr_t *t = parser_tuple_destr(m);
            expr.assert_type = AST_EXPR_TUPLE_DESTR;
            expr.value = t;
        } else {
            expr = parser_expr(m);
            // a a[0], a["b"] a.b
            PARSER_ASSERTF(can_assign(expr.assert_type), "tuple destr src must can assign operand");
        }

        ct_list_push(result->elements, &expr);
    } while (parser_consume(m, TOKEN_COMMA));
    parser_must(m, TOKEN_RIGHT_PAREN);

    return result;
}


// var (a, (b, c)) = (1, (2, 3))
static ast_tuple_destr_t *parser_var_tuple_destr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);

    ast_tuple_destr_t *result = NEW(ast_tuple_destr_t);
    result->elements = ct_list_new(sizeof(ast_expr_t));

    do {
        ast_expr_t expr = expr_new(m);
        // ident or tuple destr
        if (parser_is(m, TOKEN_LEFT_PAREN)) {
            ast_tuple_destr_t *t = parser_var_tuple_destr(m);
            expr.assert_type = AST_EXPR_TUPLE_DESTR;
            expr.value = t;
        } else {
            token_t *ident_token = parser_must(m, TOKEN_IDENT);
            ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
            var_decl->type = type_kind_new(TYPE_UNKNOWN);
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
static ast_stmt_t *parser_var_begin_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    type_t typedecl = parser_type(m);

    // var (a, b)
    if (parser_is(m, TOKEN_LEFT_PAREN)) {
        ast_var_tuple_def_stmt_t *stmt = NEW(ast_var_tuple_def_stmt_t);
        stmt->tuple_destr = parser_var_tuple_destr(m);
        parser_must(m, TOKEN_EQUAL);
        stmt->right = parser_expr(m);
        result->assert_type = AST_STMT_VAR_TUPLE_DESTR;
        result->value = stmt;
        return result;
    }

    // var a = 1 这样的标准情况
    ast_vardef_stmt_t *var_assign = NEW(ast_vardef_stmt_t);
    token_t *ident_token = parser_must(m, TOKEN_IDENT);
    var_assign->var_decl.type = typedecl;
    var_assign->var_decl.ident = ident_token->literal;
    parser_must(m, TOKEN_EQUAL);
    var_assign->right = parser_expr(m);
    result->assert_type = AST_STMT_VARDEF;
    result->value = var_assign;

    return result;
}

// int a = 1
static ast_stmt_t *parser_type_begin_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    // 类型解析
    type_t typedecl = parser_type(m);
    token_t *ident_token = parser_advance(m);

    // 仅 var 支持 tuple destr
    PARSER_ASSERTF(!parser_is(m, TOKEN_LEFT_PAREN), "only support var (a, b) this form decl assign");

    ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
    var_decl->type = typedecl;
    var_decl->ident = ident_token->literal;

    // 声明必须赋值
    parser_must(m, TOKEN_EQUAL);

    // var a = 1
    ast_vardef_stmt_t *stmt = NEW(ast_vardef_stmt_t);
    stmt->right = parser_expr(m);
    stmt->var_decl = *var_decl;
    result->assert_type = AST_STMT_VARDEF;
    result->value = stmt;
    return result;

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
static ast_stmt_t *parser_fndef_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    ast_fndef_t *fndef = ast_fndef_new(parser_peek(m)->line, parser_peek(m)->column);
    result->assert_type = AST_FNDEF;
    result->value = fndef;

    parser_must(m, TOKEN_FN);
    // stmt 中 name 不允许省略
    token_t *name_token = parser_must(m, TOKEN_IDENT);
    fndef->symbol_name = name_token->literal;
    parser_formals(m, fndef);

    // 可选返回参数
    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);
    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
    }

    if (m->type == MODULE_TYPE_TEMP) {
        // 绝对不可能是 {
        PARSER_ASSERTF(!parser_is(m, TOKEN_LEFT_CURLY), "temp module not support fn body");

        return result;
    }

    fndef->body = parser_block(m);
    return result;
}

static ast_stmt_t *parser_let_stmt(module_t *m) {
    parser_must(m, TOKEN_LET);
    ast_stmt_t *result = stmt_new(m);

    ast_let_t *stmt = NEW(ast_let_t);
    ast_expr_t expr = parser_expr(m);
    PARSER_ASSERTF(expr.assert_type == AST_EXPR_AS, "let stmt must be 'as' expr");
    stmt->expr = expr;
    result->assert_type = AST_STMT_LET;
    result->value = stmt;
    return result;
}

static ast_stmt_t *parser_throw_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_THROW);
    ast_throw_stmt_t *throw_stmt = NEW(ast_throw_stmt_t);
    throw_stmt->error = parser_expr(m);
    result->assert_type = AST_STMT_THROW;
    result->value = throw_stmt;
    return result;
}

// (var_a, var_b) = xxx
static ast_stmt_t *parser_tuple_destr_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    ast_assign_stmt_t *assign_stmt = NEW(ast_assign_stmt_t);

    // assign_stmt
    ast_expr_t left_expr = expr_new(m);
    left_expr.assert_type = AST_EXPR_TUPLE_DESTR;
    left_expr.value = parser_tuple_destr(m);

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
static ast_stmt_t *parser_stmt(module_t *m) {
    if (parser_is(m, TOKEN_VAR)) {
        // 更快的发现类型推断上的问题
        return parser_var_begin_stmt(m);
    } else if (is_type_begin_stmt(m)) {
        return parser_type_begin_stmt(m);
    } else if (parser_is(m, TOKEN_LEFT_PAREN)) {
        // 已 left param 开头的类型推断已经在 is_type_begin_stmt 中完成了，这里就是 tuple destr assign 的情况了
        return parser_tuple_destr_stmt(m);
    } else if (parser_is(m, TOKEN_THROW)) {
        return parser_throw_stmt(m);
    } else if (parser_is(m, TOKEN_LET)) {
        return parser_let_stmt(m);
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
        return parser_type_alias_stmt(m);
    } else if (parser_is(m, TOKEN_CONTINUE)) {
        return parser_continue_stmt(m);
    } else if (parser_is(m, TOKEN_BREAK)) {
        return parser_break_stmt(m);
    }

    PARSER_ASSERTF(false, "cannot parser stmt with = '%s'", parser_peek(m)->literal);
    exit(1);
}

/**
 * template 文件只能包含 type 和 fn 两种表达式
 * @param m
 * @return
 */
static ast_stmt_t *parser_template_stmt(module_t *m) {
    if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_stmt(m);
    } else if (parser_is(m, TOKEN_TYPE)) {
        return parser_type_alias_stmt(m);
    }

    PARSER_ASSERTF(false, "cannot parser stmt with = '%s' in template file", parser_peek(m)->literal);
    exit(EXIT_FAILURE);
}

static parser_rule rules[] = {
        [TOKEN_LEFT_PAREN] = {parser_left_paren_expr, parser_call_expr, PRECEDENCE_CALL},
        [TOKEN_LEFT_SQUARE] = {parser_list_new, parser_access, PRECEDENCE_CALL},
        [TOKEN_LEFT_CURLY] = {parser_left_curly_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_DOT] = {NULL, parser_select, PRECEDENCE_CALL},
        [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
        [TOKEN_PLUS] = {NULL, parser_binary, PRECEDENCE_TERM},
        [TOKEN_NOT] = {parser_unary, NULL, PRECEDENCE_UNARY},
        [TOKEN_TILDE] = {parser_unary, NULL, PRECEDENCE_UNARY},
        [TOKEN_AND] = {parser_unary, parser_binary, PRECEDENCE_AND},
        [TOKEN_OR] = {NULL, parser_binary, PRECEDENCE_OR},
        [TOKEN_XOR] = {NULL, parser_binary, PRECEDENCE_XOR},
        [TOKEN_LEFT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
        [TOKEN_RIGHT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
        [TOKEN_PERSON] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_STAR] = {parser_unary, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_OR_OR] = {NULL, parser_binary, PRECEDENCE_OR_OR},
        [TOKEN_AND_AND] = {NULL, parser_binary, PRECEDENCE_AND_AND},
        [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},
        [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},
        [TOKEN_RIGHT_ANGLE] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_GREATER_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LESS_EQUAL] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LITERAL_STRING] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_INT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_LITERAL_FLOAT] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_TRUE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_FALSE] = {parser_literal, NULL, PRECEDENCE_NULL},
        [TOKEN_NULL] = {parser_literal, NULL, PRECEDENCE_NULL},

        [TOKEN_AS] = {NULL, parser_as_expr, PRECEDENCE_TYPE_CAST},
        [TOKEN_IS] = {NULL, parser_is_expr, PRECEDENCE_TYPE_CAST},

        // 以 ident 开头的前缀表达式
        [TOKEN_IDENT] = {parser_ident_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_EOF] = {NULL, NULL, PRECEDENCE_NULL},
};

/**
 * @param type
 * @return
 */
static parser_rule *find_rule(token_e token_type) {
    return &rules[token_type];
}

static ast_expr_t parser_precedence_expr(module_t *m, parser_precedence precedence) {
    // 读取表达式前缀
    parser_prefix_fn prefix_fn = find_rule(parser_peek(m)->type)->prefix;

    PARSER_ASSERTF(prefix_fn, "cannot parser ident '%s'", parser_peek(m)->literal)

    ast_expr_t expr = prefix_fn(m); // advance

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则按表达式优先级进行处理, 如果 +/-/*// /. /[]  等
    token_e token_type = parser_peek(m)->type;
    parser_rule *infix_rule = find_rule(token_type);
    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;

        expr = infix_fn(m, expr);

        infix_rule = find_rule(parser_peek(m)->type);
    }

    return expr;
}

/**
 * foo < a.b, b<c.a>> {
 * 从 < 符号开始匹配，一单匹配到闭合符合则，且闭合符合的下一个符号是 {, 则说明当前是一个 struct new, 只是携带了 param
 * @return
 */
static bool is_struct_param_new_prefix(linked_node *current) {
    token_t *t = current->value;
    if (t->type != TOKEN_LEFT_ANGLE) {
        error_exit("parser_is_struct_param_new param must start with '<'");
        return false;
    }

    // param is left paren, so close + 1 = 1,
    int close = 1;

    while (t->type != TOKEN_EOF) {
        current = current->succ;
        t = current->value;

        if (t->type == TOKEN_LEFT_ANGLE) {
            close++;
        }

        if (t->type == TOKEN_RIGHT_ANGLE) {
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


static bool parser_is_struct_new_expr(module_t *m) {
    // foo {} ,  foo.bar<a.b, [int], ...> {},
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_CURLY)) {
        return true;
    }

    // foo.bar {},
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_LEFT_CURLY)) {
        return true;
    }

    // foo <a, b> {}
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_ANGLE)) {
        if (is_struct_param_new_prefix(parser_next(m, 1))) {
            return true;
        }
    }

    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_LEFT_ANGLE)) {
        if (is_struct_param_new_prefix(parser_next(m, 3))) {
            return true;
        }
    }

    return false;
}

static ast_expr_t parser_struct_new_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    type_t t = parser_type(m);
    ast_struct_new_t *struct_new = NEW(ast_struct_new_t);
    struct_new->type = t;
    struct_new->properties = ct_list_new(sizeof(struct_property_t));

    parser_must(m, TOKEN_LEFT_CURLY);
    if (!parser_consume(m, TOKEN_RIGHT_CURLY)) {
        do {
            // ident 类型
            struct_property_t item = {0};
            item.key = parser_must(m, TOKEN_IDENT)->literal;
            parser_must(m, TOKEN_EQUAL);
            item.right = expr_new_ptr(m);
            *((ast_expr_t *) item.right) = parser_expr(m);

            ct_list_push(struct_new->properties, &item);
        } while (parser_consume(m, TOKEN_COMMA));
        parser_consume(m, TOKEN_RIGHT_CURLY);
    }
    result.assert_type = AST_EXPR_STRUCT_NEW;
    result.value = struct_new;
    return result;
}

/**
 * 包含默认 precedence = PRECEDENCE_ASSIGN
 * @return
 */
static ast_expr_t parser_expr_with_precedence(module_t *m) {
    return parser_precedence_expr(m, PRECEDENCE_ASSIGN);
}


/**
 * 表达式优先级处理方式
 * @return
 */
static ast_expr_t parser_expr(module_t *m) {
    PARSER_ASSERTF(m->type != MODULE_TYPE_TEMP, "template file cannot contains expr");

    // struct new
    if (parser_is_struct_new_expr(m)) {
        return parser_struct_new_expr(m);
    }

    // try
    if (parser_is(m, TOKEN_TRY)) {
        return parser_try_expr(m);
    }

    // fn def
    if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_expr(m);
    }

    // new
    if (parser_is(m, TOKEN_NEW)) {
        return parser_new_expr(m);
    }

    return parser_expr_with_precedence(m);
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
        ast_stmt_t *stmt;
        if (m->type == MODULE_TYPE_TEMP) {
            stmt = parser_template_stmt(m);
        } else {
            stmt = parser_stmt(m);
        }

        slice_push(block_stmt, stmt);
        parser_must_stmt_end(m);
    }

    return block_stmt;
}

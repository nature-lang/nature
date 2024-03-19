#include "parser.h"

#include <stdio.h>
#include <string.h>

#include "src/debug/debug.h"
#include "src/error.h"
#include "token.h"
#include "utils/error.h"

static ast_expr_t parser_call_expr(module_t *m, ast_expr_t left_expr);
static list_t *parser_arg(module_t *m, ast_call_t *call);
static ast_expr_t parser_struct_new(module_t *m, type_t t);
static ast_expr_t parser_macro_call(module_t *m);
static ast_expr_t parser_struct_new_expr(module_t *m);
static ast_expr_t parser_go_expr(module_t *m);

static bool parser_is_impl_type(type_kind kind) {
    return kind == TYPE_STRING ||
           kind == TYPE_BOOL ||
           kind == TYPE_INT ||
           kind == TYPE_UINT ||
           kind == TYPE_INT8 ||
           kind == TYPE_INT16 ||
           kind == TYPE_INT32 ||
           kind == TYPE_INT64 ||
           kind == TYPE_UINT8 ||
           kind == TYPE_UINT16 ||
           kind == TYPE_UINT32 ||
           kind == TYPE_UINT64 ||
           kind == TYPE_FLOAT ||
           kind == TYPE_FLOAT32 ||
           kind == TYPE_FLOAT64 ||
           kind == TYPE_VEC ||
           kind == TYPE_MAP ||
           kind == TYPE_SET ||
           kind == TYPE_ALIAS;
}

static token_t *parser_advance(module_t *m) {
    assert(m->p_cursor.current->succ != NULL && "next token_t is null");
    token_t *t = m->p_cursor.current->value;
    m->p_cursor.current = m->p_cursor.current->succ;
#ifdef DEBUG_PARSER
    debug_parser(t->line, t->literal);
#endif
    return t;
}

/**
 * 和 advance 的相反实现
 * @return
 */
static token_t *parser_retreat(module_t *m) {
    assert(m->p_cursor.current->prev != NULL && "prev token_t is null");
    token_t *t = m->p_cursor.current->value;
    m->p_cursor.current = m->p_cursor.current->prev;
    return t;
}

static token_t *parser_peek(module_t *m) {
    return m->p_cursor.current->value;
}

static bool parser_is(module_t *m, token_type_t expect) {
    token_t *t = m->p_cursor.current->value;
    return t->type == expect;
}

static bool parser_consume(module_t *m, token_type_t expect) {
    token_t *t = m->p_cursor.current->value;
    if (t->type == expect) {
        parser_advance(m);
        return true;
    }
    return false;
}

static token_t *parser_must(module_t *m, token_type_t expect) {
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

// parser_next_is(0) = parser_is
static bool parser_next_is(module_t *m, int step, token_type_t expect) {
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

static ast_stmt_t *stmt_expr_fake_new(module_t *m, ast_expr_t expr) {
    ast_stmt_t *result = stmt_new(m);
    ast_expr_fake_stmt_t *value = NEW(ast_expr_fake_stmt_t);
    value->expr = expr;

    result->value = value;
    result->assert_type = AST_STMT_EXPR_FAKE;
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

static bool parser_is_basic_type(module_t *m) {
    if (parser_is(m, TOKEN_VAR) || parser_is(m, TOKEN_NULL) || parser_is(m, TOKEN_INT) || parser_is(m, TOKEN_VOID) ||
        parser_is(m, TOKEN_I8) || parser_is(m, TOKEN_I16) || parser_is(m, TOKEN_I32) || parser_is(m, TOKEN_I64) ||
        parser_is(m, TOKEN_UINT) || parser_is(m, TOKEN_U8) || parser_is(m, TOKEN_U16) || parser_is(m, TOKEN_U32) ||
        parser_is(m, TOKEN_U64) || parser_is(m, TOKEN_FLOAT) || parser_is(m, TOKEN_F32) || parser_is(m, TOKEN_F64) ||
        parser_is(m, TOKEN_BOOL) || parser_is(m, TOKEN_STRING)) {
        return true;
    }
    return false;
}

static slice_t *parser_body(module_t *m) {
    slice_t *stmt_list = slice_new();

    parser_must(m, TOKEN_LEFT_CURLY);// 必须是
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
 * - cptr/nptr/
 * @return
 */
static type_t parser_single_type(module_t *m) {
    type_t result = {
            .status = REDUCTION_STATUS_UNDO,
            .line = parser_peek(m)->line,
            .column = parser_peek(m)->column,
            .origin_ident = NULL,
            .impl_ident = NULL,
            .impl_args = NULL,
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
    // TODO 直接实现 parser int 转换为 int64
    if (parser_is_basic_type(m)) {
        token_t *type_token = parser_advance(m);
        result.kind = token_to_kind[type_token->type];
        result.value = NULL;
        result.impl_ident = type_kind_str[result.kind];
        if (type_token->type == TOKEN_INT || type_token->type == TOKEN_UINT || type_token->type == TOKEN_FLOAT) {
            result.origin_ident = type_token->literal;
            result.origin_type_kind = result.kind;
        }

        return result;
    }

    // ptr<type>
    if (parser_consume(m, TOKEN_POINTER)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_pointer_t *type_pointer = NEW(type_pointer_t);
        type_pointer->value_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);

        result.kind = TYPE_PTR;
        result.pointer = type_pointer;
        return result;
    }

    // [int] a = []
    if (parser_consume(m, TOKEN_LEFT_SQUARE)) {
        type_vec_t *type_vec = NEW(type_vec_t);
        type_vec->element_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_SQUARE);
        result.kind = TYPE_VEC;
        result.vec = type_vec;
        return result;
    }

    // map<type,type>
    if (parser_consume(m, TOKEN_MAP)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_map_t *map = NEW(type_map_t);
        map->key_type = parser_type(m);
        parser_must(m, TOKEN_COMMA);
        map->value_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_MAP;
        result.map = map;
        return result;
    }

    // set<type>
    if (parser_consume(m, TOKEN_SET)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_set_t *set = NEW(type_set_t);
        set->element_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_SET;
        result.set = set;
        return result;
    }

    // tup<...>
    if (parser_consume(m, TOKEN_TUP)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_tuple_t *tuple = NEW(type_tuple_t);
        tuple->elements = ct_list_new(sizeof(type_t));
        do {
            type_t t = parser_type(m);
            ct_list_push(tuple->elements, &t);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_TUPLE;
        result.tuple = tuple;
        return result;
    }

    // vec<int>
    if (parser_consume(m, TOKEN_VEC)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_vec_t *type_vec = NEW(type_vec_t);
        type_vec->element_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_VEC;
        result.vec = type_vec;
        return result;
    }

    // arr<int>
    if (parser_consume(m, TOKEN_ARR)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_array_t *type_array = NEW(type_array_t);
        type_array->element_type = parser_type(m);
        parser_consume(m, TOKEN_COMMA);
        token_t *t = parser_must(m, TOKEN_LITERAL_INT);
        int length = atoi(t->literal);
        PARSER_ASSERTF(length > 0, "array len must > 0")
        type_array->length = length;
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_ARR;
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
            struct_property_t item = {.type = parser_type(m), .key = parser_advance(m)->literal};

            if (parser_consume(m, TOKEN_EQUAL)) {
                ast_expr_t *temp_expr = expr_new_ptr(m);
                *temp_expr = parser_expr(m);
                if (temp_expr->assert_type == AST_FNDEF) {
                    ast_fndef_t *fn = temp_expr->value;
                    PARSER_ASSERTF(fn->symbol_name == NULL, "fn defined in struct cannot contain name");
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

    // alias or param
    if (parser_is(m, TOKEN_IDENT)) {
        token_t *first = parser_advance(m);

        // --------------------------------------------param----------------------------------------------------------
        // type param1 快速处理, foo_t<param1, param1> {
        if (m->parser_type_params_table && table_exist(m->parser_type_params_table, first->literal)) {
            result.kind = TYPE_PARAM;
            result.param = type_param_new(first->literal);
            result.origin_ident = result.param->ident;
            result.origin_type_kind = TYPE_PARAM;
            return result;
        }

        // --------------------------------------------alias----------------------------------------------------------
        // foo.bar 形式的类型
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

        // alias<arg1, arg2> arg1 和 arg2 是实际类型, 而不是泛型
        if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
            // parser actual params
            result.alias->args = ct_list_new(sizeof(type_t));
            do {
                type_t item = parser_single_type(m);
                ct_list_push(result.alias->args, &item);
            } while (parser_consume(m, TOKEN_COMMA));

            parser_must(m, TOKEN_RIGHT_ANGLE);
        }

        return result;
    }

    PARSER_ASSERTF(false, "ident '%s' is not a type", parser_peek(m)->literal);
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
    parser_must(m, TOKEN_TYPE);                                   // code
    type_alias_stmt->ident = parser_must(m, TOKEN_IDENT)->literal;// ident

    // <arg1, arg2>
    if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_RIGHT_ANGLE), "type alias params cannot empty");

        type_alias_stmt->params = ct_list_new(sizeof(ast_ident));

        // 放在 module 全局表中用于辅助 parser
        m->parser_type_params_table = table_new();

        do {
            token_t *ident = parser_advance(m);
            ast_ident *temp = ast_new_ident(ident->literal);
            ct_list_push(type_alias_stmt->params, temp);

            table_set(m->parser_type_params_table, ident->literal, ident->literal);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_consume(m, TOKEN_RIGHT_ANGLE);
    }

    parser_must(m, TOKEN_EQUAL);// =
    result->assert_type = AST_STMT_TYPE_ALIAS;
    result->value = type_alias_stmt;

    type_alias_stmt->type = parser_type(m);
    m->parser_type_params_table = NULL;// 右值解析完成后需要及时清空
    return result;
}

/**
 * @param m
 * @param left_expr
 * @param generics_args nullable
 * @return
 */
static type_t ast_expr_to_type_alias(module_t *m, ast_expr_t left, list_t *generics_args) {
    type_t t = {
            .status = REDUCTION_STATUS_UNDO,
            .line = parser_peek(m)->line,
            .column = parser_peek(m)->column,
            .origin_ident = NULL,
            .origin_type_kind = 0,
            .impl_ident = NULL,
            .kind = TYPE_ALIAS,
    };

    // 重新整理一下左值，整理成 type_alias_t
    if (left.assert_type == AST_EXPR_IDENT) {
        ast_ident *ident = left.value;
        t.alias = type_alias_new(ident->literal, NULL);
        t.origin_ident = ident->literal;
        t.origin_type_kind = TYPE_ALIAS;
    } else if (left.assert_type == AST_EXPR_SELECT) {
        ast_select_t *select = left.value;
        assert(select->left.assert_type == AST_EXPR_IDENT);
        ast_ident *select_left = select->left.value;
        t.alias = type_alias_new(select->key, select_left->literal);
        t.origin_ident = select->key;
        t.origin_type_kind = TYPE_ALIAS;
    } else {
        assertf(false, "struct new left type exception");
    }
    t.alias->args = generics_args;

    return t;
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

static void parser_params(module_t *m, ast_fndef_t *fn_decl) {
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

    binary_expr->operator= token_to_ast_op[operator_token->type];
    binary_expr->left = left;
    binary_expr->right = right;

    result.assert_type = AST_EXPR_BINARY;
    result.value = binary_expr;

    return result;
}

static bool is_typical_type_arg_expr(module_t *m) {
    if (parser_is_basic_type(m)) {
        return true;
    }

    // ptr v
    if (parser_is(m, TOKEN_POINTER)) {
        return true;
    }

    if (parser_is(m, TOKEN_ARR) || parser_is(m, TOKEN_MAP) || parser_is(m, TOKEN_TUP) || parser_is(m, TOKEN_VEC) ||
        parser_is(m, TOKEN_SET)) {
        return true;
    }

    if (parser_is(m, TOKEN_FN) && parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
        return true;
    }

    return false;
}

static bool parser_left_angle_is_type_args(module_t *m, ast_expr_t left) {
    linked_node *temp = m->p_cursor.current;
    linked_node *current = temp;

    token_t *t = current->value;
    assert(t->type == TOKEN_LEFT_ANGLE);

    // left angle 左侧值典型判断
    if (left.assert_type != AST_EXPR_IDENT && left.assert_type != AST_EXPR_SELECT) {
        return false;
    }
    if (left.assert_type == AST_EXPR_SELECT) {
        // select must ident . ident
        ast_select_t *select = left.value;
        if (select->left.assert_type != AST_EXPR_IDENT) {
            return false;
        }
    }

#ifdef DEBUG_PARSER
    printf("\t@@\t");
    fflush(stdout);
#endif

    // 跳过 <
    parser_advance(m);

    // 屏蔽错误
    m->intercept_errors = slice_new();

    parser_type(m);

    bool result = false;
    // 无法无法 parser 为类型则判定为非类型参数
    if (m->intercept_errors->count > 0) {
        result = false;
        goto RET;
    }

    // a < b + 1， 此时 b 也可以被 parser 为类型, 所以需要判定下一个符号是否为典型符号
    // a < T,
    // a < T>
    if (parser_is(m, TOKEN_RIGHT_ANGLE)) {
        result = true;
        goto RET;
    }

    // 更多歧义场景检测，检测直到遇到 RIGHT_ANGLE
    if (parser_consume(m, TOKEN_COMMA)) {
        do {
            parser_type(m);
        } while (parser_consume(m, TOKEN_COMMA));

        // 判断是否存在错误
        if (m->intercept_errors->count > 0) {
            result = false;
            goto RET;
        }

        if (!parser_is(m, TOKEN_RIGHT_ANGLE)) {
            result = false;
            goto RET;
        }

        if (!parser_next_is(m, 1, TOKEN_LEFT_CURLY) && !parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
            result = false;
            goto RET;
        }

        // <...>{ or <...>(
        result = true;
        goto RET;
    }


RET:
    m->intercept_errors = NULL;
    m->p_cursor.current = temp;
#ifdef DEBUG_PARSER
    printf("\t@@\t");
    fflush(stdout);
#endif
    return result;
}

/**
 * car<i8, i16>{}
 * car<i8, i16>()
 * foo.bar<i8,i16>{}
 * foo.car<i8,i16>()
 * [foo.car < a, foo.car > c]
 * @param m
 * @param left
 * @return
 */
static ast_expr_t parser_type_args_expr(module_t *m, ast_expr_t left) {
    assert(parser_peek(m)->type == TOKEN_LEFT_ANGLE);

    ast_expr_t result = expr_new(m);

    // 其实很好识别 left may ast_select_t  or ast_ident
    list_t *generics_args = ct_list_new(sizeof(type_t));
    // call<i8,i16>()
    if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
        do {
            type_t t = parser_type(m);
            ct_list_push(generics_args, &t);
        } while (parser_consume(m, TOKEN_COMMA));
        parser_must(m, TOKEN_RIGHT_ANGLE);
    }

    // 判断下一个符号
    if (parser_is(m, TOKEN_LEFT_PAREN)) {
        ast_call_t *call = NEW(ast_call_t);
        call->left = left;
        call->generics_args = generics_args;
        call->args = parser_arg(m, call);

        result.assert_type = AST_CALL;
        result.value = call;
        return result;
    }

    assert(parser_is(m, TOKEN_LEFT_CURLY));
    type_t t = ast_expr_to_type_alias(m, left, generics_args);
    return parser_struct_new(m, t);
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
    if (operator_token->type == TOKEN_NOT) {// !true
        unary_expr->operator= AST_OP_NOT;
    } else if (operator_token->type == TOKEN_MINUS) {// -2
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

        unary_expr->operator= AST_OP_NEG;
    } else if (operator_token->type == TOKEN_TILDE) {// ~0b2
        unary_expr->operator= AST_OP_BNOT;
    } else if (operator_token->type == TOKEN_AND) {// &a
        unary_expr->operator= AST_OP_LA;
    } else if (operator_token->type == TOKEN_STAR) {// *a
        unary_expr->operator= AST_OP_IA;
    } else {
        PARSER_ASSERTF(false, "unknown unary operator '%d'", token_str[operator_token->type]);
    }

    ast_expr_t operand = parser_precedence_expr(m, PRECEDENCE_UNARY);
    unary_expr->operand = operand;

    result.assert_type = AST_EXPR_UNARY;
    result.value = unary_expr;

    return result;
}

/**
 * expr catch err {
 * }
 * @param m
 * @param left
 * @return
 */
static ast_expr_t parser_catch_expr(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_CATCH);
    ast_catch_t *catch_expr = NEW(ast_catch_t);
    catch_expr->try_expr = left;

    token_t *error_ident = parser_advance(m);
    PARSER_ASSERTF(error_ident->type == TOKEN_IDENT, "parser variable definitions error, '%s' not a ident",
                   error_ident->literal);

    catch_expr->catch_err.ident = error_ident->literal;
    catch_expr->catch_err.type = type_kind_new(TYPE_UNKNOWN);// 实际上就是 errort

    catch_expr->catch_body = parser_body(m);

    result.assert_type = AST_CATCH;
    result.value = catch_expr;
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
    is_expr->src = left;
    result.assert_type = AST_EXPR_IS;
    result.value = is_expr;
    return result;
}

/**
 * 区分
 * 普通表达式 (1 + 1)
 * tuple (1, true, false)
 */
static ast_expr_t parser_left_paren_expr(module_t *m) {
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_expr_t expr = parser_expr(m);// 算术表达式中的 ()
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
    literal_expr->value = literal_token->literal;// 具体数值

    result.assert_type = AST_EXPR_LITERAL;
    result.value = literal_expr;

    return result;
}

static bool parser_is_tuple_typedecl(module_t *m, linked_node *current) {
    token_t *t = current->value;
    PARSER_ASSERTF(t->type == TOKEN_LEFT_PAREN, "tuple type decl start left param");

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
    select->key = property_token->literal;// struct 的 property 不能是运行时计算的结果，必须是具体的值

    result.assert_type = AST_EXPR_SELECT;
    result.value = select;

    return result;
}

static list_t *parser_arg(module_t *m, ast_call_t *call) {
    parser_must(m, TOKEN_LEFT_PAREN);
    list_t *args = ct_list_new(sizeof(ast_expr_t));

    // 无调用参数
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        return args;
    }

    do {
        if (parser_consume(m, TOKEN_ELLIPSIS)) {
            call->spread = true;
        }

        ast_expr_t expr = parser_expr(m);
        ct_list_push(args, &expr);

        if (call->spread) {
            PARSER_ASSERTF(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
        }

    } while (parser_consume(m, TOKEN_COMMA));

    parser_must(m, TOKEN_RIGHT_PAREN);

    return args;
}

static ast_expr_t parser_call_expr(module_t *m, ast_expr_t left_expr) {
    ast_expr_t result = expr_new(m);

    ast_call_t *call_stmt = NEW(ast_call_t);
    call_stmt->left = left_expr;

    // param handle
    call_stmt->args = parser_arg(m, call_stmt);

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
    if_stmt->condition = parser_expr_with_precedence(m);
    if_stmt->consequent = parser_body(m);

    if (parser_consume(m, TOKEN_ELSE)) {
        if (parser_is(m, TOKEN_IF)) {
            if_stmt->alternate = parser_else_if(m);
        } else {
            if_stmt->alternate = parser_body(m);
        }
    }

    result->assert_type = AST_STMT_IF;
    result->value = if_stmt;

    return result;
}

static bool prev_token_is_type(token_t *prev) {
    return prev->type == TOKEN_LEFT_PAREN || prev->type == TOKEN_LEFT_CURLY || prev->type == TOKEN_COLON ||
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
    if (parser_is_basic_type(m)) {
        return true;
    }

    if (parser_is(m, TOKEN_ANY)) {
        return true;
    }

    if (parser_is(m, TOKEN_LEFT_CURLY) || // {int}/{int:int}
        parser_is(m, TOKEN_LEFT_SQUARE)) {// [int]
        return true;
    }

    if (parser_is(m, TOKEN_POINTER)) {
        return true;
    }

    if (parser_is(m, TOKEN_ARR) || parser_is(m, TOKEN_MAP) || parser_is(m, TOKEN_TUP) || parser_is(m, TOKEN_VEC) ||
        parser_is(m, TOKEN_SET)) {
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
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_IDENT)) {
        return true;
    }

    // person|i8 a
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_OR)) {
        return true;
    }

    // package.ident|i8 foo = xxx
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_OR)) {
        return true;
    }

    // person<[i8]> foo
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_ANGLE)) {
        return true;
    }

    // person.foo<[i8]>
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
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
    // parser_must(m, TOKEN_LEFT_PAREN);

    // 通过找 ; 号的形式判断, 必须要有两个 ; 才会是 tradition
    // for int i = 1; i <= 10; i+=1
    if (is_for_tradition_stmt(m)) {
        ast_for_tradition_stmt_t *for_tradition_stmt = NEW(ast_for_iterator_stmt_t);
        for_tradition_stmt->init = parser_stmt(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->cond = parser_expr_with_precedence(m);
        parser_must(m, TOKEN_SEMICOLON);
        for_tradition_stmt->update = parser_stmt(m);

        for_tradition_stmt->body = parser_body(m);

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
        for_iterator_stmt->iterate = parser_precedence_expr(m, PRECEDENCE_TYPE_CAST);

        for_iterator_stmt->body = parser_body(m);

        result->assert_type = AST_STMT_FOR_ITERATOR;
        result->value = for_iterator_stmt;
        return result;
    }

    // for (condition) {}
    ast_for_cond_stmt_t *for_cond = NEW(ast_for_cond_stmt_t);
    for_cond->condition = parser_expr_with_precedence(m);
    // parser_must(m, TOKEN_RIGHT_PAREN);
    for_cond->body = parser_body(m);
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
    // 可以跳过 struct new/ golang 等表达式, 如果需要使用相关表达式，需要使用括号包裹
    binary_expr->right = parser_expr_with_precedence(m);
    binary_expr->operator= token_to_ast_op[t->type];
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

    if (left.assert_type == AST_CATCH) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_EQUAL), "catch cannot assign");
        PARSER_ASSERTF(!parser_is(m, TOKEN_CATCH), "catch cannot immediately next catch");

        ast_stmt_t *stmt = stmt_new(m);
        stmt->assert_type = AST_CATCH;
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

    result->value = NEW(ast_break_t);
    result->assert_type = AST_STMT_BREAK;
    return result;
}

static ast_stmt_t *parser_continue_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_CONTINUE);

    ast_continue_t *c = NEW(ast_continue_t);

    // return } 或者 ;
    c->expr = NULL;
    if (!parser_is(m, TOKEN_EOF) && !parser_is(m, TOKEN_STMT_EOF) && !parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_expr_t temp = parser_expr(m);

        c->expr = expr_new_ptr(m);
        memcpy(c->expr, &temp, sizeof(ast_expr_t));
    }

    result->value = c;
    result->assert_type = AST_STMT_CONTINUE;
    return result;
}

static ast_stmt_t *parser_return_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_advance(m);
    ast_return_stmt_t *stmt = NEW(ast_return_stmt_t);

    // return } 或者 ;
    stmt->expr = NULL;
    if (!parser_is(m, TOKEN_EOF) && !parser_is(m, TOKEN_STMT_EOF) && !parser_is(m, TOKEN_RIGHT_CURLY)) {
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
    ast_import_t *stmt = NEW(ast_import_t);
    stmt->ast_package = slice_new();

    token_t *token = parser_advance(m);
    if (token->type == TOKEN_LITERAL_STRING) {
        stmt->file = token->literal;
    } else {
        PARSER_ASSERTF(token->type == TOKEN_IDENT, "import token must string");
        slice_push(stmt->ast_package, token->literal);
        while (parser_consume(m, TOKEN_DOT)) {
            token = parser_must(m, TOKEN_IDENT);
            slice_push(stmt->ast_package, token->literal);
        }
    }

    if (parser_consume(m, TOKEN_AS)) {// 可选 as
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
    ast_vec_new_t *list_new = NEW(ast_vec_new_t);
    list_new->elements = ct_list_new(sizeof(ast_expr_t));
    parser_must(m, TOKEN_LEFT_SQUARE);

    if (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        do {
            ast_expr_t expr = parser_expr(m);
            ct_list_push(list_new->elements, &expr);
        } while (parser_consume(m, TOKEN_COMMA));
    }
    parser_must(m, TOKEN_RIGHT_SQUARE);

    result.assert_type = AST_EXPR_VEC_NEW;
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
    ast_fndef_t *fndef = ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column);
    parser_must(m, TOKEN_FN);

    if (parser_is(m, TOKEN_IDENT)) {
        token_t *name_token = parser_advance(m);
        fndef->symbol_name = name_token->literal;
        fndef->fn_name = fndef->symbol_name;
    }

    parser_params(m, fndef);

    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);
    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
    }

    fndef->body = parser_body(m);
    result.assert_type = AST_FNDEF;
    result.value = fndef;

    // fn(){}()
    if (parser_is(m, TOKEN_LEFT_PAREN)) {
        ast_call_t *call = NEW(ast_call_t);
        call->left = result;
        call->args = parser_arg(m, call);


        ast_expr_t closure_call_expr = expr_new(m);
        closure_call_expr.assert_type = AST_CALL;
        closure_call_expr.value = call;
        return closure_call_expr;
    }

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
}


static bool parser_is_impl_fn(module_t *m) {
    if (parser_is_basic_type(m)) {
        return true;
    }

    if (parser_is(m, TOKEN_VEC) || parser_is(m, TOKEN_MAP) || parser_is(m, TOKEN_SET)) {
        return true;
    }

    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT)) {
        return true;
    }

    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_PAREN)) {
        return false;
    }

    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_LEFT_ANGLE)) {
        int close = 1;

        // TODO 不允许换行
        linked_node *current = m->p_cursor.current->succ;
        token_t *t = current->value;
        int line = t->line;
        while (t->type != TOKEN_EOF && t->type != TOKEN_STMT_EOF && t->line == line) {
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
            return false;// 无法识别
        }

        token_t *next = current->succ->value;
        if (next->type == TOKEN_DOT) {
            return true;
        }

        if (next->type == TOKEN_LEFT_PAREN) {
            return false;
        }
    }


    return false;// 无法识别
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
    ast_fndef_t *fndef = ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column);
    result->assert_type = AST_FNDEF;
    result->value = fndef;

    parser_must(m, TOKEN_FN);

    // 整成类型中是没有 . 吧？就不能有.呀， fndef param?params 是什么 特殊 alias？

    // 第一个绝对是 ident, 第二个如果是 . 或者 < 则说明是类型扩展
    bool is_impl_type = false;
    if (parser_is_impl_fn(m)) {
        is_impl_type = true;
        linked_node *temp_current = m->p_cursor.current;// 回溯点

        token_t *first_token = parser_advance(m);

        // 记录泛型参数，用于 parser type 时可以正确解析
        if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
            m->parser_type_params_table = table_new();
            fndef->generics_params = ct_list_new(sizeof(ast_ident));
            do {
                token_t *ident = parser_advance(m);
                ast_ident *temp = ast_new_ident(ident->literal);
                ct_list_push(fndef->generics_params, temp);
                table_set(m->parser_type_params_table, ident->literal, ident->literal);
            } while (parser_consume(m, TOKEN_COMMA));
            parser_consume(m, TOKEN_RIGHT_ANGLE);
        }

        m->p_cursor.current = temp_current;

        // 解析完整类型是为了让 self 类型可以验证
        type_t impl_type = {0};
        if (first_token->type == TOKEN_IDENT) {
            impl_type.kind = TYPE_ALIAS;
            impl_type.impl_ident = parser_must(m, TOKEN_IDENT)->literal;
            impl_type.alias = type_alias_new(first_token->literal, NULL);

            if (fndef->generics_params) {// parser sign type
                parser_must(m, TOKEN_LEFT_ANGLE);
                impl_type.alias->args = ct_list_new(sizeof(type_t));
                do {
                    type_t param_type = parser_single_type(m);
                    assert(param_type.kind == TYPE_PARAM);
                    ct_list_push(impl_type.alias->args, &param_type);
                } while (parser_consume(m, TOKEN_COMMA));
                parser_must(m, TOKEN_RIGHT_ANGLE);
            }
        } else {
            // table 就绪的情况下可以正确的解析 param
            impl_type = parser_single_type(m);
            impl_type.impl_ident = first_token->literal;
        }
        impl_type.line = first_token->line;
        impl_type.column = first_token->column;


        // 类型检测
        PARSER_ASSERTF(parser_is_impl_type(impl_type.kind), "type '%s' cannot impl fn", type_kind_str[impl_type.kind]);

        fndef->impl_type = impl_type;

        parser_must(m, TOKEN_DOT);
    }

    token_t *token_ident = parser_must(m, TOKEN_IDENT);
    fndef->symbol_name = token_ident->literal;
    fndef->fn_name = token_ident->literal;

    if (!is_impl_type && parser_consume(m, TOKEN_LEFT_ANGLE)) {
        m->parser_type_params_table = table_new();
        fndef->generics_params = ct_list_new(sizeof(ast_ident));
        do {
            token_t *ident = parser_advance(m);
            ast_ident *temp = ast_new_ident(ident->literal);
            ct_list_push(fndef->generics_params, temp);
            table_set(m->parser_type_params_table, ident->literal, ident->literal);
        } while (parser_consume(m, TOKEN_COMMA));
        parser_consume(m, TOKEN_RIGHT_ANGLE);
    }

    parser_params(m, fndef);

    // 可选返回参数
    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);
    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
    }

    if (m->type == MODULE_TYPE_TPL) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_LEFT_CURLY), "temp module not support fn body");

        return result;
    }

    fndef->body = parser_body(m);

    m->parser_type_params_table = NULL;

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
    } else if (parser_is(m, TOKEN_GO)) {
        ast_expr_t expr = parser_go_expr(m);
        return stmt_expr_fake_new(m, expr);
    } else if (parser_is(m, TOKEN_MACRO_IDENT)) {
        ast_expr_t expr = parser_macro_call(m);
        return stmt_expr_fake_new(m, expr);
    }

    PARSER_ASSERTF(false, "cannot parser stmt with = '%s'", parser_peek(m)->literal);
    exit(1);
}

/**
 * template 文件只能包含 type 和 fn 两种表达式
 * @param m
 * @return
 */
static ast_stmt_t *parser_tpl_stmt(module_t *m) {
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
        [TOKEN_LESS_THAN] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_type_args_expr, PRECEDENCE_CALL},
        [TOKEN_MACRO_IDENT] = {parser_macro_call, NULL, PRECEDENCE_NULL},
        [TOKEN_DOT] = {NULL, parser_select, PRECEDENCE_CALL},
        [TOKEN_MINUS] = {parser_unary, parser_binary, PRECEDENCE_TERM},
        [TOKEN_PLUS] = {NULL, parser_binary, PRECEDENCE_TERM},
        [TOKEN_NOT] = {parser_unary, NULL, PRECEDENCE_UNARY},
        [TOKEN_TILDE] = {parser_unary, NULL, PRECEDENCE_UNARY},
        [TOKEN_AND] = {parser_unary, parser_binary, PRECEDENCE_AND},
        [TOKEN_OR] = {NULL, parser_binary, PRECEDENCE_OR},
        [TOKEN_XOR] = {NULL, parser_binary, PRECEDENCE_XOR},
        [TOKEN_LEFT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
        [TOKEN_PERSON] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_STAR] = {parser_unary, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_SLASH] = {NULL, parser_binary, PRECEDENCE_FACTOR},
        [TOKEN_OR_OR] = {NULL, parser_binary, PRECEDENCE_OR_OR},
        [TOKEN_AND_AND] = {NULL, parser_binary, PRECEDENCE_AND_AND},
        [TOKEN_NOT_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},
        [TOKEN_EQUAL_EQUAL] = {NULL, parser_binary, PRECEDENCE_CMP_EQUAL},

        [TOKEN_RIGHT_SHIFT] = {NULL, parser_binary, PRECEDENCE_SHIFT},
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
        [TOKEN_CATCH] = {NULL, parser_catch_expr, PRECEDENCE_CATCH},

        // 以 ident 开头的前缀表达式
        [TOKEN_IDENT] = {parser_ident_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_EOF] = {NULL, NULL, PRECEDENCE_NULL},
};

/**
 * @param type
 * @return
 */
static parser_rule *find_rule(token_type_t token_type) {
    return &rules[token_type];
}

static token_type_t parser_infix_token(module_t *m, ast_expr_t expr) {
    token_t *infix_token = parser_peek(m);

    // 歧义类型特殊处理
    if (infix_token->type == TOKEN_LEFT_ANGLE && !parser_left_angle_is_type_args(m, expr)) {
        infix_token->type = TOKEN_LESS_THAN;
    }

    // 如果是连续两个 >> , 则合并起来
    // infix token 合并
    if (infix_token->type == TOKEN_RIGHT_ANGLE && parser_next_is(m, 1, TOKEN_RIGHT_ANGLE)) {
        parser_advance(m);
        infix_token = parser_peek(m);
        infix_token->literal = ">>";
        infix_token->type = TOKEN_RIGHT_SHIFT;// 类型改写
    }

    return infix_token->type;
}

static ast_expr_t parser_precedence_expr(module_t *m, parser_precedence precedence) {
    // 读取表达式前缀
    parser_prefix_fn prefix_fn = find_rule(parser_peek(m)->type)->prefix;

    PARSER_ASSERTF(prefix_fn, "cannot parser ident '%s' type '%s'", parser_peek(m)->literal,
                   token_str[parser_peek(m)->type]);

    ast_expr_t expr = prefix_fn(m);

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则按表达式优先级进行处理, 如果 +/-/*// /. /[]  等
    parser_rule *infix_rule = find_rule(parser_infix_token(m, expr));
    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;

        expr = infix_fn(m, expr);

        infix_rule = find_rule(parser_infix_token(m, expr));
    }

    return expr;
}

/**
 * foo < a.b, b<c.a>> {
 * 从 < 符号开始匹配，一旦匹配到闭合符合则，且闭合符合的下一个符号是 {, 则说明当前是一个 struct new, 只是携带了 param
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

    // foo.bar
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_LEFT_ANGLE)) {
        if (is_struct_param_new_prefix(parser_next(m, 3))) {
            return true;
        }
    }

    // vec<> vec 特殊处理
    if (parser_is(m, TOKEN_VEC)) {
        return true;
    }

    return false;
}

/**
 * 只有典型的不包含泛型的参数 { 会进来
 * @param m
 * @param left
 * @return
 */
static ast_expr_t parser_struct_new_expr(module_t *m) {
    type_t t = parser_type(m);
    return parser_struct_new(m, t);
}

/**
 * @param m
 * @return
 */
static ast_expr_t parser_struct_new(module_t *m, type_t t) {
    ast_expr_t result = expr_new(m);
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


static ast_expr_t parser_macro_sizeof(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_macro_sizeof_expr_t *is_expr = NEW(ast_macro_sizeof_expr_t);
    is_expr->target_type = parser_single_type(m);
    parser_must(m, TOKEN_RIGHT_PAREN);
    result.assert_type = AST_MACRO_EXPR_SIZEOF;
    result.value = is_expr;
    return result;
}

static ast_expr_t parser_macro_reflect_hash(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_macro_reflect_hash_expr_t *expr = NEW(ast_macro_reflect_hash_expr_t);
    expr->target_type = parser_single_type(m);
    parser_must(m, TOKEN_RIGHT_PAREN);
    result.assert_type = AST_MACRO_EXPR_REFLECT_HASH;
    result.value = expr;
    return result;
}

static ast_expr_t parser_macro_type_eq(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_macro_type_eq_expr_t *expr = NEW(ast_macro_type_eq_expr_t);
    expr->left_type = parser_type(m);
    parser_must(m, TOKEN_COMMA);
    expr->right_type = parser_type(m);
    parser_must(m, TOKEN_RIGHT_PAREN);
    result.assert_type = AST_MACRO_EXPR_TYPE_EQ;
    result.value = expr;
    return result;
}

static ast_fndef_t *coroutine_fn_closure(module_t *m, ast_expr_t *call_expr) {
    ast_fndef_t *fndef = ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column);
    fndef->is_co_async = true;
    fndef->symbol_name = NULL;
    fndef->fn_name = NULL;
    fndef->params = ct_list_new(sizeof(ast_var_decl_t));
    fndef->return_type = type_kind_new(TYPE_VOID);

    slice_t *stmt_list = slice_new();

    // var a = call(x, x, x)
    ast_stmt_t *vardef_stmt = stmt_new(m);
    vardef_stmt->assert_type = AST_STMT_VARDEF;
    ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
    vardef->var_decl.type = type_kind_new(TYPE_UNKNOWN);
    vardef->var_decl.ident = FN_COROUTINE_RETURN_VAR;
    vardef->right = *ast_expr_copy(m, call_expr);
    vardef_stmt->value = vardef;

    // rt_coroutine_return(&result)
    ast_call_t *call = NEW(ast_call_t);
    call->left = *ast_ident_expr(fndef->line, fndef->column, BUILTIN_CALL_CO_RETURN);
    call->args = ct_list_new(sizeof(ast_expr_t));
    ast_expr_t *arg = expr_new_ptr(m);
    ast_unary_expr_t *unary = NEW(ast_unary_expr_t);
    unary->operand = *ast_ident_expr(fndef->line, fndef->column, FN_COROUTINE_RETURN_VAR);
    unary->operator= AST_OP_LA;
    arg->assert_type = AST_EXPR_UNARY;
    arg->value = unary;

    ct_list_push(call->args, arg);
    ast_stmt_t *call_stmt = stmt_new(m);
    call_stmt->assert_type = AST_CALL;
    call_stmt->value = call;

    slice_push(stmt_list, vardef_stmt);
    slice_push(stmt_list, call_stmt);

    fndef->body = stmt_list;

    return fndef;
}
static ast_fndef_t *coroutine_fn_void_closure(module_t *m, ast_expr_t *call_expr) {
    ast_fndef_t *fndef = ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column);
    fndef->is_co_async = true;
    fndef->symbol_name = NULL;
    fndef->fn_name = NULL;
    fndef->params = ct_list_new(sizeof(ast_var_decl_t));
    fndef->return_type = type_kind_new(TYPE_VOID);

    slice_t *stmt_list = slice_new();

    // call(x, x, x)
    ast_stmt_t *call_stmt = stmt_new(m);
    call_stmt->assert_type = AST_CALL;
    call_stmt->value = ast_expr_copy(m, call_expr)->value;
    slice_push(stmt_list, call_stmt);

    fndef->body = stmt_list;

    return fndef;
}

static ast_expr_t parser_go_expr(module_t *m) {
    parser_must(m, TOKEN_GO);

    ast_expr_t call_expr = parser_expr(m);

    // expr 的 type 必须是 call
    PARSER_ASSERTF(call_expr.assert_type == AST_CALL, "go expr must be call");

    ast_macro_co_async_t *go_expr = NEW(ast_macro_co_async_t);
    go_expr->origin_call = call_expr.value;
    go_expr->closure_fn = coroutine_fn_closure(m, &call_expr);
    go_expr->closure_fn_void = coroutine_fn_void_closure(m, &call_expr);
    go_expr->flag_expr = NULL;
    ast_expr_t result = expr_new(m);
    result.assert_type = AST_MACRO_CO_ASYNC;
    result.value = go_expr;
    return result;
}

/**
 * future<int> f = @co_async(sum(1, 2), co.THREAD)
 * @param m
 * @return
 */
static ast_expr_t parser_macro_co_async_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    ast_macro_co_async_t *co_async = NEW(ast_macro_co_async_t);

    parser_must(m, TOKEN_LEFT_PAREN);

    ast_expr_t call_expr = parser_expr(m);
    co_async->origin_call = call_expr.value;
    co_async->closure_fn = coroutine_fn_closure(m, &call_expr);
    co_async->closure_fn_void = coroutine_fn_void_closure(m, &call_expr);

    if (parser_consume(m, TOKEN_COMMA)) {
        co_async->flag_expr = NEW(ast_expr_t);
        *co_async->flag_expr = parser_expr(m);
    }
    parser_must(m, TOKEN_RIGHT_PAREN);

    result.assert_type = AST_MACRO_CO_ASYNC;
    result.value = co_async;
    return result;
}


/**
* 宏就解析成对应的 expr 好了，然后正常走 analyzer/infer/linear
* @param m
* @return
*/
static ast_expr_t parser_macro_call(module_t *m) {
    token_t *token = parser_must(m, TOKEN_MACRO_IDENT);

    // 默认宏选择
    if (str_equal(token->literal, MACRO_SIZEOF)) {
        return parser_macro_sizeof(m);
    }

    if (str_equal(token->literal, MACRO_REFLECT_HASH)) {
        return parser_macro_reflect_hash(m);
    }

    if (str_equal(token->literal, MACRO_CO_ASYNC)) {
        return parser_macro_co_async_expr(m);
    }

    //    if (str_equal(token->literal, MACRO_TYPE_EQ)) {
    //        return parser_macro_type_eq(m);
    //    }

    PARSER_ASSERTF(false, "macro '%s' not defined", token->literal);
}

/**
 * 表达式优先级处理方式
 * @return
 */
static ast_expr_t parser_expr(module_t *m) {
    PARSER_ASSERTF(m->type != MODULE_TYPE_TPL, "template file cannot contains expr");

    // struct new
    if (parser_is_struct_new_expr(m)) {
        return parser_struct_new_expr(m);
    }

    // go
    if (parser_is(m, TOKEN_GO)) {
        return parser_go_expr(m);
    }

    // fn def, 也能写到括号里面呀
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
        if (m->type == MODULE_TYPE_TPL) {
            stmt = parser_tpl_stmt(m);
        } else {
            stmt = parser_stmt(m);
        }

        slice_push(block_stmt, stmt);
        parser_must_stmt_end(m);
    }

    return block_stmt;
}

#include "parser.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <utils/custom_links.h>

#include "runtime/rt_linked.h"
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

static ast_expr_t parser_match_expr(module_t *m);

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

static token_t *parser_prev(module_t *m) {
    if (!m->p_cursor.current->prev) {
        return NULL;
    }
    return m->p_cursor.current->prev->value;
}

static bool parser_is(module_t *m, token_type_t expect) {
    token_t *t = m->p_cursor.current->value;
    return t->type == expect;
}

static bool parser_ident_is(module_t *m, char *expect) {
    token_t *t = m->p_cursor.current->value;
    if (t->type != TOKEN_IDENT) {
        return false;
    }
    return str_equal(t->literal, expect);
}

static bool parser_is_literal(module_t *m) {
    return parser_is(m, TOKEN_LITERAL_FLOAT) ||
           parser_is(m, TOKEN_LITERAL_INT) ||
           parser_is(m, TOKEN_LITERAL_FLOAT);
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

    PARSER_ASSERTF(t->type == expect, "expected '%s' found '%s'", token_str[expect], t->literal);

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


static bool parser_is_stmt_eof(module_t *m) {
    return parser_is(m, TOKEN_STMT_EOF) || parser_is(m, TOKEN_EOF);
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

    token_t *prev_token = parser_prev(m);

    // 这里应该选择上一个末尾表达式
    dump_errorf(m, CT_STAGE_PARSER, prev_token->line, prev_token->column,
                "excepted ';' or '}' at end of statement");
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

    parser_must(m, TOKEN_LEFT_CURLY); // 必须是
    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_stmt_t *stmt = parser_stmt(m);
        parser_must_stmt_end(m);

        slice_push(stmt_list, stmt);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);

    return stmt_list;
}

static inline type_fn_t *parser_type_fn(module_t *m) {
    type_fn_t *type_fn = NEW(type_fn_t);
    type_fn->param_types = ct_list_new(sizeof(type_t));

    parser_must(m, TOKEN_LEFT_PAREN);
    if (!parser_consume(m, TOKEN_RIGHT_PAREN)) {
        // 包含参数类型
        do {
            type_t t = parser_type(m);
            ct_list_push(type_fn->param_types, &t);

            // 可选的函数名称
            parser_consume(m, TOKEN_IDENT);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_PAREN);
    }

    if (parser_consume(m, TOKEN_COLON)) {
        type_fn->return_type = parser_type(m);
    } else {
        type_fn->return_type = type_kind_new(TYPE_VOID);
    }

    if (parser_consume(m, TOKEN_NOT)) {
        type_fn->is_errable = true;
    }

    return type_fn;
}

/**
 * example
 *
 * - i8
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
    if (!parser_is(m, TOKEN_OR) && !parser_is(m, TOKEN_QUESTION)) {
        return t;
    }

    // T?, T? 后面不在允许直接携带 |
    if (parser_consume(m, TOKEN_QUESTION)) {
        type_t union_type = {
                .status = REDUCTION_STATUS_UNDO,
                .kind = TYPE_UNION,
        };
        union_type.union_ = NEW(type_union_t);
        union_type.union_->elements = ct_list_new(sizeof(type_t));
        ct_list_push(union_type.union_->elements, &t);

        type_t null_type = type_kind_new(TYPE_NULL);
        ct_list_push(union_type.union_->elements, &null_type);

        union_type.union_->nullable = true;

        return union_type;
    }

    PARSER_ASSERTF(!parser_is(m, TOKEN_OR), "union type only be declared in type alias")

    return t;
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
            .ident = NULL,
            .ident_kind = 0,
            .args = NULL,
    };

    // any 特殊处理
    if (parser_consume(m, TOKEN_ANY)) {
        type_t union_type = {
                .status = REDUCTION_STATUS_DONE,
                .kind = TYPE_UNION,
        };
        union_type.union_ = NEW(type_union_t);
        union_type.union_->elements = ct_list_new(sizeof(type_t));
        union_type.union_->any = true;

        return union_type;
    }

    // int/float/bool/string/void/var/self
    if (parser_is_basic_type(m)) {
        token_t *type_token = parser_advance(m);
        result.kind = token_to_kind[type_token->type];
        result.value = NULL;

        if (is_impl_builtin_type(result.kind)) {
            result.ident = type_token->literal;
            result.ident_kind = TYPE_IDENT_BUILTIN;
            result.args = NULL;
        }


        result.status = REDUCTION_STATUS_DONE;

        return result;
    }

    // ptr<type>
    if (parser_consume(m, TOKEN_PTR)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_ptr_t *type_pointer = NEW(type_ptr_t);
        type_pointer->value_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);

        result.kind = TYPE_PTR;
        result.ptr = type_pointer;
        return result;
    }

    //  vec [int] a = []
    // arr [int;8] a = []
    if (parser_consume(m, TOKEN_LEFT_SQUARE)) {
        type_t element_type = parser_type(m);
        if (parser_consume(m, TOKEN_STMT_EOF)) {
            token_t *t = parser_must(m, TOKEN_LITERAL_INT);
            int length = atoi(t->literal);
            PARSER_ASSERTF(length > 0, "array len must > 0")

            type_array_t *type_arr = NEW(type_array_t);
            type_arr->element_type = element_type;
            type_arr->length = length;
            parser_must(m, TOKEN_RIGHT_SQUARE);
            result.kind = TYPE_ARR;
            result.array = type_arr;
            return result;
        } else {
            type_vec_t *type_vec = NEW(type_vec_t);
            type_vec->element_type = element_type;
            parser_must(m, TOKEN_RIGHT_SQUARE);
            result.kind = TYPE_VEC;
            result.vec = type_vec;
            return result;
        }
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

    // chan_t<int>
    if (parser_consume(m, TOKEN_CHAN)) {
        parser_must(m, TOKEN_LEFT_ANGLE);
        type_chan_t *type_chan = NEW(type_chan_t);
        type_chan->element_type = parser_type(m);
        parser_must(m, TOKEN_RIGHT_ANGLE);
        result.kind = TYPE_CHAN;
        result.chan = type_chan;
        return result;
    }

    // arr<int,i> replace to [int;i]
    //    if (parser_consume(m, TOKEN_ARR)) {
    //        parser_must(m, TOKEN_LEFT_ANGLE);
    //        type_array_t *type_array = NEW(type_array_t);
    //        type_array->element_type = parser_type(m);
    //        parser_consume(m, TOKEN_COMMA);
    //        token_t *t = parser_must(m, TOKEN_LITERAL_INT);
    //        int length = atoi(t->literal);
    //        PARSER_ASSERTF(length > 0, "array len must > 0")
    //        type_array->length = length;
    //        parser_must(m, TOKEN_RIGHT_ANGLE);
    //        result.kind = TYPE_ARR;
    //        result.array = type_array;
    //        return result;
    //    }

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
                assertf(temp_expr->assert_type != AST_FNDEF,
                        "struct field default value cannot be a function definition, Use field assignment with function identifier (e.g., 'f = fn_ident') instead.");

                item.right = temp_expr;
            }


            ct_list_push(type_struct->properties, &item);
            parser_must_stmt_end(m);
        }

        result.kind = TYPE_STRUCT;
        result.struct_ = type_struct;

        return result;
    }

    // fn(int, int):int!
    if (parser_consume(m, TOKEN_FN)) {
        type_fn_t *type_fn = parser_type_fn(m);
        result.kind = TYPE_FN;
        result.fn = type_fn;
        return result;
    }

    // alias or param
    if (parser_is(m, TOKEN_IDENT)) {
        token_t *first = parser_advance(m);

        // --------------------------------------------param----------------------------------------------------------
        // type param1 快速处理, foo_t<param1, param1> {
        if (m->parser_type_params_table && table_exist(m->parser_type_params_table, first->literal)) {
            result.kind = TYPE_IDENT;
            result.ident = first->literal;
            result.ident_kind = TYPE_IDENT_PARAM;
            return result;
        }

        // --------------------------------------------def or alias----------------------------------------------------------
        // foo.bar 形式的类型
        token_t *second = NULL;
        if (parser_consume(m, TOKEN_DOT)) {
            second = parser_advance(m);
        }

        result.kind = TYPE_IDENT;
        result.ident_kind = TYPE_IDENT_UNKNOWN; // 无法确定是 alias 还是 def 还是 interface
        if (second) {
            result.import_as = first->literal;
            result.ident = second->literal;
        } else {
            result.ident = first->literal;
        }

        // alias<arg1, arg2> arg1 和 arg2 是实际类型, 而不是泛型
        if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
            // parser actual params
            result.args = ct_list_new(sizeof(type_t));
            do {
                type_t item = parser_single_type(m);
                ct_list_push(result.args, &item);
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
static ast_stmt_t *parser_typedef_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    ast_typedef_stmt_t *typedef_stmt = NEW(ast_typedef_stmt_t);
    sc_map_init_sv(&typedef_stmt->method_table, 0, 0);

    parser_must(m, TOKEN_TYPE); // code
    typedef_stmt->ident = parser_must(m, TOKEN_IDENT)->literal; // ident

    // <param1, param2>
    if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
        PARSER_ASSERTF(!parser_is(m, TOKEN_RIGHT_ANGLE), "type alias params cannot empty");

        typedef_stmt->params = ct_list_new(sizeof(ast_generics_param_t));

        // 将泛型参数放在 module 全局表中用于辅助 parser
        m->parser_type_params_table = table_new();

        do {
            token_t *ident = parser_advance(m);
            ast_generics_param_t *temp = ast_generics_param_new(ident->line, ident->column, ident->literal);

            // 可选的泛型约束 <T:t1|t2, U:t1&t2>
            if (parser_consume(m, TOKEN_COLON)) {
                type_t t = parser_single_type(m);
                ct_list_push(temp->constraints.elements, &t);

                if (parser_is(m, TOKEN_OR)) {
                    temp->constraints.or = true;
                    while (parser_consume(m, TOKEN_OR)) {
                        t = parser_single_type(m);
                        ct_list_push(temp->constraints.elements, &t);
                    }
                } else if (parser_is(m, TOKEN_AND)) {
                    temp->constraints.and = true;
                    while (parser_consume(m, TOKEN_AND)) {
                        t = parser_single_type(m);
                        ct_list_push(temp->constraints.elements, &t);
                    }
                }

                // 只要包含类型约束， any 就是 false
                temp->constraints.any = false;
            }

            ct_list_push(typedef_stmt->params, temp);
            table_set(m->parser_type_params_table, ident->literal, ident->literal);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_ANGLE);
    }

    // impl interface, support generics args
    // type data_t: container<T> {
    // type data_t: container<int> {
    if (parser_consume(m, TOKEN_COLON)) {
        typedef_stmt->impl_interfaces = ct_list_new(sizeof(type_t));
        do {
            type_t interface_type = parser_type(m);
            interface_type.ident_kind = TYPE_IDENT_INTERFACE;
            ct_list_push(typedef_stmt->impl_interfaces, &interface_type);
        } while (parser_consume(m, TOKEN_COMMA));
    }

    parser_must(m, TOKEN_EQUAL); // =
    result->assert_type = AST_STMT_TYPEDEF;
    result->value = typedef_stmt;

    // struct / union 都在这里生命，并且暂时不再支持匿名 strut, 也就是 parser type 不需要处理 struct

    // is struct
    if (parser_consume(m, TOKEN_STRUCT)) {
        type_struct_t *type_struct = NEW(type_struct_t);
        type_struct->properties = ct_list_new(sizeof(struct_property_t));

        struct sc_map_s64 exists = {0};
        sc_map_init_s64(&exists, 0, 0); // value is type_fn_t

        parser_must(m, TOKEN_LEFT_CURLY);
        while (!parser_consume(m, TOKEN_RIGHT_CURLY)) {
            // default value
            struct_property_t item = {.type = parser_type(m), .key = parser_advance(m)->literal};

            if (sc_map_get_s64(&exists, item.key)) {
                PARSER_ASSERTF(false, "struct field name '%s' exists", item.key);
            }

            sc_map_put_s64(&exists, item.key, 1);

            if (parser_consume(m, TOKEN_EQUAL)) {
                ast_expr_t *temp_expr = expr_new_ptr(m);
                *temp_expr = parser_expr(m);
                PARSER_ASSERTF(temp_expr->assert_type != AST_FNDEF,
                               "struct field default value cannot be a function definition, use field assignment with fn identifier (e.g., 'f = fn_ident') instead.");

                item.right = temp_expr;
            }


            ct_list_push(type_struct->properties, &item);
            parser_must_stmt_end(m);
        }

        type_t t = {
                .status = REDUCTION_STATUS_UNDO,
                .line = parser_peek(m)->line,
                .column = parser_peek(m)->column,
                .ident = NULL,
                .args = NULL,
                .ident_kind = 0,
        };
        t.kind = TYPE_STRUCT;
        t.struct_ = type_struct;

        typedef_stmt->type_expr = t;

        // 后续不能直接接 ?|
        m->parser_type_params_table = NULL; // 右值解析完成后需要及时清空

        return result;
    }

    if (parser_consume(m, TOKEN_INTERFACE)) {
        type_interface_t *type_interface = NEW(type_interface_t);
        type_interface->elements = ct_list_new(sizeof(type_t));
        parser_must(m, TOKEN_LEFT_CURLY);

        struct sc_map_s64 exists = {0};
        sc_map_init_s64(&exists, 0, 0); // value is type_fn_t

        while (!parser_consume(m, TOKEN_RIGHT_CURLY)) {
            parser_must(m, TOKEN_FN);
            // ident
            char *fn_name = parser_must(m, TOKEN_IDENT)->literal;
            type_fn_t *type_fn = parser_type_fn(m);
            type_fn->fn_name = fn_name;

            if (sc_map_get_s64(&exists, fn_name)) {
                PARSER_ASSERTF(false, "interface method '%s' exists", fn_name);
            }

            sc_map_put_s64(&exists, fn_name, 1);

            type_t interface_item = {
                    .status = REDUCTION_STATUS_UNDO,
                    .line = parser_peek(m)->line,
                    .column = parser_peek(m)->column,
                    .ident = NULL,
                    .ident_kind = 0,
                    .args = NULL,
                    .kind = TYPE_FN,
                    .fn = type_fn,
            };

            ct_list_push(type_interface->elements, &interface_item);
            parser_must_stmt_end(m);
        }

        type_t t = {
                .status = REDUCTION_STATUS_UNDO,
                .line = parser_peek(m)->line,
                .column = parser_peek(m)->column,
                .ident = NULL,
                .ident_kind = 0,
                .args = NULL,
                .kind = TYPE_INTERFACE,
                .interface = type_interface,
        };

        typedef_stmt->type_expr = t;
        typedef_stmt->is_interface = true;
        m->parser_type_params_table = NULL; // 右值解析完成后需要及时清空

        return result;
    }

    type_t t = parser_single_type(m);

    if (parser_consume(m, TOKEN_QUESTION)) {
        type_t union_type = {
                .status = REDUCTION_STATUS_UNDO,
                .kind = TYPE_UNION,
        };
        union_type.union_ = NEW(type_union_t);
        union_type.union_->elements = ct_list_new(sizeof(type_t));
        ct_list_push(union_type.union_->elements, &t);

        type_t null_type = type_kind_new(TYPE_NULL);
        ct_list_push(union_type.union_->elements, &null_type);

        union_type.union_->nullable = true;

        PARSER_ASSERTF(!parser_is(m, TOKEN_OR), "union type declaration cannot use '?'");
        typedef_stmt->type_expr = union_type;

        // ! 或者 ? 后面不能再接任何类型
        m->parser_type_params_table = NULL; // 右值解析完成后需要及时清空

        return result;
    }

    if (parser_consume(m, TOKEN_OR)) {
        // union
        type_t union_type = {
                .status = REDUCTION_STATUS_UNDO,
                .kind = TYPE_UNION,
                .union_ = NEW(type_union_t)};
        union_type.union_->elements = ct_list_new(sizeof(type_t));
        ct_list_push(union_type.union_->elements, &t);

        // union type 处理
        do {
            t = parser_single_type(m);
            ct_list_push(union_type.union_->elements, &t);
        } while (parser_consume(m, TOKEN_OR));

        typedef_stmt->type_expr = union_type;
        m->parser_type_params_table = NULL; // 右值解析完成后需要及时清空
        return result;
    }

    // 一般类型
    typedef_stmt->type_expr = t;
    m->parser_type_params_table = NULL; // 右值解析完成后需要及时清空
    return result;
}

/**
 * @param m
 * @param left_expr
 * @param generics_args nullable
 * @return
 */
static type_t ast_expr_to_typedef_ident(module_t *m, ast_expr_t left, list_t *generics_args) {
    type_t t = {
            .status = REDUCTION_STATUS_UNDO,
            .line = parser_peek(m)->line,
            .column = parser_peek(m)->column,
            .ident = NULL,
            .ident_kind = TYPE_IDENT_DEF,
            .kind = TYPE_IDENT,
            .args = NULL,
    };

    // 重新整理一下左值，整理成 typedef_t
    if (left.assert_type == AST_EXPR_IDENT) {
        ast_ident *ident = left.value;
        t.ident = ident->literal;
    } else if (left.assert_type == AST_EXPR_SELECT) {
        ast_expr_select_t *select = left.value;
        assert(select->left.assert_type == AST_EXPR_IDENT);
        ast_ident *select_left = select->left.value;
        t.import_as = select_left->literal;
        t.ident = select->key;
    } else {
        assertf(false, "struct new left type exception");
    }

    t.args = generics_args;
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
    ast_expr_t right = parser_precedence_expr(m, precedence + 1, 0);

    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);

    binary_expr->op = token_to_ast_op[operator_token->type];
    binary_expr->left = left;
    binary_expr->right = right;

    result.assert_type = AST_EXPR_BINARY;
    result.value = binary_expr;

    return result;
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
        ast_expr_select_t *select = left.value;
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
    type_t t = ast_expr_to_typedef_ident(m, left, generics_args);
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
    if (operator_token->type == TOKEN_NOT) {
        // !true
        unary_expr->op = AST_OP_NOT;
    } else if (operator_token->type == TOKEN_MINUS) {
        // -2
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

        unary_expr->op = AST_OP_NEG;
    } else if (operator_token->type == TOKEN_TILDE) {
        // ~0b2
        unary_expr->op = AST_OP_BNOT;
    } else if (operator_token->type == TOKEN_AND) {
        // &a
        unary_expr->op = AST_OP_LA;
    } else if (operator_token->type == TOKEN_STAR) {
        // *a
        unary_expr->op = AST_OP_IA;
    } else {
        PARSER_ASSERTF(false, "unknown unary operator '%d'", token_str[operator_token->type]);
    }

    ast_expr_t operand = parser_precedence_expr(m, PRECEDENCE_UNARY, 0);
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
    catch_expr->catch_err.type = type_kind_new(TYPE_UNKNOWN); // 实际上就是 errort

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

static ast_expr_t parser_match_is_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_IS);

    PARSER_ASSERTF(m->parser_match_cond, "is type must be specified in the match expression")

    ast_match_is_expr_t *is_expr = NEW(ast_match_is_expr_t);

    is_expr->target_type = parser_single_type(m);

    result.assert_type = AST_EXPR_MATCH_IS;
    result.value = is_expr;
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
    ast_expr_t expr = parser_expr(m); // 算术表达式中的 ()
    if (parser_consume(m, TOKEN_RIGHT_PAREN)) {
        // ast 本身已经包含了 group 的含义，所以这里不需要特别再包一层 group 了
        return expr;
    }

    // tuple new
    parser_must(m, TOKEN_COMMA);

    ast_tuple_new_t *tuple = NEW(ast_tuple_new_t);
    tuple->elements = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(tuple->elements, &expr);


    do {
        expr = parser_expr(m);
        ct_list_push(tuple->elements, &expr);

        if (parser_is(m, TOKEN_RIGHT_PAREN)) {
            break;
        } else {
            parser_must(m, TOKEN_COMMA);
        }
    } while (!parser_is(m, TOKEN_RIGHT_PAREN));

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
    literal_expr->len = literal_token->length;

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
static ast_expr_t parser_unknown_select(module_t *m, ast_expr_t left) {
    ast_expr_t result = expr_new(m);

    parser_must(m, TOKEN_DOT);

    token_t *property_token = parser_must(m, TOKEN_IDENT);
    ast_expr_select_t *select = NEW(ast_expr_select_t);

    select->left = left;
    select->key = property_token->literal; // struct 的 property 不能是运行时计算的结果，必须是具体的值

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

    while (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        if (parser_consume(m, TOKEN_ELLIPSIS)) {
            call->spread = true;
        }

        ast_expr_t expr = parser_expr(m);
        ct_list_push(args, &expr);

        if (call->spread) {
            PARSER_ASSERTF(parser_is(m, TOKEN_RIGHT_PAREN), "can only use '...' as the final argument in the list");
        }

        // 结尾存在 ',' 或者 ) 可以避免换行符识别异常, 所以 parser 需要支持最后一个 TOKEN_COMMA 可选的情况
        if (parser_is(m, TOKEN_RIGHT_PAREN)) {
            break;
        } else {
            parser_must(m, TOKEN_COMMA);
        }
    }

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

        if (close == 0 && t->type == TOKEN_STMT_EOF) {
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
 * call generics 和 type generics 有一样的前缀，所以需要进行前瞻检测
 * person_t<t>()
 * vec<t>()
 * vec<t> a = xxx
 */
static bool is_call_generics(module_t *m) {
    linked_node *temp = m->p_cursor.current;
    linked_node *current = temp;

#ifdef DEBUG_PARSER
    printf("\t@@\t");
    fflush(stdout);
#endif

    // 屏蔽错误
    m->intercept_errors = slice_new();

    type_t t = parser_type(m);
    bool result = false;

    // 无法判定为类型，直接返回 true
    if (m->intercept_errors->count > 0) {
        result = true;
        goto RET;
    }

    // 判断下一个典型符号
    if (parser_is(m, TOKEN_LEFT_PAREN)) {
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
        parser_is(m, TOKEN_LEFT_SQUARE)) {
        // [int]
        return true;
    }

    if (parser_is(m, TOKEN_PTR)) {
        return true;
    }

    if (parser_is(m, TOKEN_ARR) || parser_is(m, TOKEN_MAP) || parser_is(m, TOKEN_TUP) || parser_is(m, TOKEN_VEC) ||
        parser_is(m, TOKEN_SET) || parser_is(m, TOKEN_CHAN)) {
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
        // 检测 type 然后检测下一个符号
        return !is_call_generics(m);
    }

    // person.foo<[i8]> or person.foo<[i8]>()
    if (parser_is(m, TOKEN_IDENT) && parser_next_is(m, 1, TOKEN_DOT) && parser_next_is(m, 2, TOKEN_IDENT) &&
        parser_next_is(m, 3, TOKEN_LEFT_ANGLE)) {
        // 检测 type 然后检测下一个符号，并进行回溯
        return !is_call_generics(m);
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
        parser_must(m, TOKEN_STMT_EOF);
        for_tradition_stmt->cond = parser_expr_with_precedence(m);
        parser_must(m, TOKEN_STMT_EOF);
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
        for_iterator_stmt->iterate = parser_precedence_expr(m, PRECEDENCE_TYPE_CAST, 0);

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
    PARSER_ASSERTF(token_complex_assign(t->type), "assign=%s token exception", token_str[t->type]);

    // 转换成逻辑运算符
    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);
    // 可以跳过 struct new/ golang 等表达式, 如果需要使用相关表达式，需要使用括号包裹
    binary_expr->right = parser_expr_with_precedence(m);
    binary_expr->op = token_to_ast_op[t->type];
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
static ast_stmt_t *parser_expr_begin_stmt(module_t *m) {
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

    PARSER_ASSERTF(!parser_is_stmt_eof(m), "expression incompleteness, ident need used");


    // 不是 call 那接下来一定就是 assign 了
    // foo = 1 、foo.bar = 1 、foo[1] = 1、foo().as = 1;
    return parser_assign(m, left);
}

static ast_stmt_t *parser_break_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_BREAK);

    ast_break_t *b = NEW(ast_break_t);

    // return } 或者 ;
    b->expr = NULL;
    if (!parser_is_stmt_eof(m) && !parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_expr_t temp = parser_expr(m);

        b->expr = expr_new_ptr(m);
        memcpy(b->expr, &temp, sizeof(ast_expr_t));
    }

    result->value = b;
    result->assert_type = AST_STMT_BREAK;
    return result;
}

/*select {
    ch.on_recv() -> msg { // recv 的 msg 同样是可选的。
        continue
    }
    ch.on_send(msg) -> {
        break
    }
    _ -> {
    }
}*/
static ast_stmt_t *parser_select_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_SELECT);
    parser_must(m, TOKEN_LEFT_CURLY);

    ast_select_stmt_t *select = NEW(ast_select_stmt_t);
    select->cases = slice_new();

    bool has_default = false;
    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        ast_select_case_t *select_case = NEW(ast_select_case_t);

        // default
        if (parser_is(m, TOKEN_IDENT) && str_equal(parser_peek(m)->literal, "_")) {
            parser_advance(m);
            parser_must(m, TOKEN_RIGHT_ARROW);
            select_case->recv_var = NULL;
            select_case->handle_body = NULL;
            select_case->is_default = true;
            select_case->handle_body = parser_body(m);
            slice_push(select->cases, select_case);
            parser_must_stmt_end(m);

            has_default = true;

            continue;
        }

        ast_expr_t expr = parser_expr_with_precedence(m);
        PARSER_ASSERTF(expr.assert_type == AST_CALL, "select case must be chan select call");
        ast_call_t *call = expr.value;
        PARSER_ASSERTF(call->left.assert_type == AST_EXPR_SELECT, "select case must be chan select call");
        ast_expr_select_t *call_select = call->left.value;
        if (str_equal(call_select->key, "on_recv")) {
            select->recv_count++;
            select_case->is_recv = true;
        } else if (str_equal(call_select->key, "on_send")) {
            select->send_count++;
            select_case->is_recv = false;
        } else {
            PARSER_ASSERTF(false, "only on_recv or on_send can be used in select case");
        }


        select_case->on_call = call;

        parser_must(m, TOKEN_RIGHT_ARROW); // ->

        // -> msg {}
        // -> {}
        if (parser_is(m, TOKEN_IDENT)) {
            select_case->recv_var = NEW(ast_var_decl_t);
            // 需要根据 iterator 的类型对 key 和 value type 进行类型判断
            select_case->recv_var->type = type_kind_new(TYPE_UNKNOWN);
            select_case->recv_var->ident = parser_must(m, TOKEN_IDENT)->literal;
        }

        select_case->handle_body = parser_body(m);

        slice_push(select->cases, select_case);
        parser_must_stmt_end(m);
    }
    parser_must(m, TOKEN_RIGHT_CURLY);

    select->has_default = has_default;

    if (select->has_default && select->cases->count == 1) {
        PARSER_ASSERTF(false, "select must contains on_call case");
    }

    result->assert_type = AST_STMT_SELECT;
    result->value = select;
    return result;
}

static ast_stmt_t *parser_try_catch_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_TRY);

    ast_try_catch_stmt_t *try_stmt = NEW(ast_try_catch_stmt_t);
    try_stmt->try_body = parser_body(m);

    parser_must(m, TOKEN_CATCH);

    token_t *error_ident = parser_must(m, TOKEN_IDENT);
    try_stmt->catch_err.ident = error_ident->literal;
    try_stmt->catch_err.type = type_kind_new(TYPE_UNKNOWN);

    try_stmt->catch_body = parser_body(m);

    result->assert_type = AST_STMT_TRY_CATCH;
    result->value = try_stmt;
    return result;
}

static ast_stmt_t *parser_continue_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_must(m, TOKEN_CONTINUE);

    result->value = NULL;
    result->assert_type = AST_STMT_CONTINUE;
    return result;
}

static ast_stmt_t *parser_return_stmt(module_t *m) {
    ast_stmt_t *result = stmt_new(m);
    parser_advance(m);
    ast_return_stmt_t *stmt = NEW(ast_return_stmt_t);

    // return } 或者 ;
    stmt->expr = NULL;
    if (!parser_is_stmt_eof(m) && !parser_is(m, TOKEN_RIGHT_CURLY)) {
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

    if (parser_consume(m, TOKEN_AS)) {
        // 可选 as
        token = parser_advance(m);
        PARSER_ASSERTF(token->type == TOKEN_IDENT || token->type == TOKEN_IMPORT_STAR, "import as must ident");
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
static ast_expr_t parser_left_square_expr(module_t *m) {
    ast_expr_t result = expr_new(m);

    parser_must(m, TOKEN_LEFT_SQUARE);

    if (parser_consume(m, TOKEN_RIGHT_SQUARE)) {
        ast_vec_new_t *vec_new = NEW(ast_vec_new_t);
        vec_new->elements = ct_list_new(sizeof(ast_expr_t));
        result.assert_type = AST_EXPR_VEC_NEW;
        result.value = vec_new;
        return result;
    }

    // parser first element
    ast_expr_t first_expr = parser_expr(m);
    if (parser_consume(m, TOKEN_STMT_EOF)) {
        ast_vec_repeat_new_t *vec_repeat_new = NEW(ast_vec_repeat_new_t);

        ast_expr_t length_expr = parser_expr(m);
        parser_must(m, TOKEN_RIGHT_SQUARE);

        vec_repeat_new->default_element = first_expr;
        vec_repeat_new->length_expr = length_expr;
        result.assert_type = AST_EXPR_VEC_REPEAT_NEW;
        result.value = vec_repeat_new;
        return result;
    }

    ast_vec_new_t *vec_new = NEW(ast_vec_new_t);
    vec_new->elements = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(vec_new->elements, &first_expr);

    if (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        parser_must(m, TOKEN_COMMA);
    }

    while (!parser_is(m, TOKEN_RIGHT_SQUARE)) {
        ast_expr_t expr = parser_expr(m);
        ct_list_push(vec_new->elements, &expr);

        if (parser_is(m, TOKEN_RIGHT_SQUARE)) {
            break;
        } else {
            parser_must(m, TOKEN_COMMA);
        }
    }

    parser_must(m, TOKEN_RIGHT_SQUARE);

    result.assert_type = AST_EXPR_VEC_NEW;
    result.value = vec_new;

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
    if (parser_consume(m, TOKEN_COLON)) { // k:v
        // map
        ast_map_new_t *map_new = NEW(ast_map_new_t);
        map_new->elements = ct_list_new(sizeof(ast_map_element_t));
        ast_map_element_t element = {.key = key_expr, .value = parser_expr(m)};
        ct_list_push(map_new->elements, &element);

        // 必须消耗掉一个逗号或者右大括号才能继续
        if (!parser_is(m, TOKEN_RIGHT_CURLY)) {
            parser_must(m, TOKEN_COMMA);
        }

        while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
            element.key = parser_expr(m);
            parser_must(m, TOKEN_COLON);
            element.value = parser_expr(m);
            ct_list_push(map_new->elements, &element);

            if (parser_is(m, TOKEN_RIGHT_CURLY)) {
                break;
            } else {
                parser_must(m, TOKEN_COMMA);
            }
        }

        // 跳过可能存在的 STMT_EOF, scanner 添加
        parser_consume(m, TOKEN_STMT_EOF);

        parser_must(m, TOKEN_RIGHT_CURLY);
        result.assert_type = AST_EXPR_MAP_NEW;
        result.value = map_new;
        return result;
    }

    // set
    ast_set_new_t *expr = NEW(ast_set_new_t);
    expr->elements = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(expr->elements, &key_expr);

    if (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        parser_must(m, TOKEN_COMMA);
    }

    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        key_expr = parser_expr(m);
        ct_list_push(expr->elements, &key_expr);

        if (parser_is(m, TOKEN_RIGHT_CURLY)) {
            break;
        } else {
            parser_must(m, TOKEN_COMMA);
        }
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
        PARSER_ASSERTF(false, "closure fn cannot have a name");
        token_t *name_token = parser_advance(m);
        fndef->symbol_name = name_token->literal;
        fndef->fn_name = fndef->symbol_name;
        fndef->fn_name_with_pkg = ident_with_prefix(m->ident, fndef->symbol_name);
    }

    parser_params(m, fndef);

    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);

    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
        fndef->return_type.line = parser_peek(m)->line;
        fndef->return_type.column = parser_peek(m)->column;
    }

    // has errable
    if (parser_consume(m, TOKEN_NOT)) {
        fndef->is_errable = true;
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
    parser_must(m, TOKEN_IDENT);
    ast_new_expr_t *new_expr = NEW(ast_new_expr_t);
    new_expr->type = parser_type(m);

    parser_must(m, TOKEN_LEFT_PAREN);
    if (!parser_is(m, TOKEN_RIGHT_PAREN)) {
        // has default value
        if (parser_is(m, TOKEN_IDENT) && (parser_next_is(m, 1, TOKEN_EQUAL) || parser_next_is(m, 1, TOKEN_COMMA))) {
            new_expr->properties = ct_list_new(sizeof(struct_property_t));

            // struct default value
            while (!parser_is(m, TOKEN_RIGHT_PAREN)) {
                struct_property_t item = {0};
                token_t *ident_token = parser_must(m, TOKEN_IDENT);
                item.key = ident_token->literal;
                if (parser_consume(m, TOKEN_EQUAL)) {
                    item.right = expr_new_ptr(m);
                    *((ast_expr_t *) item.right) = parser_expr(m);
                } else {
                    item.right = ast_ident_expr(ident_token->line, ident_token->column, item.key);
                }

                ct_list_push(new_expr->properties, &item);

                if (parser_is(m, TOKEN_RIGHT_PAREN)) {
                    break;
                } else {
                    parser_must(m, TOKEN_COMMA);
                }
            }
        } else {

            new_expr->default_expr = expr_new_ptr(m);
            *new_expr->default_expr = parser_expr(m);

            // copy to properties
            if (new_expr->default_expr->assert_type == AST_EXPR_IDENT) {
                new_expr->properties = ct_list_new(sizeof(struct_property_t));
                ast_ident *ident = new_expr->default_expr->value;
                struct_property_t item = {
                        .key = strdup(ident->literal),
                        .right = ast_ident_expr(parser_peek(m)->line, parser_peek(m)->column, strdup(ident->literal))};
                ct_list_push(new_expr->properties, &item);
            }
        }
    }

    parser_must(m, TOKEN_RIGHT_PAREN);

    result.assert_type = AST_EXPR_NEW;
    result.value = new_expr;
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
            PARSER_ASSERTF(can_assign(expr.assert_type), "tuple destr src operand assign failed");
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

    if (parser_is(m, TOKEN_CHAN)) {
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
            return false; // 无法识别
        }

        token_t *next = current->succ->value;
        if (next->type == TOKEN_DOT) {
            return true;
        }

        if (next->type == TOKEN_LEFT_PAREN) {
            return false;
        }
    }


    return false; // 无法识别
}


/**
 * // name 不可省略，暂时不支持匿名函数
 * fn name(int a, int b): int {
 * }
 * @param m
 * @return
 */
static ast_stmt_t *parser_fndef_stmt(module_t *m, ast_fndef_t *fndef) {
    ast_stmt_t *result = stmt_new(m);
    result->assert_type = AST_FNDEF;
    result->value = fndef;

    parser_must(m, TOKEN_FN);

    // 第一个绝对是 ident, 第二个如果是 . 或者 < 则说明是类型扩展
    bool is_impl_type = false;
    if (parser_is_impl_fn(m)) {
        is_impl_type = true;
        linked_node *temp_current = m->p_cursor.current; // 回溯点

        token_t *first_token = parser_advance(m);

        // 记录泛型参数，用于 parser type 时可以正确解析
        if (parser_consume(m, TOKEN_LEFT_ANGLE)) {
            m->parser_type_params_table = table_new();
            fndef->generics_params = ct_list_new(sizeof(ast_generics_param_t));
            do {
                token_t *ident = parser_advance(m);

                // 默认是 union any
                ast_generics_param_t *temp = ast_generics_param_new(ident->line, ident->column, ident->literal);

                // 可选的泛型约束 <T:t1|t2, U:t1&t2>
                if (parser_consume(m, TOKEN_COLON)) {
                    type_t t = parser_single_type(m);
                    ct_list_push(temp->constraints.elements, &t);

                    if (parser_is(m, TOKEN_OR)) {
                        temp->constraints.or = true;
                        while (parser_consume(m, TOKEN_OR)) {
                            t = parser_single_type(m);
                            ct_list_push(temp->constraints.elements, &t);
                        }
                    } else if (parser_is(m, TOKEN_AND)) {
                        temp->constraints.and = true;
                        while (parser_consume(m, TOKEN_AND)) {
                            t = parser_single_type(m);
                            ct_list_push(temp->constraints.elements, &t);
                        }
                    }

                    // 只要包含类型约束， any 就是 false
                    temp->constraints.any = false;
                }

                ct_list_push(fndef->generics_params, temp);
                table_set(m->parser_type_params_table, ident->literal, ident->literal);
            } while (parser_consume(m, TOKEN_COMMA));

            parser_must(m, TOKEN_RIGHT_ANGLE);
        }

        m->p_cursor.current = temp_current; // 已经发生了回溯，所以 first_token 等都不占用 token

        // 解析完整类型是为了让 self 类型可以验证 fn ...(foo<T, U> a, T b)...
        type_t impl_type = {0};
        if (first_token->type == TOKEN_IDENT) {
            impl_type.kind = TYPE_IDENT;
            impl_type.ident = parser_must(m, TOKEN_IDENT)->literal;
            impl_type.ident_kind = TYPE_IDENT_DEF;

            if (fndef->generics_params) {
                // parser sign type
                parser_must(m, TOKEN_LEFT_ANGLE);
                impl_type.args = ct_list_new(sizeof(type_t));
                do {
                    type_t param_type = parser_single_type(m);
                    assert(param_type.ident_kind == TYPE_IDENT_PARAM);

                    // 排除参数约束
                    if (parser_consume(m, TOKEN_COLON)) {
                        do {
                            parser_single_type(m);
                        } while (parser_consume(m, TOKEN_OR));
                    }

                    ct_list_push(impl_type.args, &param_type);
                } while (parser_consume(m, TOKEN_COMMA));

                parser_must(m, TOKEN_RIGHT_ANGLE);
            }
        } else {
            // first_token 是 type
            // table 就绪的情况下可以正确的解析 param
            impl_type = parser_single_type(m);
            impl_type.ident = first_token->literal;
            impl_type.ident_kind = TYPE_IDENT_BUILTIN;
            impl_type.args = NULL;
        }
        impl_type.line = first_token->line;
        impl_type.column = first_token->column;


        // 类型检测
        PARSER_ASSERTF(is_impl_builtin_type(impl_type.kind) || impl_type.kind == TYPE_IDENT, "type '%s' cannot impl fn", type_kind_str[impl_type.kind]);

        fndef->impl_type = impl_type;

        parser_must(m, TOKEN_DOT);
    }

    token_t *token_ident = parser_must(m, TOKEN_IDENT);
    fndef->symbol_name = token_ident->literal;
    fndef->fn_name = token_ident->literal;
    fndef->fn_name_with_pkg = ident_with_prefix(m->ident, token_ident->literal);

    if (!is_impl_type && parser_consume(m, TOKEN_LEFT_ANGLE)) {
        m->parser_type_params_table = table_new();
        fndef->generics_params = ct_list_new(sizeof(ast_generics_param_t));
        do {
            token_t *ident = parser_advance(m);
            ast_generics_param_t *temp = ast_generics_param_new(ident->line, ident->column, ident->literal);

            // 可选的泛型约束 <T:t1|t2, U:t1&t2>
            if (parser_consume(m, TOKEN_COLON)) {
                type_t t = parser_single_type(m);
                ct_list_push(temp->constraints.elements, &t);

                if (parser_is(m, TOKEN_OR)) {
                    temp->constraints.or = true;
                    while (parser_consume(m, TOKEN_OR)) {
                        t = parser_single_type(m);
                        ct_list_push(temp->constraints.elements, &t);
                    }
                } else if (parser_is(m, TOKEN_AND)) {
                    temp->constraints.and = true;
                    while (parser_consume(m, TOKEN_AND)) {
                        t = parser_single_type(m);
                        ct_list_push(temp->constraints.elements, &t);
                    }
                }

                // 只要包含类型约束， any 就是 false
                temp->constraints.any = false;
            }


            ct_list_push(fndef->generics_params, temp);
            table_set(m->parser_type_params_table, ident->literal, ident->literal);
        } while (parser_consume(m, TOKEN_COMMA));

        parser_must(m, TOKEN_RIGHT_ANGLE);
    }

    parser_params(m, fndef);

    // 可选返回参数
    bool has_return_type = false;
    if (parser_consume(m, TOKEN_COLON)) {
        fndef->return_type = parser_type(m);
        has_return_type = true;
    } else {
        fndef->return_type = type_kind_new(TYPE_VOID);
        fndef->return_type.line = parser_peek(m)->line;
        fndef->return_type.column = parser_peek(m)->column;
    }

    // has errable
    if (parser_consume(m, TOKEN_NOT)) {
        fndef->is_errable = true;
    }

    if (parser_is_stmt_eof(m)) {
        fndef->is_tpl = true;
        return result;
    }

    fndef->body = parser_body(m);

    m->parser_type_params_table = NULL;

    return result;
}

/**
 * 只能是在 module 中声明
 * @param m
 * @return
 */
static ast_stmt_t *parser_label(module_t *m) {
    ast_fndef_t *fndef = ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column);

    do {
        token_t *token = parser_must(m, TOKEN_LABEL);
        if (str_equal(token->literal, MACRO_LINKID)) {
            if (parser_is(m, TOKEN_IDENT)) {
                token_t *linkid_value = parser_must(m, TOKEN_IDENT);
                fndef->linkid = linkid_value->literal;
            } else {
                token_t *literal = parser_must(m, TOKEN_LITERAL_STRING);
                fndef->linkid = literal->literal;
            }
        } else if (str_equal(token->literal, MACRO_LOCAL)) {
            fndef->is_private = true;
        } else if (str_equal(token->literal, MACRO_RUNTIME_USE)) {
            parser_must(m, TOKEN_IDENT);
        } else {
            // 不认识的 label 不做处理, 直接跳过
            PARSER_ASSERTF(false, "unknown fn label '%s'", token->literal);
        }
    } while (parser_is(m, TOKEN_LABEL));

    parser_must(m, TOKEN_STMT_EOF);

    if (parser_is(m, TOKEN_TYPE)) {
        return parser_typedef_stmt(m);
    } else if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_stmt(m, fndef);
    } else {
        PARSER_ASSERTF(false, "the label can only be applied to type alias or fn.");
    }
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

static ast_stmt_t *parser_left_param_begin_stmt(module_t *m) {
    linked_node *temp = m->p_cursor.current;

    // tuple dester 判断
    parser_must(m, TOKEN_LEFT_PAREN);
    ast_expr_t prefix_expr = parser_expr(m);
    bool is_comma = parser_is(m, TOKEN_COMMA);

    m->p_cursor.current = temp;

#ifdef DEBUG_PARSER
    printf("@@\n");
    fflush(stdout);
#endif

    if (is_comma) {
        return parser_tuple_destr_stmt(m);
    }

    // (a[0])[1].b.(c) = 24
    // (&a.b).call()
    return parser_expr_begin_stmt(m);
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
        return parser_left_param_begin_stmt(m);
    } else if (parser_is(m, TOKEN_THROW)) {
        return parser_throw_stmt(m);
    } else if (parser_is(m, TOKEN_LET)) {
        return parser_let_stmt(m);
    } else if (parser_is(m, TOKEN_LABEL)) {
        return parser_label(m);
    } else if (parser_is(m, TOKEN_IDENT) || parser_is(m, TOKEN_STAR)) {
        // *ident = 12
        return parser_expr_begin_stmt(m);
    } else if (parser_is(m, TOKEN_IF)) {
        return parser_if_stmt(m);
    } else if (parser_is(m, TOKEN_FOR)) {
        return parser_for_stmt(m);
    } else if (parser_is(m, TOKEN_RETURN)) {
        return parser_return_stmt(m);
    } else if (parser_is(m, TOKEN_IMPORT)) {
        return parser_import_stmt(m);
    } else if (parser_is(m, TOKEN_TYPE)) {
        return parser_typedef_stmt(m);
    } else if (parser_is(m, TOKEN_CONTINUE)) {
        return parser_continue_stmt(m);
    } else if (parser_is(m, TOKEN_BREAK)) {
        return parser_break_stmt(m);
    } else if (parser_is(m, TOKEN_GO)) {
        ast_expr_t expr = parser_go_expr(m);
        return stmt_expr_fake_new(m, expr);
    } else if (parser_is(m, TOKEN_MATCH)) {
        ast_expr_t expr = parser_match_expr(m);
        return stmt_expr_fake_new(m, expr);
    } else if (parser_is(m, TOKEN_SELECT)) {
        return parser_select_stmt(m);
    } else if (parser_is(m, TOKEN_TRY)) {
        return parser_try_catch_stmt(m);
    } else if (parser_is(m, TOKEN_MACRO_IDENT)) {
        ast_expr_t expr = parser_expr_with_precedence(m);
        return stmt_expr_fake_new(m, expr);
    }

    PARSER_ASSERTF(false, "statement cannot start with '%s'", parser_peek(m)->literal);
}

static ast_stmt_t *parser_global_stmt(module_t *m) {
    // module parser 只包含着几种简单语句
    if (parser_is(m, TOKEN_VAR)) {
        return parser_var_begin_stmt(m);
    } else if (is_type_begin_stmt(m)) {
        return parser_type_begin_stmt(m);
    } else if (parser_is(m, TOKEN_LABEL)) {
        return parser_label(m);
    } else if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_stmt(m, ast_fndef_new(m, parser_peek(m)->line, parser_peek(m)->column));
    } else if (parser_is(m, TOKEN_IMPORT)) {
        return parser_import_stmt(m);
    } else if (parser_is(m, TOKEN_TYPE)) {
        return parser_typedef_stmt(m);
    }

    PARSER_ASSERTF(false, "non-declaration statement outside fn body")
}

static parser_rule rules[] = {
        [TOKEN_LEFT_PAREN] = {parser_left_paren_expr, parser_call_expr, PRECEDENCE_CALL},
        [TOKEN_LEFT_SQUARE] = {parser_left_square_expr, parser_access, PRECEDENCE_CALL},
        [TOKEN_LEFT_CURLY] = {parser_left_curly_expr, NULL, PRECEDENCE_NULL},
        [TOKEN_LESS_THAN] = {NULL, parser_binary, PRECEDENCE_COMPARE},
        [TOKEN_LEFT_ANGLE] = {NULL, parser_type_args_expr, PRECEDENCE_CALL},
        [TOKEN_MACRO_IDENT] = {parser_macro_call, NULL, PRECEDENCE_NULL},
        [TOKEN_DOT] = {NULL, parser_unknown_select, PRECEDENCE_CALL},
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
        [TOKEN_IS] = {parser_match_is_expr, parser_is_expr, PRECEDENCE_TYPE_CAST},
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
        infix_token->type = TOKEN_RIGHT_SHIFT; // 类型改写
    }

    return infix_token->type;
}

static ast_expr_t parser_precedence_expr(module_t *m, parser_precedence precedence, token_type_t exclude) {
    // 读取表达式前缀
    parser_prefix_fn prefix_fn = find_rule(parser_peek(m)->type)->prefix;

    PARSER_ASSERTF(prefix_fn, "cannot parser ident '%s'", parser_peek(m)->literal);

    ast_expr_t expr = prefix_fn(m);

    // 前缀表达式已经处理完成，判断是否有中缀表达式，有则按表达式优先级进行处理, 如果 +/-/*// /. /[]  等
    token_type_t token_type = parser_infix_token(m, expr);
    if (exclude > 0 && token_type == exclude) {
        return expr;
    }
    parser_rule *infix_rule = find_rule(token_type);

    while (infix_rule->infix_precedence >= precedence) {
        parser_infix_fn infix_fn = infix_rule->infix;

        expr = infix_fn(m, expr);

        token_type = parser_infix_token(m, expr);
        if (exclude > 0 && token_type == exclude) {
            return expr;
        }

        infix_rule = find_rule(token_type);
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
    while (!parser_is(m, TOKEN_RIGHT_CURLY)) {
        // ident 类型
        struct_property_t item = {0};
        item.key = parser_must(m, TOKEN_IDENT)->literal;
        parser_must(m, TOKEN_EQUAL);
        item.right = expr_new_ptr(m);
        *((ast_expr_t *) item.right) = parser_expr(m);
        ct_list_push(struct_new->properties, &item);

        if (parser_is(m, TOKEN_RIGHT_CURLY)) {
            break;
        } else {
            parser_must(m, TOKEN_COMMA);
        }
    }

    parser_must(m, TOKEN_RIGHT_CURLY);
    result.assert_type = AST_EXPR_STRUCT_NEW;
    result.value = struct_new;
    return result;
}

/**
 * 包含默认 precedence = PRECEDENCE_ASSIGN
 * @return
 */
static ast_expr_t parser_expr_with_precedence(module_t *m) {
    return parser_precedence_expr(m, PRECEDENCE_ASSIGN, 0);
}

static ast_expr_t parser_macro_default_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    parser_must(m, TOKEN_LEFT_PAREN);
    parser_must(m, TOKEN_RIGHT_PAREN);
    ast_macro_default_expr_t *default_expr = NULL;
    result.assert_type = AST_MACRO_EXPR_DEFAULT;
    result.value = default_expr;
    return result;
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
    ast_fndef_t *fndef = ast_fndef_new(m, call_expr->line, call_expr->column);
    fndef->is_async = true;
    fndef->is_errable = true;
    fndef->symbol_name = NULL;
    fndef->params = ct_list_new(sizeof(ast_var_decl_t));
    fndef->return_type = type_kind_new(TYPE_VOID);

    slice_t *stmt_list = slice_new();

    // var a = call(x, x, x)
    ast_stmt_t *vardef_stmt = stmt_new(m);
    vardef_stmt->assert_type = AST_STMT_VARDEF;
    ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
    vardef->var_decl.type = type_kind_new(TYPE_UNKNOWN);
    vardef->var_decl.ident = FN_COROUTINE_RETURN_VAR;
    vardef->right = *ast_expr_copy(call_expr);
    vardef_stmt->value = vardef;

    // co_return(&result)
    ast_call_t *call = NEW(ast_call_t);
    call->left = *ast_ident_expr(fndef->line, fndef->column, BUILTIN_CALL_CO_RETURN);
    call->args = ct_list_new(sizeof(ast_expr_t));
    ast_expr_t *arg = expr_new_ptr(m);
    ast_unary_expr_t *unary = NEW(ast_unary_expr_t);
    unary->operand = *ast_ident_expr(fndef->line, fndef->column, FN_COROUTINE_RETURN_VAR);
    unary->op = AST_OP_LA;
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
    ast_fndef_t *fndef = ast_fndef_new(m, call_expr->line, call_expr->column);
    fndef->is_async = true;
    fndef->is_errable = true;
    fndef->symbol_name = NULL;
    fndef->params = ct_list_new(sizeof(ast_var_decl_t));
    fndef->return_type = type_kind_new(TYPE_VOID);

    slice_t *stmt_list = slice_new();

    // call(x, x, x)
    ast_stmt_t *call_stmt = stmt_new(m);
    call_stmt->assert_type = AST_CALL;
    call_stmt->value = ast_expr_copy(call_expr)->value;
    slice_push(stmt_list, call_stmt);

    fndef->body = stmt_list;

    return fndef;
}

static ast_expr_t parser_match_expr(module_t *m) {
    parser_must(m, TOKEN_MATCH);

    ast_match_t *match = NEW(ast_match_t);

    // 如果想要编写 match {a, b, c} {
    // } 这样有歧义的表达式时，需要使用括号包裹, match ({a, b, c}) {}
    if (!parser_is(m, TOKEN_LEFT_CURLY)) {
        ast_expr_t subject = parser_expr_with_precedence(m);
        match->subject = expr_new_ptr(m);
        *match->subject = subject;

        m->parser_match_subject = true; // 识别 ｜ 符号的含义
    }

    parser_must(m, TOKEN_LEFT_CURLY);
    slice_t *cases = slice_new();

    while (!parser_consume(m, TOKEN_RIGHT_CURLY)) {
        ast_match_case_t *match_case = NEW(ast_match_case_t);

        m->parser_match_cond = true; // 用来区分 match is 等特殊表达式

        list_t *cond_list = ct_list_new(sizeof(ast_expr_t));
        if (match->subject) {
            do {
                ast_expr_t expr = parser_precedence_expr(m, PRECEDENCE_ASSIGN, TOKEN_OR);

                ct_list_push(cond_list, &expr);
            } while (parser_consume(m, TOKEN_OR));
        } else {
            ast_expr_t expr = parser_expr(m);
            ct_list_push(cond_list, &expr);
        }

        parser_must(m, TOKEN_RIGHT_ARROW); // ->

        m->parser_match_cond = false;

        // expr or stmt
        slice_t *handle_body = NULL;
        if (parser_is(m, TOKEN_LEFT_CURLY)) {
            // expr -> {
            //  stmt
            //  stmt
            // }
            handle_body = parser_body(m);
        } else {
            slice_t *stmt_list = slice_new();
            ast_stmt_t *stmt = stmt_new(m);
            ast_break_t *b = NEW(ast_break_t);
            b->expr = expr_new_ptr(m);
            // expr -> {break expr}
            *b->expr = parser_expr(m);
            stmt->value = b;
            stmt->assert_type = AST_STMT_BREAK;
            slice_push(stmt_list, stmt);
            handle_body = stmt_list;
        }

        parser_must_stmt_end(m);

        match_case->cond_list = cond_list;
        match_case->handle_body = handle_body;

        slice_push(cases, match_case);
    }

    match->cases = cases;

    m->parser_match_subject = false;
    ast_expr_t result = expr_new(m);
    result.assert_type = AST_MATCH;
    result.value = match;

    return result;
}

static slice_t *async_args_copy(module_t *m, ast_expr_t expr) {
    ast_call_t *call_expr = expr.value;
    slice_t *result = slice_new();
    for (int i = 0; i < call_expr->args->length; ++i) {
        ast_expr_t *arg_expr = ct_list_value(call_expr->args, i);

        char *unique_name = var_ident_with_index(m, TEMP_VAR_IDENT);
        ast_stmt_t *vardef_stmt = stmt_new(m);
        vardef_stmt->assert_type = AST_STMT_VARDEF;
        ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
        vardef->var_decl.type = type_kind_new(TYPE_UNKNOWN);
        vardef->var_decl.ident = strdup(unique_name);
        vardef->right = *ast_expr_copy(arg_expr);
        vardef_stmt->value = vardef;
        slice_push(result, vardef_stmt);

        // change arg ident
        *arg_expr = *ast_ident_expr(expr.line, expr.column, unique_name);
    }

    return result;
}

static ast_expr_t parser_go_expr(module_t *m) {
    parser_must(m, TOKEN_GO);

    ast_expr_t call_expr = parser_expr(m);

    // expr 的 type 必须是 call
    PARSER_ASSERTF(call_expr.assert_type == AST_CALL, "go expr must be call");

    ast_macro_async_t *async_expr = NEW(ast_macro_async_t);

    async_expr->args_copy_stmts = async_args_copy(m, call_expr);

    async_expr->origin_call = call_expr.value;
    async_expr->closure_fn = coroutine_fn_closure(m, &call_expr);
    async_expr->closure_fn_void = coroutine_fn_void_closure(m, &call_expr);
    async_expr->flag_expr = NULL;
    ast_expr_t result = expr_new(m);
    result.assert_type = AST_MACRO_ASYNC;
    result.value = async_expr;
    return result;
}

/**
 * future<int> f = @async(sum(1, 2), co.THREAD)
 * @param m
 * @return
 */
static ast_expr_t parser_macro_async_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    ast_macro_async_t *async_expr = NEW(ast_macro_async_t);

    parser_must(m, TOKEN_LEFT_PAREN);

    ast_expr_t call_expr = parser_expr(m);

    async_expr->args_copy_stmts = async_args_copy(m, call_expr);

    async_expr->origin_call = call_expr.value;
    async_expr->closure_fn = coroutine_fn_closure(m, &call_expr);
    async_expr->closure_fn_void = coroutine_fn_void_closure(m, &call_expr);

    // 可选的 flag
    if (parser_consume(m, TOKEN_COMMA)) {
        async_expr->flag_expr = NEW(ast_expr_t);
        *async_expr->flag_expr = parser_expr(m);
    }
    parser_must(m, TOKEN_RIGHT_PAREN);

    result.assert_type = AST_MACRO_ASYNC;
    result.value = async_expr;
    return result;
}

static ast_expr_t parser_macro_ula_expr(module_t *m) {
    ast_expr_t result = expr_new(m);
    ast_macro_ula_expr_t *ula_expr = NEW(ast_macro_ula_expr_t);

    parser_must(m, TOKEN_LEFT_PAREN);
    ula_expr->src = parser_expr(m);
    parser_must(m, TOKEN_RIGHT_PAREN);

    result.assert_type = AST_MACRO_EXPR_ULA;
    result.value = ula_expr;
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

    if (str_equal(token->literal, MACRO_DEFAULT)) {
        return parser_macro_default_expr(m);
    }

    if (str_equal(token->literal, MACRO_ASYNC)) {
        return parser_macro_async_expr(m);
    }

    if (str_equal(token->literal, MACRO_ULA)) {
        return parser_macro_ula_expr(m);
    }

    PARSER_ASSERTF(false, "macro '%s' not defined", token->literal);
}

static bool parser_is_new(module_t *m) {
    if (!parser_ident_is(m, token_str[TOKEN_NEW])) {
        return false;
    }

    return parser_next_is(m, 1, TOKEN_IDENT) ||
           parser_next_is(m, 1, TOKEN_INT) ||
           parser_next_is(m, 1, TOKEN_I8) ||
           parser_next_is(m, 1, TOKEN_I16) ||
           parser_next_is(m, 1, TOKEN_I32) ||
           parser_next_is(m, 1, TOKEN_I64) ||
           parser_next_is(m, 1, TOKEN_UINT) ||
           parser_next_is(m, 1, TOKEN_U8) ||
           parser_next_is(m, 1, TOKEN_U16) ||
           parser_next_is(m, 1, TOKEN_U32) ||
           parser_next_is(m, 1, TOKEN_U64) ||
           parser_next_is(m, 1, TOKEN_FLOAT) ||
           parser_next_is(m, 1, TOKEN_F32) ||
           parser_next_is(m, 1, TOKEN_F64) ||
           parser_next_is(m, 1, TOKEN_LEFT_SQUARE) || // array new
           parser_next_is(m, 1, TOKEN_BOOL);
}

/**
 * 表达式优先级处理方式
 * @return
 */
static ast_expr_t parser_expr(module_t *m) {
    // struct new
    if (parser_is_struct_new_expr(m)) {
        return parser_struct_new_expr(m);
    }

    // go
    if (parser_is(m, TOKEN_GO)) {
        return parser_go_expr(m);
    }

    // match
    if (parser_is(m, TOKEN_MATCH)) {
        return parser_match_expr(m);
    }

    // fn def, 也能写到括号里面呀
    if (parser_is(m, TOKEN_FN)) {
        return parser_fndef_expr(m);
    }

    if (parser_is_new(m)) {
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

        slice_push(block_stmt, parser_global_stmt(m));

        parser_must_stmt_end(m);
    }

    return block_stmt;
}

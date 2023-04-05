#ifndef NATURE_SRC_SYNTAX_PARSER_H_
#define NATURE_SRC_SYNTAX_PARSER_H_

#include "utils/linked.h"
#include "utils/slice.h"
#include "src/ast.h"
#include "token.h"
#include <stdlib.h>
#include "src/module.h"

typedef enum {
    PRECEDENCE_NULL, // 最低优先级
    PRECEDENCE_ASSIGN,
    PRECEDENCE_OR,
    PRECEDENCE_AND,
    PRECEDENCE_EQUALITY,
    PRECEDENCE_COMPARE,
    PRECEDENCE_TERM,
    PRECEDENCE_FACTOR,
    PRECEDENCE_UNARY,
    PRECEDENCE_CALL, // foo.bar foo["bar"] foo() foo().foo.bar 这几个表达式都是同一优先级，应该从左往右依次运算
    PRECEDENCE_PRIMARY, // 最高优先级
} parser_precedence;

typedef ast_expr (*parser_prefix_fn)(module_t *module);

typedef ast_expr (*parser_infix_fn)(module_t *module, ast_expr prefix);

typedef struct {
    parser_prefix_fn prefix;
    parser_infix_fn infix;
    parser_precedence infix_precedence;
} parser_rule;

slice_t *parser(module_t *m, linked_t *token_list);

static ast_stmt *parser_stmt(module_t *m);

static ast_expr parser_expr(module_t *m);

static typeuse_t parser_typedecl(module_t *m);

static ast_expr parser_precedence_expr(module_t *m, parser_precedence precedence);

static parser_rule *find_rule(token_e type);

static ast_stmt *parser_if_stmt(module_t *m);
///**
// * @param type
// * @return
// */
//parser_rule *parser_get_rule(token_e type);
//
//ast_expr parser_expr(module_t *m);
//
//ast_expr parser_precedence_expr(module_t *m, parser_precedence precedence);
//
//ast_expr parser_literal(module_t *m);
//
//ast_expr parser_unary(module_t *m);
//
//ast_expr parser_grouping(module_t *m);
//
//ast_expr parser_ident_expr(module_t *m);
//
//ast_expr parser_call_expr(module_t *m, ast_expr left_expr);
//
//ast_expr parser_struct_access(module_t *m, ast_expr left);
//
//ast_expr parser_access(module_t *m, ast_expr left);
//
//ast_expr parser_binary(module_t *m, ast_expr left);
//
//ast_expr parser_fn_decl_expr(module_t *m, typeuse_t type);
//
//ast_expr parser_new_struct(module_t *m, typeuse_t type);
//
//ast_expr parser_new_list(module_t *m);
//
//ast_expr parser_new_map(module_t *m);
//
//ast_expr parser_direct_type_expr(module_t *m);
//
//ast_expr parser_struct_type_expr(module_t *m);
//
//ast_stmt *parser_stmt(module_t *m);
//
//slice_t *parser_block(module_t *m);
//
//ast_stmt *parser_ident_stmt(module_t *m);
//
//ast_stmt *parser_import_stmt(module_t *m);
//
//ast_stmt *parser_return_stmt(module_t *m);
//
//ast_stmt *parser_auto_infer_decl(module_t *m);
//
//ast_stmt *parser_var_or_fn_decl(module_t *m);
//
//ast_stmt *parser_if_stmt(module_t *m);
//
//ast_stmt *parser_for_stmt(module_t *m);
//
//ast_stmt *parser_while_stmt(module_t *m);
//
//ast_stmt *parser_type_decl_stmt(module_t *m);
//
//slice_t *parser_else_if(module_t *m);
//
//ast_new_fn *parser_fn_decl(module_t *m, typeuse_t type);
//
//ast_var_decl *parser_var_decl(module_t *m);
//
//void parser_actual_param(module_t *m, ast_call *call);
//
//void parser_formal_param(module_t *m, ast_new_fn *fn_decl);
//
//void parser_type_function_formal_param(module_t *m, typeuse_fn_t *type_fn);
//
//typeuse_t parser_type(module_t *m);
//
///**
// * foo = 12
// * foo.bar = 12
// * foo[bar] = 12
// * @param ident
// * @return
// */
//ast_stmt *parser_assign(module_t *m, ast_expr left);
//
//token_t *parser_advance(module_t *m);
//
//token_t *parser_peek(module_t *m);
//
//int parser_line(module_t *m);
//
//bool parser_consume(module_t *m, token_e t);
//
//bool parser_is(module_t *m, token_e t);
//
//bool parser_next_is(module_t *m, int step, token_e t);
//
//linked_node *parser_next(module_t *m, int step);
//
///**
// * 兼容 void
// * @return
// */
//bool parser_is_direct_type(module_t *m);
//
//bool parser_is_custom_type_var(module_t *m);
//
//bool parser_is_simple_type(module_t *m);
//
//token_t *parser_must(module_t *m, token_e t);
//
//bool parser_must_stmt_end(module_t *m);
//
//bool parser_is_fn_decl(module_t *m, linked_node *current);
//
//void parser_cursor_init(module_t *m, linked_t *token_list);
//
//ast_stmt *parser_new_stmt();
//
//ast_expr parser_new_expr(module_t *m);
//
#endif //NATURE_SRC_SYNTAX_PARSER_H_

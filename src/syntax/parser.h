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
    PRECEDENCE_STRUCT_NEW, // as / is
    PRECEDENCE_OR_OR, // ||
    PRECEDENCE_AND_AND, // &&
    PRECEDENCE_OR, // |
    PRECEDENCE_XOR, // ^
    PRECEDENCE_AND, // %
    PRECEDENCE_CMP_EQUAL, // == !=
    PRECEDENCE_COMPARE, // > < >= <=
    PRECEDENCE_SHIFT, // << >>
    PRECEDENCE_TERM, // + -
    PRECEDENCE_FACTOR, // * / %
    PRECEDENCE_TYPE_CAST, // as / is
    PRECEDENCE_UNARY, // - ! ~
    PRECEDENCE_CALL, // foo.bar foo["bar"] foo() foo().foo.bar 这几个表达式都是同一优先级，应该从左往右依次运算
    PRECEDENCE_PRIMARY, // 最高优先级
} parser_precedence;


static ast_expr_op_t token_to_ast_op[] = {
        [TOKEN_PLUS] = AST_OP_ADD, // +
        [TOKEN_MINUS] = AST_OP_SUB, // -
        [TOKEN_STAR] = AST_OP_MUL, // *
        [TOKEN_SLASH] = AST_OP_DIV, // /
        [TOKEN_PERSON] = AST_OP_REM, // /
        [TOKEN_EQUAL_EQUAL] = AST_OP_EE,
        [TOKEN_NOT_EQUAL] = AST_OP_NE,
        [TOKEN_GREATER_EQUAL] = AST_OP_GE,
        [TOKEN_RIGHT_ANGLE] = AST_OP_GT,
        [TOKEN_LESS_EQUAL] = AST_OP_LE,
        [TOKEN_LEFT_ANGLE] = AST_OP_LT,
        [TOKEN_AND_AND] = AST_OP_AND_AND,
        [TOKEN_OR_OR] = AST_OP_OR_OR,
        // 位运算
        [TOKEN_TILDE] = AST_OP_BNOT, // ~
        [TOKEN_AND] = AST_OP_AND,  // &
        [TOKEN_OR] = AST_OP_OR,  // |
        [TOKEN_XOR] = AST_OP_XOR,  // ^
        [TOKEN_LEFT_SHIFT] = AST_OP_LSHIFT,  // <<
        [TOKEN_RIGHT_SHIFT] = AST_OP_RSHIFT, // >>

        // equal 快捷运算拆解
        [TOKEN_PERSON_EQUAL] = AST_OP_REM,
        [TOKEN_MINUS_EQUAL] =AST_OP_SUB,
        [TOKEN_PLUS_EQUAL] = AST_OP_ADD,
        [TOKEN_SLASH_EQUAL] = AST_OP_DIV,
        [TOKEN_STAR_EQUAL] = AST_OP_MUL,
        [TOKEN_OR_EQUAL] = AST_OP_OR,
        [TOKEN_AND_EQUAL] = AST_OP_AND,
        [TOKEN_XOR_EQUAL] = AST_OP_XOR,
        [TOKEN_LEFT_SHIFT_EQUAL] = AST_OP_LSHIFT,
        [TOKEN_RIGHT_SHIFT_EQUAL] = AST_OP_RSHIFT,
};

static type_kind token_to_kind[] = {
        // literal
        [TOKEN_TRUE] = TYPE_BOOL,
        [TOKEN_FALSE] = TYPE_BOOL,
        [TOKEN_LITERAL_FLOAT] = TYPE_FLOAT,
        [TOKEN_LITERAL_INT] = TYPE_INT,
        [TOKEN_LITERAL_STRING] = TYPE_STRING,

        // type
        [TOKEN_BOOL] = TYPE_BOOL,
        [TOKEN_FLOAT] = TYPE_FLOAT,
        [TOKEN_F32] = TYPE_FLOAT32,
        [TOKEN_F64] = TYPE_FLOAT64,
        [TOKEN_INT] = TYPE_INT,
        [TOKEN_I8] = TYPE_INT8,
        [TOKEN_I16] = TYPE_INT16,
        [TOKEN_I32] = TYPE_INT32,
        [TOKEN_I64] = TYPE_INT64,
        [TOKEN_UINT] = TYPE_UINT,
        [TOKEN_U8] = TYPE_UINT8,
        [TOKEN_U16] = TYPE_UINT16,
        [TOKEN_U32] = TYPE_UINT32,
        [TOKEN_U64] = TYPE_UINT64,
        [TOKEN_STRING] = TYPE_STRING,
        [TOKEN_NULL] = TYPE_NULL,
        [TOKEN_SELF] = TYPE_SELF,
        [TOKEN_VAR] = TYPE_UNKNOWN,
        [TOKEN_ANY] = TYPE_UNION,
};


typedef ast_expr_t (*parser_prefix_fn)(module_t *module);

typedef ast_expr_t (*parser_infix_fn)(module_t *module, ast_expr_t prefix);

typedef struct {
    parser_prefix_fn prefix;
    parser_infix_fn infix;
    parser_precedence infix_precedence;
} parser_rule;

slice_t *parser(module_t *m, linked_t *token_list);

static ast_stmt_t *parser_stmt(module_t *m);

static ast_expr_t parser_expr(module_t *m);

static type_t parser_single_type(module_t *m);

static type_t parser_type(module_t *m);

static ast_expr_t parser_precedence_expr(module_t *m, parser_precedence precedence);

static parser_rule *find_rule(token_e token_type);

static ast_stmt_t *parser_if_stmt(module_t *m);

#endif //NATURE_SRC_SYNTAX_PARSER_H_

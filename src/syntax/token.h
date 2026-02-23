#ifndef NATURE_SRC_SYNTAX_TOKEN_H_
#define NATURE_SRC_SYNTAX_TOKEN_H_

#include <stdlib.h>

#include "utils/helper.h"

typedef enum {
    // SINGLE-CHARACTER TOKENS.
    TOKEN_LEFT_PAREN = 1,
    TOKEN_RIGHT_PAREN,// ()
    TOKEN_LEFT_SQUARE,
    TOKEN_RIGHT_SQUARE,// []
    TOKEN_LEFT_CURLY,
    TOKEN_RIGHT_CURLY,// {}
    TOKEN_LEFT_ANGLE, // <
    TOKEN_LESS_THAN,  // <
    TOKEN_RIGHT_ANGLE,// >

    TOKEN_COMMA,      // ,
    TOKEN_DOT,        // .
    TOKEN_MINUS,      // -
    TOKEN_PLUS,       // +
    TOKEN_ELLIPSIS,   // ...
    TOKEN_RANGE,   // ...
    TOKEN_COLON,      // :
    TOKEN_SEMICOLON,  // ;
    TOKEN_SLASH,      // /
    TOKEN_STAR,       //  a * b, *a
    TOKEN_IMPORT_STAR,// import as *
    TOKEN_PERSON,     // %
    TOKEN_QUESTION,   // ?
    TOKEN_RIGHT_ARROW,// ->

    // ONE OR TWO CHARACTER TOKENS.
    TOKEN_NOT,// !
    TOKEN_NOT_EQUAL,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER_EQUAL,// >=
    TOKEN_LESS_EQUAL,   // <=
    TOKEN_AND_AND,      // &&
    TOKEN_OR_OR,        // ||

    TOKEN_PLUS_EQUAL,       // +=
    TOKEN_MINUS_EQUAL,      // -=
    TOKEN_STAR_EQUAL,       // *=
    TOKEN_SLASH_EQUAL,      // /=
    TOKEN_PERSON_EQUAL,     // %=
    TOKEN_AND_EQUAL,        // &=
    TOKEN_OR_EQUAL,         // |=
    TOKEN_XOR_EQUAL,        // ^=
    TOKEN_LEFT_SHIFT_EQUAL, // <<=
    TOKEN_RIGHT_SHIFT_EQUAL,// >>=

    // 位运算
    TOKEN_TILDE,      // ~
    TOKEN_AND,        // &
    TOKEN_OR,         // |
    TOKEN_XOR,        // ^
    TOKEN_LEFT_SHIFT, // <<
    TOKEN_RIGHT_SHIFT,// >>

    // LITERALS.
    TOKEN_IDENT,
    TOKEN_POUND,      // #
    TOKEN_MACRO_IDENT,// @sizeof/@default/@async
    TOKEN_LABEL,      // #linkid
    TOKEN_LITERAL_STRING,
    TOKEN_LITERAL_FLOAT,
    TOKEN_LITERAL_INT,

    // TYPE
    TOKEN_STRING,
    TOKEN_BOOL,
    TOKEN_FLOAT,
    TOKEN_INT,
    TOKEN_UINT,
    TOKEN_U8,
    TOKEN_U16,
    TOKEN_U32,
    TOKEN_U64,
    TOKEN_I8,
    TOKEN_I16,
    TOKEN_I32,
    TOKEN_I64,
    TOKEN_F32,
    TOKEN_F64,
    TOKEN_NEW,

    // 内置复合类型
    TOKEN_ARR,
    TOKEN_VEC,
    TOKEN_MAP,
    TOKEN_TUP,
    TOKEN_SET,
    TOKEN_CHAN,

    // KEYWORDS.
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_TYPE,
    TOKEN_NULL,
    TOKEN_VOID,
    TOKEN_ANY,
    TOKEN_STRUCT,
    TOKEN_INTERFACE,
    TOKEN_ENUM,
    TOKEN_UNION,
    TOKEN_THROW,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_MATCH,
    TOKEN_SELECT,
    TOKEN_TEST,
    TOKEN_CONTINUE,
    TOKEN_BREAK,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_ELSE_IF,
    TOKEN_VAR,
    TOKEN_CONST,
    TOKEN_LET,
    TOKEN_IS,
    TOKEN_SIZEOF,
    TOKEN_REFLECT_HASH,
    TOKEN_AS,
    TOKEN_BOOM,
    TOKEN_FN,
    TOKEN_IMPORT,
    TOKEN_RETURN,
    TOKEN_GO,
    TOKEN_STMT_EOF, // ;
    TOKEN_EOF,// TOKEN_EOF 一定要在最后一个，否则会索引溢出
} token_type_t;

typedef enum {
    LEFT_ANGLE_TYPE_FN_ARGS,
    LEFT_ANGLE_TYPE_TYPE_ARGS,
    LOGIC_LT,
} left_angle_type_e;

static string token_str[] = {
        [TOKEN_LEFT_PAREN] = "(",
        [TOKEN_RIGHT_PAREN] = ")",
        [TOKEN_LEFT_SQUARE] = "[",
        [TOKEN_RIGHT_SQUARE] = "]",
        [TOKEN_LEFT_CURLY] = "{",
        [TOKEN_RIGHT_CURLY] = "}",
        [TOKEN_LEFT_ANGLE] = "<",
        [TOKEN_RIGHT_ANGLE] = ">",
        [TOKEN_COMMA] = ",",
        [TOKEN_DOT] = ".",
        [TOKEN_MINUS] = "-",
        [TOKEN_PLUS] = "+",
        [TOKEN_COLON] = ":",
        [TOKEN_SLASH] = "/",
        [TOKEN_STAR] = "*",
        [TOKEN_IMPORT_STAR] = "*",
        [TOKEN_QUESTION] = "?",
        [TOKEN_NOT] = "!",
        [TOKEN_PERSON] = "%",
        [TOKEN_NOT_EQUAL] = "!=",
        [TOKEN_EQUAL] = "=",
        [TOKEN_EQUAL_EQUAL] = "==",
        [TOKEN_GREATER_EQUAL] = ">=",
        [TOKEN_LESS_EQUAL] = "<=",
        [TOKEN_AND] = "&",
        [TOKEN_AND_AND] = "&&",
        [TOKEN_OR] = "|",
        [TOKEN_OR_OR] = "||",
        [TOKEN_XOR] = "^",
        [TOKEN_LEFT_SHIFT] = "<<",
        [TOKEN_RIGHT_SHIFT] = ">>",
        [TOKEN_TILDE] = "~",
        [TOKEN_RIGHT_ARROW] = "->",

        [TOKEN_PERSON_EQUAL] = "%=",
        [TOKEN_MINUS_EQUAL] = "-=",
        [TOKEN_PLUS_EQUAL] = "+=",
        [TOKEN_SLASH_EQUAL] = "/=",
        [TOKEN_STAR_EQUAL] = "*=",
        [TOKEN_OR_EQUAL] = "|=",
        [TOKEN_AND_EQUAL] = "&=",
        [TOKEN_XOR_EQUAL] = "^=",
        [TOKEN_LEFT_SHIFT_EQUAL] = "<<=",
        [TOKEN_RIGHT_SHIFT_EQUAL] = ">>=",

        [TOKEN_IDENT] = "ident_literal",
        [TOKEN_LITERAL_STRING] = "string_literal",
        [TOKEN_LITERAL_FLOAT] = "float_literal",
        [TOKEN_LITERAL_INT] = "int_literal",

        [TOKEN_NEW] = "new",
        [TOKEN_CONTINUE] = "continue",
        [TOKEN_BREAK] = "break",

        [TOKEN_CHAN] = "chan",
        [TOKEN_VEC] = "vec",
        [TOKEN_MAP] = "map",
        [TOKEN_SET] = "set",
        [TOKEN_TUP] = "tup",
        [TOKEN_TRUE] = "true",
        [TOKEN_FALSE] = "false",
        [TOKEN_TYPE] = "type",
        [TOKEN_NULL] = "null",
        [TOKEN_VOID] = "void",
        [TOKEN_ANY] = "any",
        [TOKEN_STRUCT] = "struct",

        [TOKEN_FOR] = "for",
        [TOKEN_IN] = "in",
        [TOKEN_IF] = "if",
        [TOKEN_ELSE] = "else",
        [TOKEN_ELSE_IF] = "else if",

        [TOKEN_VAR] = "var",
        [TOKEN_CONST] = "const",
        [TOKEN_STRING] = "string",
        [TOKEN_BOOL] = "bool",
        [TOKEN_FLOAT] = "float",
        [TOKEN_F32] = "f32",
        [TOKEN_F64] = "f64",
        [TOKEN_INT] = "int",
        [TOKEN_I8] = "i8",
        [TOKEN_I16] = "i16",
        [TOKEN_I32] = "i32",
        [TOKEN_I64] = "i64",
        [TOKEN_UINT] = "uint",
        [TOKEN_U8] = "u8",
        [TOKEN_U16] = "u16",
        [TOKEN_U32] = "u32",
        [TOKEN_U64] = "u64",

        [TOKEN_FN] = "fn",
        [TOKEN_RETURN] = "return",
        [TOKEN_CATCH] = "catch",
        [TOKEN_MATCH] = "match",
        [TOKEN_SELECT] = "select",
        [TOKEN_TEST] = "test",
        [TOKEN_TRY] = "try",
        [TOKEN_THROW] = "throw",
        [TOKEN_LET] = "let",

        [TOKEN_IMPORT] = "import",

        // 类型相关
        [TOKEN_AS] = "as",
        [TOKEN_IS] = "is",
        [TOKEN_GO] = "go",
        [TOKEN_LABEL] = "fn_label",
        [TOKEN_MACRO_IDENT] = "macro_ident",

        [TOKEN_STMT_EOF] = ";",
        [TOKEN_EOF] = "\0",
};

typedef struct {
    token_type_t type;// 通配类型，如 var
    char *literal;
    int line;
    int column;// token 的起始字符
    int length;// token 占用的总字符长度
} token_t;

static inline bool token_complex_assign(token_type_t t) {
    return t == TOKEN_PERSON_EQUAL || t == TOKEN_MINUS_EQUAL || t == TOKEN_PLUS_EQUAL || t == TOKEN_SLASH_EQUAL ||
           t == TOKEN_STAR_EQUAL ||
           t == TOKEN_OR_EQUAL || t == TOKEN_AND_EQUAL || t == TOKEN_XOR_EQUAL || t == TOKEN_LEFT_SHIFT_EQUAL ||
           t == TOKEN_RIGHT_SHIFT_EQUAL;
}

static inline token_t *token_new_with_len(uint8_t token, char *literal, uint64_t len, int line, int column) {
    token_t *t = malloc(sizeof(token_t));
    t->type = token;
    t->literal = literal;
    t->line = line;
    t->column = column;
    t->length = len;

#ifdef DEBUG_SCANNER
    debug_scanner(t);
#endif
    return t;
}

static inline token_t *token_new(uint8_t token, char *literal, int line, int column) {
    token_t *t = malloc(sizeof(token_t));
    t->type = token;
    t->literal = literal;
    t->line = line;
    t->column = column;
    t->length = strlen(literal);

#ifdef DEBUG_SCANNER
    debug_scanner(t);
#endif
    return t;
}

#endif// NATURE_SRC_SYNTAX_TOKEN_H_

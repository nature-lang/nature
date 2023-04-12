#ifndef NATURE_SRC_SYNTAX_TOKEN_H_
#define NATURE_SRC_SYNTAX_TOKEN_H_

#include <stdlib.h>
#include "utils/value.h"

typedef enum {
    // SINGLE-CHARACTER TOKENS.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN, // ()
    TOKEN_LEFT_SQUARE, TOKEN_RIGHT_SQUARE, // []
    TOKEN_LEFT_CURLY, TOKEN_RIGHT_CURLY, // {}
    TOKEN_LEFT_ANGLE, TOKEN_RIGHT_ANGLE, // <>


    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS, TOKEN_ELLIPSIS,
    TOKEN_COLON, TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, // * STAR
    TOKEN_PERSON, // %


    // ONE OR TWO CHARACTER TOKENS.
    TOKEN_NOT, TOKEN_NOT_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS_EQUAL,
    TOKEN_AND, TOKEN_AND_AND, TOKEN_OR, TOKEN_OR_OR,

    // LITERALS.
    TOKEN_IDENT, TOKEN_LITERAL_STRING, TOKEN_LITERAL_FLOAT, TOKEN_LITERAL_INT,

    // KEYWORDS.
    TOKEN_P_ANGLE, // p<
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_TYPE, TOKEN_NULL, TOKEN_ANY, TOKEN_STRUCT,
    TOKEN_THROW, TOKEN_CATCH, TOKEN_SELF,
    TOKEN_FOR, TOKEN_IN, TOKEN_IF, TOKEN_ELSE, TOKEN_ELSE_IF,
    TOKEN_VAR, TOKEN_STRING, TOKEN_BOOL, TOKEN_FLOAT, TOKEN_INT,
//    TOKEN_MAP, TOKEN_SET,  TOKEN_WHILE, TOKEN_TUPLE, TOKEN_ARRAY,
    TOKEN_FN,
    TOKEN_IMPORT, TOKEN_AS, TOKEN_RETURN,
    TOKEN_STMT_EOF, TOKEN_EOF, // TOKEN_EOF 一定要在最后一个，否则会索引溢出
} token_e;

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
        [TOKEN_PLUS] = "-",
        [TOKEN_COLON] = ":",
        [TOKEN_SEMICOLON] = ";",
        [TOKEN_SLASH] = "/",
        [TOKEN_STAR] = "*",
        [TOKEN_NOT] = "!",
        [TOKEN_NOT_EQUAL] = "!=",
        [TOKEN_EQUAL] = "=",
        [TOKEN_EQUAL_EQUAL] = "==",
        [TOKEN_GREATER_EQUAL] = ">=",
        [TOKEN_LESS_EQUAL] = "<=",
        [TOKEN_AND] = "&",
        [TOKEN_AND_AND] = "&&",
        [TOKEN_OR] = "|",
        [TOKEN_OR_OR] = "||",

        [TOKEN_IDENT] = "ident literal",
        [TOKEN_LITERAL_STRING] = "string literal",
        [TOKEN_LITERAL_FLOAT] = "float literal",
        [TOKEN_LITERAL_INT] = "int literal",

        [TOKEN_TRUE] = "true",
        [TOKEN_FALSE] = "false",
        [TOKEN_TYPE] = "type",
        [TOKEN_NULL] = "null",
        [TOKEN_ANY] = "any",
        [TOKEN_SELF] = "self",
        [TOKEN_STRUCT] = "struct",

        [TOKEN_FOR] = "for",
        [TOKEN_IN] = "in",
        [TOKEN_IF] = "if",
        [TOKEN_ELSE] = "else",
        [TOKEN_ELSE_IF] = "else if",

        [TOKEN_VAR] = "var",
        [TOKEN_STRING] = "string",
        [TOKEN_BOOL] = "bool",
        [TOKEN_FLOAT] = "float",
        [TOKEN_INT] = "int",

//        [TOKEN_LIST] = "list",
//        [TOKEN_MAP] = "map",
//        [TOKEN_SET] = "set",
//        [TOKEN_TUPLE] = "tup",
        [TOKEN_FN] = "fn",
        [TOKEN_RETURN] = "return",
        [TOKEN_CATCH] = "catch",
        [TOKEN_THROW] = "throw",

        [TOKEN_IMPORT] = "import",
        [TOKEN_AS] = "as",

        [TOKEN_STMT_EOF] = ";",
        [TOKEN_EOF] = "\0",
};

typedef struct {
    token_e type; // 通配类型，如 var
    string literal;
    int line;
} token_t;

token_t *token_new(uint8_t type, char *literal, int line);

#endif //NATURE_SRC_SYNTAX_TOKEN_H_

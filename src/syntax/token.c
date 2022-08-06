#include "token.h"
#include "src/debug/debug.h"

string token_type_to_string[] = {
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

        [TOKEN_LITERAL_IDENT] = "ident literal",
        [TOKEN_LITERAL_STRING] = "string literal",
        [TOKEN_LITERAL_FLOAT] = "float literal",
        [TOKEN_LITERAL_INT] = "int literal",

        [TOKEN_TRUE] = "true",
        [TOKEN_FALSE] = "false",
        [TOKEN_TYPE] = "type",
        [TOKEN_NULL] = "null",
        [TOKEN_ANY] = "any",
        [TOKEN_STRUCT] = "struct",

        [TOKEN_FOR] = "for",
        [TOKEN_IN] = "in",
        [TOKEN_WHILE] = "while",
        [TOKEN_IF] = "if",
        [TOKEN_ELSE] = "else",
        [TOKEN_ELSE_IF] = "else if",

        [TOKEN_VAR] = "var",
        [TOKEN_STRING] = "string",
        [TOKEN_BOOL] = "bool",
        [TOKEN_FLOAT] = "float",
        [TOKEN_INT] = "int",

        [TOKEN_ARRAY] = "list",
        [TOKEN_MAP] = "map",
        [TOKEN_FUNCTION] = "fn",
        [TOKEN_VOID] = "void",
        [TOKEN_RETURN] = "return",

        [TOKEN_IMPORT] = " import",
        [TOKEN_AS] = "module_name",

        [TOKEN_STMT_EOF] = ";",
        [TOKEN_EOF] = "\0",
};

token *token_new(uint8_t type, char *literal, int line) {
    token *t = malloc(sizeof(token));
    t->type = type;
    t->literal = literal;
    t->line = line;

#ifdef DEBUG_SCANNER
    debug_scanner(t);
#endif
    return t;
}

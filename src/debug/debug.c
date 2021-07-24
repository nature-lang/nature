#include "debug.h"
#include <stdio.h>

int current_parser_line = 0;

string ast_stmt_expr_type_to_debug[] = {
    [AST_EXPR_LITERAL]="AST_EXPR_TYPE_LITERAL",
    [AST_EXPR_BINARY]="AST_EXPR_TYPE_BINARY",
    [AST_EXPR_UNARY]="AST_EXPR_TYPE_UNARY",
    [AST_EXPR_IDENT]="AST_EXPR_TYPE_IDENT",
    [AST_EXPR_SELECT_PROPERTY]="AST_EXPR_TYPE_ACCESS_STRUCT",
    [AST_EXPR_ACCESS_ENV]="AST_EXPR_TYPE_ENV_INDEX",
    [AST_EXPR_ACCESS]="AST_EXPR_TYPE_ACCESS",
    [AST_EXPR_SELECT]="AST_EXPR_TYPE_SELECT",
    [AST_EXPR_ACCESS_MAP]="AST_EXPR_TYPE_ACCESS_MAP",
    [AST_EXPR_NEW_MAP]="AST_EXPR_TYPE_NEW_MAP",
    [AST_EXPR_ACCESS_LIST]="AST_EXPR_TYPE_ACCESS_LIST",
    [AST_EXPR_NEW_LIST]="AST_EXPR_TYPE_NEW_LIST",
//    [AST_EXPR_TYPE_LIST_DECL]="AST_EXPR_TYPE_LIST_DECL",
    [AST_VAR_DECL]="AST_VAR_DECL",
    [AST_STMT_VAR_DECL_ASSIGN]="AST_STMT_VAR_DECL_ASSIGN",
    [AST_STMT_ASSIGN]="AST_STMT_ASSIGN",
    [AST_STMT_RETURN]="AST_STMT_RETURN",
    [AST_STMT_IF]="AST_STMT_IF",
    [AST_STMT_FOR_IN]="AST_STMT_FOR_IN",
    [AST_STMT_WHILE]="AST_STMT_WHILE",
    [AST_FUNCTION_DECL]="AST_FUNCTION_DECL",
    [AST_CALL]="AST_CALL",
    [AST_CLOSURE_DECL]="AST_CLOSURE_DECL",
    [AST_STMT_TYPE_DECL]="AST_STMT_TYPE_DECL",
};

string token_type_to_debug[] = {
    [TOKEN_LEFT_PAREN]="TOKEN_LEFT_PAREN",
    [TOKEN_RIGHT_PAREN]="TOKEN_RIGHT_PAREN", // ()
    [TOKEN_LEFT_SQUARE]="TOKEN_LEFT_SQUARE",
    [TOKEN_RIGHT_SQUARE]="TOKEN_RIGHT_SQUARE", // []
    [TOKEN_LEFT_CURLY]="TOKEN_LEFT_CURLY",
    [TOKEN_RIGHT_CURLY]="TOKEN_RIGHT_CURLY", // {}
    [TOKEN_LEFT_ANGLE]="TOKEN_LEFT_ANGLE",
    [TOKEN_RIGHT_ANGLE]="TOKEN_RIGHT_ANGLE", // <>


    [TOKEN_COMMA]="TOKEN_COMMA",
    [TOKEN_DOT]="TOKEN_DOT",
    [TOKEN_MINUS]="TOKEN_MINUS",
    [TOKEN_PLUS]="TOKEN_PLUS",
    [TOKEN_COLON]="TOKEN_COLON",
    [TOKEN_SEMICOLON]="TOKEN_SEMICOLON",
    [TOKEN_SLASH]="TOKEN_SLASH",
    [TOKEN_STAR]="TOKEN_STAR", // * STAR
    [TOKEN_EOF]="TOKEN_EOF",
    [TOKEN_STMT_EOF]="TOKEN_STMT_EOF",

    // ONE OR TWO CHARACTER TOKENS.
    [TOKEN_NOT]="TOKEN_NOT",
    [TOKEN_NOT_EQUAL]="TOKEN_NOT_EQUAL",
    [TOKEN_EQUAL]="TOKEN_EQUAL",
    [TOKEN_EQUAL_EQUAL]="TOKEN_EQUAL_EQUAL",
    [TOKEN_GREATER_EQUAL]="TOKEN_GREATER_EQUAL",
    [TOKEN_LESS_EQUAL]="TOKEN_LESS_EQUAL",
    [TOKEN_AND]="TOKEN_AND",
    [TOKEN_AND_AND]="TOKEN_AND_AND",
    [TOKEN_OR]="TOKEN_OR",
    [TOKEN_OR_OR]="TOKEN_OR_OR",

    // LITERALS.
    [TOKEN_LITERAL_IDENT]="TOKEN_LITERAL_IDENT",
    [TOKEN_LITERAL_STRING]="TOKEN_LITERAL_STRING",
    [TOKEN_LITERAL_FLOAT]="TOKEN_LITERAL_FLOAT",
    [TOKEN_LITERAL_INT]="TOKEN_LITERAL_INT",

    // KEYWORDS.
    [TOKEN_TRUE]="TOKEN_TRUE",
    [TOKEN_FALSE]="TOKEN_FALSE",
    [TOKEN_TYPE]="TOKEN_TYPE",
    [TOKEN_NULL]="TOKEN_NULL",
    [TOKEN_ANY]="TOKEN_ANY",
    [TOKEN_STRUCT]="TOKEN_STRUCT",
    [TOKEN_FOR]="TOKEN_FOR",
    [TOKEN_IN]="TOKEN_IN",
    [TOKEN_WHILE]="TOKEN_WHILE",
    [TOKEN_IF]="TOKEN_IF",
    [TOKEN_ELSE]="TOKEN_ELSE",
    [TOKEN_ELSE_IF]="TOKEN_ELSE_IF",
    [TOKEN_VAR]="TOKEN_VAR",
    [TOKEN_STRING]="TOKEN_STRING",
    [TOKEN_BOOL]="TOKEN_BOOL",
    [TOKEN_FLOAT]="TOKEN_FLOAT",
    [TOKEN_INT]="TOKEN_INT",
    [TOKEN_LIST]="TOKEN_LIST",
    [TOKEN_MAP]="TOKEN_MAP",
    [TOKEN_FUNCTION]="TOKEN_FUNCTION",
    [TOKEN_VOID]="TOKEN_VOID",
    [TOKEN_IMPORT]="TOKEN_IMPORT",
    [TOKEN_AS]="TOKEN_AS",
    [TOKEN_RETURN]="TOKEN_RETURN"
};

void debug_parser(int line, char *token) {
  if (current_parser_line != line) {
    current_parser_line = line;
    printf("\n");
    printf("%d:", line);
  }
  printf("%s ", token);
  fflush(stdout);
}

void debug_parser_stmt(ast_stmt_expr_type t) {
  printf("\n[DEBUG] PARSER stmt: %s\n", ast_stmt_expr_type_to_debug[t]);
}

void debug_scanner(token *t) {
  printf("[DEBUG] SCANNER line:%d, %s: %s |", t->line, token_type_to_debug[t->type], t->literal);
}

void debug_analysis_stmt(ast_stmt stmt) {
  printf("[DEBUG] ANALYSIS line: %d, stmt: %s\n", stmt.line, ast_stmt_expr_type_to_debug[stmt.type]);
}

void debug_infer_stmt(ast_stmt stmt) {
  printf("[DEBUG] INFER line: %d, stmt: %s\n", stmt.line, ast_stmt_expr_type_to_debug[stmt.type]);
}

#include "debug.h"
#include <stdio.h>

int current_parser_line = 0;

void debug_parser(int line, char *token) {
  if (current_parser_line != line) {
    current_parser_line = line;
    printf("\n");
    printf("%d:", line);
  }
  printf("%s ", token);
  fflush(stdout);
}

string ast_stmt_expr_type_to_string[] = {
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

void debug_ast_stmt(ast_stmt_expr_type t) {
  printf("\n[DEBUG]stmt: %s\n", ast_stmt_expr_type_to_string[t]);
}

void debug_analysis_stmt(ast_stmt stmt) {
  printf("[DEBUG] analysis line: %d, stmt: %s\n", stmt.line, ast_stmt_expr_type_to_string[stmt.type]);
}

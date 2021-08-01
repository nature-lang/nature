#include <stdio.h>
#include "debug.h"
#include "debug_lir.h"

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

string lir_op_type_to_debug[] = {
    [LIR_OP_TYPE_ADD]="ADD  ",
    [LIR_OP_TYPE_SUB]="SUB  ",
    [LIR_OP_TYPE_MUL]="MUL  ",
    [LIR_OP_TYPE_DIV]="DIV  ",
    [LIR_OP_TYPE_LT]="LT   ",
    [LIR_OP_TYPE_LTE]="LTE  ",
    [LIR_OP_TYPE_GT]="GT   ",
    [LIR_OP_TYPE_GTE]="GTE  ",
    [LIR_OP_TYPE_EQ_EQ]="E_EQ ",
    [LIR_OP_TYPE_NOT_EQ]="N_EQ ",
    [LIR_OP_TYPE_NOT]="NOT  ",
    [LIR_OP_TYPE_MINUS]="MINUS",
    [LIR_OP_TYPE_PHI]="PHI  ",
    [LIR_OP_TYPE_MOVE]="MOVE ",
    [LIR_OP_TYPE_CMP_GOTO]="CMP_GO",
    [LIR_OP_TYPE_GOTO]="GOTO ",
    [LIR_OP_TYPE_PUSH]="PUSH  ",
    [LIR_OP_TYPE_POP]="POP   ",
    [LIR_OP_TYPE_CALL]="CALL  ",
    [LIR_OP_TYPE_RUNTIME_CALL]="R_CALL",
    [LIR_OP_TYPE_RETURN]="RET   ",
    [LIR_OP_TYPE_LABEL]="LABEL ",
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

void debug_stmt(string type, ast_stmt stmt) {
  printf("[DEBUG] %s line: %d, stmt: %s\n", type, stmt.line, ast_stmt_expr_type_to_debug[stmt.type]);
}

void debug_lir(int line, lir_op *op) {
  printf("[DEBUG] LIR %d:\t", line);
  if (op->type == LIR_OP_TYPE_LABEL) {
    printf(
        "%s:\n",
        lir_operand_to_string(op->result)
    );
    return;
  }
  printf(
      "\t\t%s\t\t%s , %s => %s",
      lir_op_type_to_debug[op->type],
      lir_operand_to_string(op->first),
      lir_operand_to_string(op->second),
      lir_operand_to_string(op->result)
  );
  printf("\n");
}

/**
 * 遍历展示 blocks
 * @param c
 */
void debug_cfg(closure *c) {
  printf("closure: %s------------------------------------------------------------------------\n", c->name);
  for (int i = 0; i < c->blocks.count; ++i) {
    lir_basic_block *basic_block = c->blocks.list[i];
    debug_basic_block(basic_block);
  }
}

/**
 * block: name
 *     move....
 *     xxx...
 *     xxx..
 * succ: name1, name2
 * pred: name1, name2
 *
 *
 * @param block
 */
void debug_basic_block(lir_basic_block *block) {
  // block name, ops, succ, pred
  printf("block: %s\n", block->name);

  lir_op *current = block->operates->front;
  while (current != NULL) {
    printf(
        "\t\t%s\t%s , %s => %s\n",
        lir_op_type_to_debug[current->type],
        lir_operand_to_string(current->first),
        lir_operand_to_string(current->second),
        lir_operand_to_string(current->result)
    );
    current = current->succ;
  }

  printf("\n\t\tpred:");
  for (int i = 0; i < block->preds.count; ++i) {
    printf("%s\t", block->preds.list[i]->name);
  }
  printf("\n\t\tsucc:");
  for (int i = 0; i < block->succs.count; ++i) {
    printf("%s\t", block->succs.list[i]->name);
  }

  printf("\n\n\n");
}

#include <stdio.h>
#include "debug.h"
#include "lir.h"
#include "asm.h"

int current_parser_line = 0;

string ast_type_to_str[] = {
        [AST_EXPR_LITERAL]="AST_EXPR_TYPE_LITERAL",
        [AST_EXPR_BINARY]="AST_EXPR_TYPE_BINARY",
        [AST_EXPR_UNARY]="AST_EXPR_TYPE_UNARY",
        [AST_EXPR_IDENT]="AST_EXPR_TYPE_IDENT",
        [AST_EXPR_STRUCT_SELECT]="AST_EXPR_INSTANCE_ACCESS",
//        [AST_EXPR_STRUCT_ACCESS]="AST_EXPR_TYPE_ACCESS_STRUCT",
        [AST_EXPR_ENV_ACCESS]="AST_EXPR_ENV_VALUE",
        [AST_EXPR_ACCESS]="AST_EXPR_ACCESS",
        [AST_EXPR_SELECT]="AST_EXPR_SELECT",
        [AST_EXPR_MAP_ACCESS]="AST_EXPR_TYPE_ACCESS_MAP",
        [AST_EXPR_MAP_NEW]="AST_EXPR_TYPE_NEW_MAP",
        [AST_EXPR_LIST_ACCESS]="AST_EXPR_TYPE_ACCESS_LIST",
        [AST_EXPR_LIST_NEW]="AST_EXPR_TYPE_NEW_LIST",
//    [AST_EXPR_TYPE_LIST_DECL]="AST_EXPR_TYPE_LIST_DECL",
        [AST_VAR_DECL]="AST_VAR_DECL",
        [AST_STMT_VAR_DEF]="AST_STMT_VAR_DECL_ASSIGN",
        [AST_STMT_ASSIGN]="AST_STMT_ASSIGN",
        [AST_STMT_RETURN]="AST_STMT_RETURN",
        [AST_STMT_IF]="AST_STMT_IF",
        [AST_STMT_FOR_ITERATOR]="AST_STMT_FOR_ITERATOR",
        [AST_FNDEF]="AST_FUNCTION_DECL",
        [AST_CALL]="AST_CALL",
        [AST_CLOSURE_DEF]="AST_CLOSURE_DECL",
        [AST_STMT_TYPEDEF]="AST_STMT_TYPE_DECL",
};

string token_type_to_str[] = {
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
        [TOKEN_IDENT]="TOKEN_IDENT",
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
//        [TOKEN_WHILE]="TOKEN_WHILE",
        [TOKEN_IF]="TOKEN_IF",
        [TOKEN_ELSE]="TOKEN_ELSE",
        [TOKEN_ELSE_IF]="TOKEN_ELSE_IF",
        [TOKEN_VAR]="TOKEN_VAR",
        [TOKEN_STRING]="TOKEN_STRING",
        [TOKEN_BOOL]="TOKEN_BOOL",
        [TOKEN_FLOAT]="TOKEN_FLOAT",
        [TOKEN_INT]="TOKEN_INT",
//        [TOKEN_ARRAY]="TOKEN_ARRAY",
//        [TOKEN_MAP]="TOKEN_MAP",
        [TOKEN_FN]="TOKEN_FN",
        [TOKEN_IMPORT]="TOKEN_IMPORT",
        [TOKEN_AS]="TOKEN_AS",
        [TOKEN_RETURN]="TOKEN_RETURN"
};

string lir_opcode_to_string[] = {
        [LIR_OPCODE_ADD]="ADD  ",
        [LIR_OPCODE_SUB]="SUB  ",
        [LIR_OPCODE_MUL]="MUL  ",
        [LIR_OPCODE_DIV]="DIV  ",
        [LIR_OPCODE_SLT]="SLT   ",
        [LIR_OPCODE_SLE]="SLE  ",
        [LIR_OPCODE_SGT]="SGT   ",
        [LIR_OPCODE_SGE]="SGE  ",
        [LIR_OPCODE_SEE]="SEE ",
        [LIR_OPCODE_SNE]="SNE ",
        [LIR_OPCODE_NOT]="NOT  ",
        [LIR_OPCODE_NEG]="NEG   ",
        [LIR_OPCODE_LEA]="LEA   ",
//        [LIR_OPCODE_SIA]="SIA  ",
        [LIR_OPCODE_PHI]="PHI  ",
        [LIR_OPCODE_MOVE]="MOVE ",
        [LIR_OPCODE_BEQ]="BEQ",
        [LIR_OPCODE_BAL]="BAL ",
        [LIR_OPCODE_PUSH]="PUSH  ",
        [LIR_OPCODE_POP]="POP   ",
        [LIR_OPCODE_CALL]="CALL  ",
        [LIR_OPCODE_BUILTIN_CALL]="B_CALL",
        [LIR_OPCODE_RT_CALL]="R_CALL",
        [LIR_OPCODE_CLR] ="CLR    ",
        [LIR_OPCODE_RETURN]="RETURN ",
        [LIR_OPCODE_LABEL]="LABEL ",
        [LIR_OPCODE_FN_BEGIN] = "FN_BEGIN",
        [LIR_OPCODE_FN_END] = "FN_END",
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

void debug_parser_stmt(ast_type_t t) {
    printf("\n[DEBUG] PARSER stmt: %s\n", ast_type_to_str[t]);
}

void debug_scanner(token_t *t) {
    printf("[DEBUG] SCANNER line:%d, %s: %s \n", t->line, token_type_to_str[t->type], t->literal);
}

void debug_stmt(string type, ast_stmt stmt) {
    printf("[DEBUG] %s line: %d, stmt: %s\n", type, stmt.line, ast_type_to_str[stmt.assert_type]);
}

/**
 * 遍历展示 blocks
 * @param c
 */
void debug_lir(closure_t *c) {
#ifdef DEBUG_LIR
    printf("lir: %s------------------------------------------------------------------------\n", c->name);
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *basic_block = c->blocks->take[i];
        debug_basic_block(basic_block);
    }

    fflush(stdout);
#endif
}

/**
 * block: as
 *     move....
 *     xxx...
 *     xxx..
 * succ: name1, name2
 * pred: name1, name2
 *
 *
 * @param block
 */
void debug_basic_block(basic_block_t *block) {
    // block as, ops, succ, pred
    printf("block: %s\t\t", block->name);
    if (block->loop.index != -1) {
        printf("%s", block->loop.header ? "loop_header|" : "");
        printf("%s", block->loop.end ? "loop_end|" : "");
        printf("loop:i-%d/d-%d", block->loop.index, block->loop.depth);
    }
    printf("\n");

    linked_node *current = block->operations->front;
    while (current->value != NULL) {
        lir_op_t *op = current->value;
        printf("%d", op->id);
        printf("\t\t%s\t", lir_opcode_to_string[op->code]);

        // first
        if (op->first) {
            printf("%s", lir_operand_to_string(op->first));
        }
        if (op->second) {
            if (op->first) {
                printf(", ");
            }
            printf("%s", lir_operand_to_string(op->second));
        }
        if (op->output) {
            if (op->first) {
                printf(" -> ");
            }
            printf("%s", lir_operand_to_string(op->output));
        }
        printf("\n");

        current = current->succ;
    }

    printf("\n\t\tpred:");
    for (int i = 0; i < block->preds->count; ++i) {
        printf("%s\t", ((basic_block_t *) block->preds->take[i])->name);
    }
    printf("\n\t\tsucc:");
    for (int i = 0; i < block->succs->count; ++i) {
        printf("%s\t", ((basic_block_t *) block->succs->take[i])->name);
    }
    printf("\n\t\tlive_in:");
    for (int i = 0; i < block->live->count; ++i) {
        lir_var_t *var = block->live->take[i];
        printf("%s\t", var->ident);
    }
    printf("\n\n\n");
}

void debug_asm(closure_t *c) {
#ifdef DEBUG_ASM
    printf("asm: %s------------------------------------------------------------------------\n", c->name);
    for (int i = 0; i < c->asm_operations->count; ++i) {
        asm_op_to_string(i, c->asm_operations->take[i]);
    }
    fflush(stdout);
#endif
}

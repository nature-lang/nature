#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "utils/value.h"
#include "src/ast.h"
#include "src/syntax/token.h"
#include "src/lir/lir.h"

string lir_opcode_to_string[UINT8_MAX];

#define DEBUG_STR_COUNT 1024

//#define DEBUG_SCANNER

//#define DEBUG_PARSER

//#define DEBUG_ANALYSIS

//#define DEBUG_INFER

//#define DEBUG_COMPILER

//#define DEBUG_LIR

//#define DEBUG_CFG

void debug_scanner(token *t);

void debug_parser(int line, string token);

void debug_parser_stmt(ast_type_e t);

void debug_stmt(string type, ast_stmt stmt);

void debug_lir(closure_t *c);

void debug_asm(closure_t *c);

void debug_basic_block(basic_block_t *block);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

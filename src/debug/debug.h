#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "utils/helper.h"
#include "src/ast.h"
#include "src/syntax/token.h"
#include "src/lir.h"
#include "src/register/interval.h"
#include "src/register/allocate.h"

extern string lir_opcode_to_string[UINT8_MAX];

#define DEBUG_STR_COUNT 1024

//#define DEBUG_SCANNER

//#define DEBUG_PARSER

//#define DEBUG_ANALYZER

//#define DEBUG_CHECKING

//#define DEBUG_COMPILER

//#define DEBUG_LIR

//#define DEBUG_CFG

void debug_scanner(token_t *t);

void debug_parser(int line, string token);

void debug_parser_stmt(ast_type_t t);

void debug_stmt(string type, ast_stmt_t stmt);

void debug_lir(closure_t *c);

void debug_block_lir(closure_t *c, char *stage_after);

void debug_interval(closure_t *c);

void debug_module_asm(module_t *m);

void debug_asm(closure_t *c);

void debug_basic_block(basic_block_t *block);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "src/value.h"
#include "src/ast.h"
#include "src/syntax/token.h"
#include "src/lir/lir.h"

#define DEBUG_STR_COUNT 1000

//#define DEBUG_SCANNER

//#define DEBUG_PARSER

//#define DEBUG_ANALYSIS

//#define DEBUG_INFER

//#define DEBUG_COMPILER

#define DEBUG_COMPILER_LIR

#define DEBUG_CFG

void debug_scanner(token *t);

void debug_parser(int line, string token);

void debug_parser_stmt(ast_stmt_expr_type t);

void debug_stmt(string type, ast_stmt stmt);

void debug_lir(int lir_line, lir_op *op);

void debug_cfg(closure *c);

void debug_basic_block(lir_basic_block *block);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

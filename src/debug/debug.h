#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "src/value.h"
#include "src/ast.h"

#define DEBUG_SCANNER

#define DEBUG_PARSER

//#define DEBUG_ANALYSIS

void debug_parser(int line, string token);

void debug_ast_stmt(ast_stmt_expr_type t);

void debug_analysis_stmt(ast_stmt stmt);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

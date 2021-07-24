#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "src/value.h"
#include "src/ast.h"
#include "src/syntax/token.h"

//#define DEBUG_SCANNER

//#define DEBUG_PARSER

//#define DEBUG_ANALYSIS

//#define DEBUG_INFER

void debug_scanner(token* t);

void debug_parser(int line, string token);

void debug_parser_stmt(ast_stmt_expr_type t);

void debug_analysis_stmt(ast_stmt stmt);

void debug_infer_stmt(ast_stmt stmt);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

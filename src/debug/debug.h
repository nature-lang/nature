#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "src/value.h"
#include "src/ast.h"

#define DEBUG_PARSER

//#define DEBUG_SCANNER

void debug_parser(int line, string token);

void debug_ast_stmt(ast_stmt_expr_type t);

#endif //NATURE_SRC_DEBUG_DEBUG_H_

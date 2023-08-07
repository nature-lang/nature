#ifndef NATURE_SRC_LINEAR_H_
#define NATURE_SRC_LINEAR_H_

#include "utils/linked.h"
#include "ast.h"
#include "lir.h"
#include "utils/linked.h"
#include "utils/slice.h"

typedef lir_operand_t *(*linear_expr_fn)(module_t *m, ast_expr_t expr);

void linear(module_t *m);

static void linear_body(module_t *m, slice_t *body);

static void linear_stmt(module_t *m, ast_stmt_t *stmt);

static lir_operand_t *linear_expr(module_t *m, ast_expr_t expr);

static void linear_assign(module_t *m, ast_assign_stmt_t *stmt);

static lir_operand_t *linear_zero_operand(module_t *m, type_t t);

#endif //NATURE_SRC_linear_H_
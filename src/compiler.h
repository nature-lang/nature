#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "utils/linked.h"
#include "ast.h"
#include "lir.h"
#include "utils/linked.h"
#include "utils/slice.h"

typedef lir_operand_t *(*compiler_expr_fn)(module_t *m, ast_expr expr);

void compiler(module_t *m);

static void compiler_block(module_t *m, slice_t *block);

static void compiler_stmt(module_t *m, ast_stmt *stmt);

static lir_operand_t *compiler_expr(module_t *m, ast_expr expr);

#endif //NATURE_SRC_COMPILER_H_

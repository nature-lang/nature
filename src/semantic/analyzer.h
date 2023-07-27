#ifndef NATURE_SRC_AST_ANALYZER_H_
#define NATURE_SRC_AST_ANALYZER_H_

#include "src/ast.h"
#include "src/symbol/symbol.h"
#include "src/module.h"

#define ANONYMOUS_FN_NAME "@lambda"

// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package as 确定使用 analyzer_main 还是 analyzer_module
 * @param t
 * @param stmt_list
 */
void analyzer(module_t *m, slice_t *stmt_list);

static void analyzer_expr(module_t *m, ast_expr_t *expr);

static void analyzer_stmt(module_t *m, ast_stmt_t *stmt);

static void analyzer_var_tuple_destr(module_t *m, ast_tuple_destr_t *tuple_destr);

#endif //NATURE_SRC_AST_ANALYZER_H_

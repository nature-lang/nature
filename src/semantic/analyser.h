#ifndef NATURE_SRC_AST_ANALYSER_H_
#define NATURE_SRC_AST_ANALYSER_H_

#include "src/ast.h"
#include "src/symbol/symbol.h"
#include "src/module.h"


#define ANONYMOUS_FN_NAME "_"


// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package as 确定使用 analyser_main 还是 analyser_module
 * @param t
 * @param stmt_list
 */
void analyser(module_t *m, slice_t *stmt_list);

static void analyser_expr(module_t *m, ast_expr *expr);

static void analyser_stmt(module_t *m, ast_stmt *stmt);


#endif //NATURE_SRC_AST_ANALYSER_H_

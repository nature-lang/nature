#ifndef NATURE_SRC_AST_ANALYZER_H_
#define NATURE_SRC_AST_ANALYZER_H_

#include "src/ast.h"
#include "src/module.h"
#include "src/symbol/symbol.h"


// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package as 确定使用 analyzer_main 还是 analyzer_module
 * @param t
 * @param stmt_list
 */
void analyzer(module_t *m, slice_t *stmt_list);

void analyzer_import(module_t *m, ast_import_t *import);

char *analyzer_force_unique_ident(module_t *m);

#endif//NATURE_SRC_AST_ANALYZER_H_

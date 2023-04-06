/**
 * 类型推导与判断
 */
#ifndef NATURE_SRC_AST_INFER_H_
#define NATURE_SRC_AST_INFER_H_

#include "src/ast.h"
#include "src/structs.h"

void infer(module_t *m);

static void infer_stmt(ast_stmt *stmt);

void infer_var_decl(ast_var_decl *var_decl);

static typeuse_t reduction_typeuse(module_t *m, typeuse_t t);

/**
 * struct 允许顺序不通，但是 key 和 code 需要相同，在还原时需要根据 key 进行排序
 * @param type
 * @return
 */
static typeuse_t infer_type(typeuse_t type);

#endif //NATURE_SRC_AST_INFER_H_

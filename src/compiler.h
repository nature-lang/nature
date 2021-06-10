#ifndef NATURE_SRC_COMPILER_H_
#define NATURE_SRC_COMPILER_H_

#include "src/lib/list.h"
#include "src/ast/ast.h"

// 入口
list *compiler(ast_closure_decl decl);

#endif //NATURE_SRC_COMPILER_H_

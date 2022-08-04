#include <stdio.h>
#include <stdlib.h>
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analysis.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "utils/helper.h"
//#include "src/ssa.h"
#include "src/debug/debug.h"
#include <unistd.h>

int main() {
    // 读取文件
    char *source = file_read("/home/vagrant/Code/nature/example/ssa.n");

    // scanner
    list *token_list = scanner(source);
    // parser
    ast_block_stmt stmt_list = parser(token_list);
    // analysis
    ast_closure_decl closure_decl = analysis(stmt_list);
    // infer 类型检查和推导
    infer(&closure_decl);

    // compiler to lir
    compiler_closures closures = compiler(&closure_decl);

    // construct cfg
    for (int i = 0; i < closures.count; ++i) {
        closure *c = closures.list[i];
        // 构造 cfg
        cfg(c);

        // 构造 ssa
//    ssa(c);

#ifdef DEBUG_CFG
        debug_cfg(closures.list[i]);
#endif
    }

    printf("Hello, World!\n");
    return 0;
}

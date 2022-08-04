#ifndef NATURE_TARGET_H
#define NATURE_TARGET_H

#include "syntax/scanner.h"
#include "syntax/parser.h"
#include "import.h"
#include "utils/list.h"
#include "value.h"

// 记录遍历过的 import(根据 full path 判断)

/**
 * Target district
 */
typedef struct {
    char *source; // 源文件
    string namespace;
    string package;

    scanner_cursor s_cursor;
    scanner_error s_error;
    list *token_list; // scanner 结果

    parser_cursor p_cursor;
    ast_block_stmt stmt_list;

    // analysis
    // TODO var_decls to init closures? 这样就只需要 compiler closures 就行了
    // 分析阶段(包括 closure 构建,全局符号表构建), 根据是否为 main 生成 import/symbol/var_decls(symbol)/closure_decls
    list *imports; // import_t, 图遍历 imports

    // 对外全局符号 -> 三种类型 var/fn/type_decl
    list *symbols; // symbol_t, 这里只存储全局符号

    list *ast_closure_decls; // 全局的或者非全局的都在这里了
} target_t;

#endif //NATURE_TARGET_H

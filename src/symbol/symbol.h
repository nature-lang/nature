#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include <stdlib.h>
#include "utils/helper.h"
#include "utils/table.h"
#include "utils/linked.h"
#include "src/ast.h"

#define FN_MAIN_NAME "main"
#define FN_INIT_NAME "init"
//#define BUILTIN_ERRORT "errort"
#define ENV_IDENT "env"

/**
 * 编译时产生的所有符号都进行唯一处理后写入到该 table 中
 * 1. 模块名 + fn名称
 * 2. 作用域不同时允许同名的符号(局部变量)，也进行唯一性处理
 *
 * 符号的来源有
 * 1. 局部变量与全局变量
 * 2. 函数
 * 3. 自定义 type, 例如 type foo = int
 */
table_t *symbol_table;

slice_t *symbol_fn_list;

slice_t *symbol_closure_list;

slice_t *symbol_var_list;

slice_t *symbol_typedef_list;

typedef enum {
    SYMBOL_VAR,
    SYMBOL_TYPE_ALIAS,
    SYMBOL_FN,
    SYMBOL_CLOSURE,
} symbol_type_t;

typedef struct {
    string ident; // 符号唯一标识
    bool is_local; // 对应 elf 符号中的 global/local, 表示能否被外部链接链接到
    symbol_type_t type;
    void *ast_value; // ast_typedef_stmt/ast_var_decl/ast_fndef_t/closure_t

    // 由于支持重载，所以会支持同名不同类型的函数, 但是由于在 analyzer 阶段类型仅仅收集了符号没有进行类型还原
    // 所以所有的同名全局函数都回注册到 fndefs 中
    slice_t *fndefs; // ast_fndef*
    int64_t ref_count; // 引用计数
} symbol_t;

static inline bool is_builtin_call(char *ident) {
    if (!ident) {
        return false;
    }

    return str_equal(ident, "print") ||
           str_equal(ident, "println") ||
           str_equal(ident, "list_push") ||
           str_equal(ident, "list_length") ||
           str_equal(ident, "map_delete") ||
           str_equal(ident, "map_length") ||
           str_equal(ident, "set_contains") ||
           str_equal(ident, "set_add") ||
           str_equal(ident, "set_delete");
}

symbol_t *symbol_table_set(char* ident, symbol_type_t type, void *ast_value, bool is_local);

symbol_t *symbol_table_get(char* ident);

symbol_t *symbol_table_get_noref(char* ident);

void symbol_table_delete(char* ident);

void symbol_table_set_var(char* unique_ident, type_t type);

void symbol_init();

#endif //NATURE_SRC_AST_SYMBOL_H_

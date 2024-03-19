#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include "src/ast.h"
#include "utils/helper.h"
#include "utils/linked.h"
#include "utils/table.h"
#include <stdlib.h>

#define FN_MAIN_NAME "main"
#define FN_INIT_NAME "init"
#define ANONYMOUS_FN_NAME "lambda"
#define FN_SELF_NAME "self"
#define ENV_IDENT "env"
#define TYPE_PARAM_T "T"
#define TYPE_PARAM_U "U"

#define TYPE_COROUTINE_T "coroutine_t"
#define TYPE_CHAN_T "chan_t"
#define TYPE_FUTURE_T "future_t"

#define MACRO_SIZEOF "sizeof"
#define MACRO_REFLECT_HASH "reflect_hash"
#define MACRO_CO_ASYNC "co_async"
#define MACRO_TYPE_EQ "type_eq"


// 临时表，用来临时记录, key = ident, value is any
extern table_t *can_import_symbol_table;

// key = temp_ident, value = table_t, 记录了 temp 中的所有符号
extern table_t *import_tpl_symbol_table;

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
extern table_t *symbol_table;

extern slice_t *symbol_fn_list;

extern slice_t *symbol_closure_list;

extern slice_t *symbol_var_list;

extern slice_t *symbol_typedef_list;

typedef enum {
    SYMBOL_VAR,
    SYMBOL_TYPE_ALIAS,
    SYMBOL_FN,
} symbol_type_t;

typedef struct {
    string ident; // 符号唯一标识
    bool is_local;// 对应 elf 符号中的 global/local, 表示能否被外部链接链接到
    symbol_type_t type;
    void *ast_value;  // ast_typedef_stmt/ast_var_decl/ast_fndef_t/closure_t
    int64_t ref_count;// 引用计数
} symbol_t;

static inline bool is_builtin_call(char *ident) {
    if (!ident) {
        return false;
    }

    return str_equal(ident, "print") ||
           //           str_equal(ident, "fib_test.mod.fib") ||
           str_equal(ident, "println");
}

symbol_t *symbol_table_set(char *ident, symbol_type_t type, void *ast_value, bool is_local);

symbol_t *symbol_table_get(char *ident);

symbol_t *symbol_table_get_noref(char *ident);

void symbol_table_delete(char *ident);

void symbol_table_set_var(char *unique_ident, type_t type);

void symbol_init();

#endif//NATURE_SRC_AST_SYMBOL_H_

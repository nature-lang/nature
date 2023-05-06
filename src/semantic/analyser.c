#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "utils/helper.h"
#include "analyser.h"
#include "utils/error.h"
#include "utils/table.h"
#include "utils/slice.h"
#include "src/symbol/symbol.h"
#include "src/debug/debug.h"

static void analyser_block(module_t *m, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
        // switch 结构导向优化
#ifdef DEBUG_ANALYSER
        debug_stmt("ANALYSER", block->tack[i]);
#endif
        analyser_stmt(m, block->take[i]);
    }
}

static type_t analyser_fndef_to_type(ast_fndef_t *fndef) {
    type_fn_t *f = NEW(type_fn_t);
    f->name = fndef->symbol_name;
    f->formal_types = ct_list_new(sizeof(type_t));
    f->return_type = fndef->return_type;
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var = ct_list_value(fndef->formals, i);
        ct_list_push(f->formal_types, &var->type);
    }
    f->rest = fndef->rest_param;
    type_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_UNDO;

    fndef->type = result;

    return result;
}

/**
 * type 不像 var 一样可以通过 env 引入进来，type 只有作用域的概念
 * 如果在当前文件的作用域中没有找到当前 type?
 * @param m
 * @param current
 * @param ident
 * @return
 */
static char *analyser_resolve_type(module_t *m, analyser_fndef_t *current, string ident) {
    slice_t *locals = m->analyser_current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];
        if (str_equal(ident, local->ident)) {
            assertf(local->type == SYMBOL_TYPEDEF, "ident=%s not type", local->ident);
            // 在 scope 中找到了该 type ident, 返回该 ident 的 unique_ident
            return local->unique_ident;
        }
    }

    if (current->parent == NULL) {
        // 当前 module 中同样有全局 type，在使用时是可以省略名称的
        // analyser 在初始化 module 时已经将这些符号全都注册到了全局符号表中 (module_ident + ident)
        char *global_ident = ident_with_module(m->ident, ident);
        symbol_t *s = table_get(symbol_table, global_ident);
        if (s != NULL) {
            return global_ident; // 完善 type 都访问名称
        }

        s = table_get(symbol_table, ident);
        if (s != NULL) {
            // 不需要改写使用的名称了
            return ident;
        }

        assertf(false, "type '%s' undeclared \n", ident);
    }

    return analyser_resolve_type(m, current->parent, ident);
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。只需要类型为 typedef ident 时能够定位唯一名称即可
 * @param type
 */
static void analyser_typeuse(module_t *m, type_t *type) {
    // 如果只是简单的 ident,又应该如何改写呢？
    // type foo = int
    // 'foo' is type_decl_ident
    if (type->kind == TYPE_IDENT) {
        type_ident_t *ident = type->ident;
        string unique_name = analyser_resolve_type(m, m->analyser_current, ident->literal);
        ident->literal = unique_name;
        return;
    }

    if (type->kind == TYPE_MAP) {
        type_map_t *map_decl = type->map;
        analyser_typeuse(m, &map_decl->key_type);
        analyser_typeuse(m, &map_decl->value_type);
        return;
    }

    if (type->kind == TYPE_SET) {
        type_set_t *set = type->set;
        analyser_typeuse(m, &set->element_type);
        return;
    }

    if (type->kind == TYPE_LIST) {
        type_list_t *list_decl = type->list;
        analyser_typeuse(m, &list_decl->element_type);
        return;
    }

    if (type->kind == TYPE_TUPLE) {
        type_tuple_t *tuple = type->tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *element_type = ct_list_value(tuple->elements, i);
            analyser_typeuse(m, element_type);
        }
        return;
    }

    if (type->kind == TYPE_FN) {
        type_fn_t *type_fn = type->fn;
        analyser_typeuse(m, &type_fn->return_type);
        for (int i = 0; i < type_fn->formal_types->length; ++i) {
            type_t *t = ct_list_value(type_fn->formal_types, i);
            analyser_typeuse(m, t);
        }
    }

    if (type->kind == TYPE_STRUCT) {
        type_struct_t *struct_decl = type->struct_;
        for (int i = 0; i < struct_decl->properties->length; ++i) {
            struct_property_t *item = ct_list_value(struct_decl->properties, i);
            analyser_typeuse(m, &item->type);

            // 可选右值解析
            if (item->right) {
                ast_expr *expr = item->right;
                if (expr->assert_type == AST_FNDEF) {
                    ast_fndef_t *fndef = expr->value;
                    fndef->self_struct = type;
                }

                analyser_expr(m, item->right);
            }
        }
    }
}

/**
 * code 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
local_ident_t *local_ident_new(module_t *m, symbol_type type, void *decl, string ident) {
    // unique ident
    string unique_ident = var_unique_ident(m, ident);

    local_ident_t *local = NEW(local_ident_t);
    local->ident = ident;
    local->unique_ident = unique_ident;
    local->depth = m->analyser_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    slice_push(m->analyser_current->locals, local);


    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl, true);

    return local;
}

/**
 * 块级作用域处理
 */
static void analyser_begin_scope(module_t *m) {
    m->analyser_current->scope_depth++;
}

static void analyser_end_scope(module_t *m) {
    m->analyser_current->scope_depth--;
    slice_t *locals = m->analyser_current->locals;
    while (locals->count > 0) {
        int index = locals->count - 1;
        local_ident_t *local = locals->take[index];
        if (local->depth <= m->analyser_current->scope_depth) {
            break;
        }


        if (local->is_capture) {
            slice_push(m->analyser_current->fndef->be_capture_locals, local);
        }


        // 从 locals 中移除该变量
        slice_remove(locals, index);
    }
}


static void analyser_if(module_t *m, ast_if_stmt *if_stmt) {
    analyser_expr(m, &if_stmt->condition);

    analyser_begin_scope(m);
    analyser_block(m, if_stmt->consequent);
    analyser_end_scope(m);

    analyser_begin_scope(m);
    analyser_block(m, if_stmt->alternate);
    analyser_end_scope(m);
}

static void analyser_assign(module_t *m, ast_assign_stmt *assign) {
    analyser_expr(m, &assign->left);
    analyser_expr(m, &assign->right);
}

static void analyser_throw(module_t *m, ast_throw_stmt *throw) {
    analyser_expr(m, &throw->error);
}

/**
 * 函数的递归自调用分为两种类型，一种是包含名称的函数递归自调用, 一种是不包含名称的函数递归自调用。
 * 对于第一种形式， global ident 一定能够找到该 fn 进行调用。
 *
 * 现在是较为复杂的第二种情况(self 关键字现在已经被占用了，所以无法使用),
 * 也就是闭包 fn 的自调用。这
 * @param m
 * @param call
 */
static void analyser_call(module_t *m, ast_call *call) {
    // 函数地址 unique 改写
    analyser_expr(m, &call->left);

    // 实参 unique 改写
    for (int i = 0; i < call->actual_params->length; ++i) {
        ast_expr *actual_param = ct_list_value(call->actual_params, i);
        analyser_expr(m, actual_param);
    }
}


/**
 * 检查当前作用域及当前 scope 是否重复定义了 ident
 * @param ident
 * @return
 */
static bool analyser_redeclare_check(module_t *m, char *ident) {
    if (m->analyser_current->scope_depth == 0) {
        return true;
    }

    // 从内往外遍历
    slice_t *locals = m->analyser_current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];

        // 如果找到了更高一级的作用域此时是允许重复定义变量的，直接跳过就行了
        if (local->depth != -1 && local->depth < m->analyser_current->scope_depth) {
            break;
        }

        if (str_equal(local->ident, ident)) {
            assertf(false, "line=%d, redeclare ident=%s", m->analyser_line, ident);
        }
    }

    return true;
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param var_decl
 */
static void analyser_var_decl(module_t *m, ast_var_decl *var_decl) {
    analyser_redeclare_check(m, var_decl->ident);

    analyser_typeuse(m, &var_decl->type);

    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, var_decl, var_decl->ident);

    // 改写
    var_decl->ident = local->unique_ident;
}

static void analyser_vardef(module_t *m, ast_vardef_stmt *stmt) {
    analyser_expr(m, &stmt->right);
    analyser_redeclare_check(m, stmt->var_decl.ident);
    analyser_typeuse(m, &stmt->var_decl.type);

    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, &stmt->var_decl, stmt->var_decl.ident);
    stmt->var_decl.ident = local->unique_ident;
}

static void analyser_var_tuple_destr(module_t *m, ast_tuple_destr *tuple_destr) {
    for (int i = 0; i < tuple_destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(tuple_destr->elements, i);
        // 要么是 ast_var_decl 要么是 ast_tuple_destr
        if (expr->assert_type == AST_VAR_DECL) {
            analyser_var_decl(m, expr->value);
        } else if (expr->assert_type == AST_EXPR_TUPLE_DESTR) {
            analyser_var_tuple_destr(m, expr->value);
        } else {
            assertf(false, "var tuple destr expr type exception");
        }
    }
}

/**
 * var (a, b, (c, d)) = xxxx
 * @param m
 * @param stmt
 */
static void analyser_var_tuple_destr_stmt(module_t *m, ast_var_tuple_def_stmt *stmt) {
    analyser_expr(m, &stmt->right);
    analyser_var_tuple_destr(m, stmt->tuple_destr);
}

static analyser_fndef_t *analyser_current_init(module_t *m, ast_fndef_t *fndef) {
    analyser_fndef_t *new = NEW(analyser_fndef_t);
    new->locals = slice_new();
    new->frees = slice_new();
    new->free_table = table_new();

    new->scope_depth = 0;

    fndef->capture_exprs = ct_list_new(sizeof(ast_expr));
    fndef->be_capture_locals = slice_new();
    new->fndef = fndef;

    // 继承关系
    new->parent = m->analyser_current;
    m->analyser_current = new;

    return m->analyser_current;
}

/**
 * 全局 fn (main/module fn) 到 name 已经提前加入到了符号表中。不需要重新调整，主要是对 param 和 body 进行处理即可
 * @param m
 * @param fndef
 */
static void analyser_global_fndef(module_t *m, ast_fndef_t *fndef) {
    analyser_current_init(m, fndef);
    analyser_typeuse(m, &fndef->return_type);
    analyser_begin_scope(m);
    // 函数形参处理
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *param = ct_list_value(fndef->formals, i);

        // type 中引用了 typedef ident 到话,同样需要更新引用
        analyser_typeuse(m, &param->type);

        // 注册(param->ident 此时已经随 param->type 一起写入到了 symbol 中)
        local_ident_t *param_local = local_ident_new(m, SYMBOL_VAR, param, param->ident);

        // 将 ast 中到 param->ident 进行改写
        param->ident = param_local->unique_ident;
    }
    analyser_block(m, fndef->body);
    analyser_end_scope(m);
    m->analyser_current = m->analyser_current->parent;
}


/**
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 *
 * fn<void(int, int)> foo = void (int a, int b) {
 *
 * }
 *
 * 函数此时有两个名称,所以需要添加两次符号表
 * 其中 foo 属于 var[code == fn]
 * bar 属于 label
 * fn(int, int) foo = fn bar(int a, int b) {
 *
 * }
 * @param fndef
 * @return
 */
static void analyser_local_fndef(module_t *m, ast_fndef_t *fndef) {
    // 更新 m->analyser_current
    analyser_current_init(m, fndef);

    // 函数名称重复声明检测,
    if (fndef->symbol_name == NULL) {
        fndef->symbol_name = ANONYMOUS_FN_NAME;
    } else {
        analyser_redeclare_check(m, fndef->symbol_name);
    }

    // 在内部作用域中已 symbol fn 的形式注册该 fn_name,顺便改写名称,由于不确定符号的具体类型，所以暂时不注册到符号表中
    string unique_ident = var_unique_ident(m, fndef->symbol_name);
    local_ident_t *fn_local = NEW(local_ident_t);
    fn_local->ident = fndef->symbol_name;
    fn_local->unique_ident = unique_ident;
    fn_local->depth = m->analyser_current->scope_depth;
    fn_local->decl = fndef;
    fn_local->type = SYMBOL_FN;
    slice_push(m->analyser_current->locals, fn_local);
    fndef->symbol_name = fn_local->unique_ident;

    analyser_typeuse(m, &fndef->return_type);
    // 开启一个新的 function 作用域
    analyser_begin_scope(m);

    // 函数形参处理
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *param = ct_list_value(fndef->formals, i);

        // type 中引用了 typedef ident 到话,同样需要更新引用
        analyser_typeuse(m, &param->type);

        // 注册(param->ident 此时已经随 param->type 一起写入到了 symbol 中)
        local_ident_t *param_local = local_ident_new(m, SYMBOL_VAR, param, param->ident);

        // 将 ast 中到 param->ident 进行改写
        param->ident = param_local->unique_ident;
    }

    // 分析请求体 block, 其中进行了对外部变量的捕获并改写, 以及 local var 名称 unique
    analyser_block(m, fndef->body);

    int free_var_count = 0;

    // m->analyser_current free > 0 表示当前函数引用了外部的环境，此时该函数将被编译成一个 closure
    for (int i = 0; i < m->analyser_current->frees->count; ++i) {
        // free 中包含了当前环境引用对外部对环境变量.这是基于定义域而不是调用栈对
        // 如果想要访问到当前 fndef, 则一定已经经过了当前 fndef 的定义域
        free_ident_t *free_var = m->analyser_current->frees->take[i];
        if (free_var->type != SYMBOL_VAR) { // fn label 可以直接全局访问到，不需要做 env assign
            continue;
        }
        free_var_count++;

        // 封装成 ast_expr 更利于 compiler
        ast_expr expr;
        // local 表示引用的 fn 是在 fndef->parent 的 local 变量，而不是自己的 local
        if (free_var->is_local) {
            // ast_ident 表达式
            expr.assert_type = AST_EXPR_IDENT;
            expr.value = ast_new_ident(free_var->ident);
        } else {
            // ast_env_index 表达式, 这里时 parent 再次引用了 parent env 到意思
            expr.assert_type = AST_EXPR_ENV_ACCESS;
            ast_env_access *env_access = NEW(ast_env_access);
            env_access->index = free_var->env_index;
            env_access->unique_ident = free_var->ident;
            expr.value = env_access;
        }

        ct_list_push(fndef->capture_exprs, &expr);
    }

    analyser_end_scope(m);

    // 退出当前 current
    m->analyser_current = m->analyser_current->parent;

    assert(m->analyser_current);

    // 如果当前函数是定层函数，退出后 m->analyser_current is null
    // 不过顶层函数也不存在 closure 引用的情况，直接注册到符号表中退出就行了
    if (free_var_count > 0) {
        fndef->closure_name = fndef->symbol_name;
        fndef->symbol_name = var_unique_ident(m, ANONYMOUS_FN_NAME); // 二进制中的 label name

        // 符号表内容修改为 var_decl
        ast_var_decl *var_decl = NEW(ast_var_decl);
        var_decl->type = analyser_fndef_to_type(fndef);
        var_decl->ident = fndef->closure_name;

        symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, true);
        symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, true);

        local_ident_t *parent_var_local = NEW(local_ident_t);
        parent_var_local->ident = fn_local->ident; // 原始名称不变
        parent_var_local->unique_ident = var_decl->ident;
        parent_var_local->depth = m->analyser_current->scope_depth;
        parent_var_local->decl = var_decl;
        parent_var_local->type = SYMBOL_VAR;
        slice_push(m->analyser_current->locals, parent_var_local);

    } else {
        // 符号就是普通的 symbol fn 符号，现在可以注册到符号表中了
        symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, true);

        // 仅加入到作用域，符号表就不需要重复添加了
        local_ident_t *parent_fn_local = NEW(local_ident_t);
        parent_fn_local->ident = fn_local->ident;
        parent_fn_local->unique_ident = fn_local->unique_ident;
        parent_fn_local->depth = m->analyser_current->scope_depth;
        parent_fn_local->decl = fn_local->decl;
        parent_fn_local->type = SYMBOL_FN;
        slice_push(m->analyser_current->locals, parent_fn_local);
    }
}

/**
 * @param current
 * @param is_local
 * @param index
 * @param ident
 * @return
 */
static int analyser_push_free(analyser_fndef_t *current, bool is_local, int index, string ident, symbol_type type) {
    // if exists, return exists index
    free_ident_t *free = table_get(current->free_table, ident);
    if (free) {
        return free->index;
    }

    // 新增 free
    free = NEW(free_ident_t);
    free->is_local = is_local;
    free->env_index = index;
    free->ident = ident;
    free->type = type;
    int free_index = slice_push(current->frees, free);
    free->index = free_index;
    table_set(current->free_table, ident, free);

    return free_index;
}

/**
 * 返回 top scope ident index
 * @param current
 * @param ident
 * @return
 */
static int8_t analyser_resolve_free(analyser_fndef_t *current, char **ident, symbol_type *type) {
    if (current->parent == NULL) {
        return -1;
    }

    slice_t *locals = current->parent->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(*ident, local->ident) == 0) {
            if (local->type == SYMBOL_VAR) {
                local->is_capture = true;
            }
            *ident = local->unique_ident;
            *type = local->type;
            return (int8_t) analyser_push_free(current, true, i, *ident, *type);
        }
    }


    // 一级 parent 没有找到，则继续向上 parent 递归查询
    int8_t parent_free_index = analyser_resolve_free(current->parent, ident, type);
    if (parent_free_index != -1) {
        // 在更高级的某个 parent 中找到了符号，则在 current 中添加对逃逸变量的引用处理
        return (int8_t) analyser_push_free(current, false, parent_free_index, *ident, *type);
    }

    return -1;
}

static void analyser_ident(module_t *m, ast_expr *expr) {
    ast_ident *temp = expr->value;
    // 避免如果存在两个位置引用了同一 ident 清空下造成同时改写两个地方的异常
    ast_ident *ident = NEW(ast_ident);
    ident->literal = temp->literal;
    expr->value = ident;

    // 在当前函数作用域中查找变量定义(local 是有清理逻辑的，一旦离开作用域就会被清理, 所以这里不用担心使用了下一级的 local)
    slice_t *locals = m->analyser_current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];
        if (str_equal(local->ident, ident->literal)) {
            ident->literal = local->unique_ident;
            return;
        }
    }

    // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
    symbol_type type = SYMBOL_VAR;
    int8_t free_var_index = analyser_resolve_free(m->analyser_current, &ident->literal, &type);
    if (free_var_index != -1) {
        // free 不一定是 var, 可能是 fn, 如果是 fn 只要改名了就行
        if (type != SYMBOL_VAR) {
            return;
        }

        // 如果使用的 ident 是逃逸的变量，则需要使用 access_env 代替
        // 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
        expr->assert_type = AST_EXPR_ENV_ACCESS;
        ast_env_access *env_access = NEW(ast_env_access);
        env_access->index = free_var_index;
        // 外部到 ident 可能已经修改了名字,这里进行冗于记录
        env_access->unique_ident = ident->literal;
        expr->value = env_access;
        return;
    }

    // 使用当前 module 中的全局符号是可以省略 module name 的, 但是当前 module 在注册 ident 时缺附加了 module.ident
    // 所以需要为 ident 添加上全局访问符号再看看能不能找到该 ident
    char *current_global_ident = ident_with_module(m->ident, ident->literal);
    symbol_t *s = table_get(symbol_table, current_global_ident);
    if (s != NULL) {
        ident->literal = current_global_ident; // 找到了则添加全局名称
        return;
    }

    // 最后还要判断是不是 println/print/set 等 builtin 类型的不带前缀的全局符号
    s = table_get(symbol_table, ident->literal);
    if (s != NULL) {
        // 不需要改写使用的名称了
        return;
    }

    assertf(false, "line=%d, identifier '%s' undeclared \n", expr->line, ident->literal);
}


static void analyser_access(module_t *m, ast_access *access) {
    analyser_expr(m, &access->left);
    analyser_expr(m, &access->key);
}

/*
 * 如果是 import_path.test 则进行符号改写, 改写成 namespace + module_name
 * struct.key , instance? 则不做任何对处理。
 */
static void analyser_select(module_t *m, ast_expr *expr) {
    ast_select *select = expr->value;
    if (select->left.assert_type == AST_EXPR_IDENT) {
        // 这里将全局名称改写后并不能直接去符号表中查找，但是此时符号可能还没有注册完成，所以不能直接通过 symbol table 查找到
        ast_ident *ident = select->left.value;
        ast_import *import = table_get(m->import_table, ident->literal);
        if (import) {
            // 这里直接将 module.select 改成了全局唯一名称，彻底消灭了select ！(不需要检测 import 服务是否存在，这在 linker 中会做的)
            char *unique_ident = ident_with_module(import->module_ident, select->key);
            expr->assert_type = AST_EXPR_IDENT;
            expr->value = ast_new_ident(unique_ident);
            return;
        }
    }

    analyser_expr(m, &select->left);
}

static void analyser_binary(module_t *m, ast_binary_expr *expr) {
    analyser_expr(m, &expr->left);
    analyser_expr(m, &expr->right);
}

static void analyser_unary(module_t *m, ast_unary_expr *expr) {
    analyser_expr(m, &expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
static void analyser_struct_new(module_t *m, ast_struct_new_t *expr) {
    analyser_typeuse(m, &expr->type);

    for (int i = 0; i < expr->properties->length; ++i) {
        struct_property_t *property = ct_list_value(expr->properties, i);
        analyser_expr(m, property->right);
    }
}

static void analyser_map_new(module_t *m, ast_map_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_map_element *element = ct_list_value(expr->elements, i);
        analyser_expr(m, &element->key);
        analyser_expr(m, &element->value);
    }
}


static void analyser_set_new(module_t *m, ast_set_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr *key = ct_list_value(expr->elements, i);
        analyser_expr(m, key);
    }
}

static void analyser_tuple_new(module_t *m, ast_tuple_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr *element = ct_list_value(expr->elements, i);
        analyser_expr(m, element);
    }
}

/**
 * tuple_destr = tuple_new
 * (a.b, a, (c, c.d)) = (1, 2)
 * @param m
 * @param expr
 */
static void analyser_tuple_destr(module_t *m, ast_tuple_destr *tuple) {
    for (int i = 0; i < tuple->elements->length; ++i) {
        ast_expr *element = ct_list_value(tuple->elements, i);
        analyser_expr(m, element);
    }
}


static void analyser_list_new(module_t *m, ast_list_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr *value = ct_list_value(expr->elements, i);
        analyser_expr(m, value);
    }
}

static void analyser_catch(module_t *m, ast_catch *expr) {
    analyser_call(m, expr->call);
}

static void analyser_for_cond(module_t *m, ast_for_cond_stmt *stmt) {
    analyser_expr(m, &stmt->condition);

    analyser_begin_scope(m);
    analyser_block(m, stmt->body);
    analyser_end_scope(m);
}

static void analyser_for_tradition(module_t *m, ast_for_tradition_stmt *stmt) {
    analyser_begin_scope(m);
    analyser_stmt(m, stmt->init);
    analyser_expr(m, &stmt->cond);
    analyser_stmt(m, stmt->update);
    analyser_block(m, stmt->body);
    analyser_end_scope(m);
}

static void analyser_for_iterator(module_t *m, ast_for_iterator_stmt *stmt) {
    // iterate 是对变量的使用，所以其在 scope
    analyser_expr(m, &stmt->iterate);

    analyser_begin_scope(m); // iterator 中创建的 key 和 value 的所在作用域都应该在当前 for scope 里面

    analyser_var_decl(m, &stmt->key);
    if (stmt->value) {
        analyser_var_decl(m, stmt->value);
    }
    analyser_block(m, stmt->body);

    analyser_end_scope(m);
}

static void analyser_return(module_t *m, ast_return_stmt *stmt) {
    if (stmt->expr != NULL) {
        analyser_expr(m, stmt->expr);
    }
}

// type foo = int
static void analyser_typedef(module_t *m, ast_typedef_stmt *stmt) {
    analyser_redeclare_check(m, stmt->ident);
    analyser_typeuse(m, &stmt->type);

    local_ident_t *local = local_ident_new(m, SYMBOL_TYPEDEF, stmt, stmt->ident);
    stmt->ident = local->unique_ident;
}


static void analyser_expr(module_t *m, ast_expr *expr) {
    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            return analyser_binary(m, expr->value);
        }
        case AST_EXPR_UNARY: {
            return analyser_unary(m, expr->value);
        }
        case AST_EXPR_CATCH: {
            return analyser_catch(m, expr->value);
        }
        case AST_EXPR_STRUCT_NEW: {
            return analyser_struct_new(m, expr->value);
        }
        case AST_EXPR_MAP_NEW: {
            return analyser_map_new(m, expr->value);
        }
        case AST_EXPR_SET_NEW: {
            return analyser_set_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_NEW: {
            return analyser_tuple_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_DESTR: {
            return analyser_tuple_destr(m, expr->value);
        }
        case AST_EXPR_LIST_NEW: {
            return analyser_list_new(m, expr->value);
        }
        case AST_EXPR_ACCESS: {
            return analyser_access(m, expr->value);
        }
        case AST_EXPR_SELECT: {
            // analyser 仅进行了变量重命名
            // 此时作用域不明确，无法进行任何的表达式改写。
            return analyser_select(m, expr);
        }
        case AST_EXPR_IDENT: {
            // ident unique 改写并注册到符号表中
            return analyser_ident(m, expr);
        }
        case AST_CALL: {
            return analyser_call(m, expr->value);
        }
        case AST_FNDEF: {
            slice_push(m->ast_fndefs, expr->value);
            return analyser_local_fndef(m, expr->value);
        }
        default:
            return;
    }
}

/**
 * 包含在 fndef body 中等各种表达式
 * @param m
 * @param stmt
 */
static void analyser_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return analyser_var_decl(m, stmt->value);
        }
        case AST_STMT_VAR_DEF: {
            return analyser_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return analyser_var_tuple_destr_stmt(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return analyser_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            slice_push(m->ast_fndefs, stmt->value);

            return analyser_local_fndef(m, stmt->value);
        }
        case AST_CALL: {
            return analyser_call(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return analyser_throw(m, stmt->value);
        }
        case AST_STMT_IF: {
            return analyser_if(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return analyser_for_cond(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return analyser_for_tradition(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return analyser_for_iterator(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return analyser_return(m, stmt->value);
        }
        case AST_STMT_TYPEDEF: {
            return analyser_typedef(m, stmt->value);
        }
        default: {
            return;
        }
    }
}

/**
 * 多个模块的解析
 * @param m
 * @param stmt_list
 */
static void analyser_module(module_t *m, slice_t *stmt_list) {
    // init
    m->analyser_line = 0;
    int import_end_index = 0;
    // import 统计
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            import_end_index = i;
            break;
        }

        ast_import *import = stmt->value;
        full_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
    }

    // var_decl blocks
    slice_t *var_assign_list = slice_new(); // 存放 stmt
    slice_t *fn_list = slice_new();

    // 跳过 import 语句开始计算, 不直接使用 analyser stmt, 因为 module 中不需要这么多表达式
    for (int i = import_end_index; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type == AST_VAR_DECL) {
            ast_var_decl *var_decl = stmt->value;
            var_decl->ident = ident_with_module(m->ident, var_decl->ident);

            symbol_t *s = symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_STMT_VAR_DEF) {
            ast_vardef_stmt *vardef = stmt->value;
            ast_var_decl *var_decl = &vardef->var_decl;
            var_decl->ident = ident_with_module(m->ident, var_decl->ident);
            symbol_t *s = symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, false);
            slice_push(m->global_symbols, s);

            // 转换成 assign stmt，然后导入到 init 中
            ast_stmt *temp_stmt = NEW(ast_stmt);
            ast_assign_stmt *assign = NEW(ast_assign_stmt);
            assign->left = (ast_expr) {
                    .assert_type = AST_EXPR_IDENT,
                    .value = ast_new_ident(var_decl->ident),
//                    .code = var_decl->code,
            };
            assign->right = vardef->right;
            temp_stmt->assert_type = AST_STMT_ASSIGN;
            temp_stmt->value = assign;
            slice_push(var_assign_list, temp_stmt);
            continue;
        }

        if (stmt->assert_type == AST_STMT_TYPEDEF) {
            ast_typedef_stmt *type_decl = stmt->value;
            type_decl->ident = ident_with_module(m->ident, type_decl->ident);
            symbol_t *s = symbol_table_set(type_decl->ident, SYMBOL_TYPEDEF, type_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = stmt->value;
            // 这里就可以看到 fn name 没有做唯一性处理，只是加了 ident 限定, 并加入到了全局符号表中
            fndef->symbol_name = ident_with_module(m->ident, fndef->symbol_name); // 全局函数改名
            symbol_t *s = symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, false);
            slice_push(m->global_symbols, s);
            slice_push(fn_list, fndef);
            continue;
        }

        assert(false && "[analyser_module] module stmt only support var_decl/new_fn/type_decl");
    }

    // 添加 init fn
    ast_fndef_t *fn_init = NEW(ast_fndef_t);
    fn_init->symbol_name = ident_with_module(m->ident, FN_INIT_NAME);
    fn_init->return_type = type_basic_new(TYPE_VOID);
    fn_init->formals = ct_list_new(sizeof(ast_var_decl));
    fn_init->body = var_assign_list;

    // 加入到全局符号表，等着调用就好了
    symbol_t *s = symbol_table_set(fn_init->symbol_name, SYMBOL_FN, fn_init, false);
    slice_push(m->global_symbols, s);
    slice_push(fn_list, fn_init);

    // 添加调用指令(后续 root module 会将这条指令添加到 main body 中)
    ast_stmt *call_stmt = NEW(ast_stmt);
    ast_call *call = NEW(ast_call);
    call->left = (ast_expr) {
            .assert_type = AST_EXPR_IDENT,
            .value = ast_new_ident(s->ident), // module.init
    };
    call->actual_params = ct_list_new(sizeof(ast_expr));
    call_stmt->assert_type = AST_CALL;
    call_stmt->value = call;
    m->call_init_stmt = call_stmt;

    // 此时所有对符号都已经主要到了全局变量表中，vardef 的右值则注册到了 fn.init 中，下面对 fndef 进行改写
    for (int i = 0; i < fn_list->count; ++i) {
        ast_fndef_t *fndef = fn_list->take[i];
        slice_push(m->ast_fndefs, fndef);
        analyser_global_fndef(m, fndef);
    }
}

/**
 * main.c 中定义的所有代码都会被丢到 main closure_t 中
 * @param t
 * @param stmt_list
 */
static void analyser_main(module_t *m, slice_t *stmt_list) {
    // 过滤处 import 部分, 其余部分再包到 main closure_t 中
    int import_last_index = -1;
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            break;
        }

        ast_import *import = stmt->value;
        full_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
        import_last_index = i;
    }

    m->analyser_line = 0;

    // main 所有表达式
    ast_fndef_t *fndef = NEW(ast_fndef_t);
    fndef->symbol_name = FN_MAIN_NAME;
    fndef->closure_name = NULL;
    fndef->body = slice_new();
    fndef->return_type = type_basic_new(TYPE_VOID);
    fndef->formals = ct_list_new(sizeof(ast_var_decl));
    for (int i = import_last_index + 1; i < stmt_list->count; ++i) {
        slice_push(fndef->body, stmt_list->take[i]);
    }

    // 符号表注册
    symbol_t *s = symbol_table_set(FN_MAIN_NAME, SYMBOL_FN, fndef, false);
    slice_push(m->global_symbols, s);

    // 先注册主 fndef
    slice_push(m->ast_fndefs, fndef);

    // 作为 fn 的 body 处理
    analyser_global_fndef(m, fndef);
}

void analyser(module_t *m, slice_t *stmt_list) {
    if (m->type == MODULE_TYPE_MAIN) {
        analyser_main(m, stmt_list);
    } else {
        analyser_module(m, stmt_list);
    }
}

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

/**
 * type 不像 var 一样可以通过 env 引入进来，type 只有作用域的概念
 * 如果在当前文件的作用域中没有找到当前 type?
 * @param m
 * @param current
 * @param ident
 * @return
 */
static char *analyser_resolve_type(module_t *m, analyser_fndef_t *current, string ident) {
    local_scope_t *current_scope = current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->idents->count; ++i) {
            local_ident_t *local = current_scope->idents->take[i];
            if (str_equal(ident, local->ident)) {
                assertf(local->type == SYMBOL_TYPEDEF, "ident=%s not type", local->ident);
                // 在 scope 中找到了该 type ident, 返回该 ident 的 unique_ident
                return local->unique_ident;
            }
        }

        current_scope = current_scope->parent;
    }

    if (current->parent == NULL) {
        // 当前 module 中同样有全局 type，在使用时是可以省略名称的
        // analyser 在初始化 module 时已经将这些符号全都注册到了全局符号表中 (module_ident + ident)
        char *global_ident = ident_with_module(m->ident, ident);
        symbol_t *s = table_get(symbol_table, global_ident);
        if (s != NULL) {
            return global_ident; // 完善 type 都访问名称
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
        analyser_typeuse(m, &set->key_type);
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

///**
// * @param name
// * @return
// */
//static char *analyser_unique_ident(module_t *m, char *name) {
//    char *unique_name = LIR_UNIQUE_NAME(name);
//    return ident_with_module(m->ident, unique_name);
//}

/**
 * TODO 不同 module 直接有同名的 ident 怎么处理？
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
    local->scope_depth = m->analyser_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    local_scope_t *current_scope = m->analyser_current->current_scope;
    slice_push(current_scope->idents, local);

    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl, true);

    return local;
}

static local_scope_t *local_scope_new(uint8_t scope_depth, local_scope_t *parent) {
    local_scope_t *new = malloc(sizeof(local_scope_t));
    new->idents = slice_new();
    new->scope_depth = scope_depth;
    new->parent = parent;
    return new;
}

/**
 * 块级作用域处理
 */
static void analyser_begin_scope(module_t *m) {
    m->analyser_current->scope_depth++;
    local_scope_t *current_scope = m->analyser_current->current_scope;
    m->analyser_current->current_scope = local_scope_new(m->analyser_current->scope_depth, current_scope);
}

static void analyser_end_scope(module_t *m) {
    local_scope_t *current_scope = m->analyser_current->current_scope;
    m->analyser_current->current_scope = current_scope->parent;
    m->analyser_current->scope_depth--;
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
    local_scope_t *current_scope = m->analyser_current->current_scope;
    for (int i = 0; i < current_scope->idents->count; ++i) {
        local_ident_t *local = current_scope->idents->take[i];
        if (strcmp(ident, local->ident) == 0) {
            error_redeclare_ident(m->analyser_line, ident);
            return false;
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
        } else if (expr->assert_type == AST_STMT_VAR_TUPLE_DESTR) {
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

static void analyser_fndef_name(module_t *m, ast_fndef_t *fndef) {
    // 仅 fun 再次定义 as 才需要再次添加到符号表
    if (!str_equal(fndef->name, FN_MAIN_NAME)) {
        if (strlen(fndef->name) == 0) {
            // 如果没有函数名称，则添加匿名函数名称(通过 module 唯一标识区分)
            fndef->name = ANONYMOUS_FN_NAME;
        } else {
            analyser_redeclare_check(m, fndef->name);
        }

        // 函数名称改写
        local_ident_t *local = local_ident_new(m, SYMBOL_FN, fndef, fndef->name);

        fndef->name = local->unique_ident;
    }
}

static void analyser_function_begin(module_t *m) {
    analyser_begin_scope(m);
}

static void analyser_function_end(module_t *m) {
    analyser_end_scope(m);

    m->analyser_current->current_scope->idents;
    // TODO 如果被下级捕获，则函数推出时，应该将捕获的相关变量 copy 到 heap 中，
    // 更新下一级中到 env 引用到值即可？
    // env 多级引用时？保存到是个啥？

    m->analyser_current = m->analyser_current->parent;
}

static analyser_fndef_t *analyser_current_init(module_t *m, char *fn_name) {
    analyser_fndef_t *new = malloc(sizeof(analyser_fndef_t));
    new->frees = slice_new();
    new->free_table = table_new();
    new->delay_fndefs = ct_list_new(sizeof(delay_fndef_t));
    new->scope_depth = 0;
    new->fn_name = fn_name;
    new->current_scope = NULL;

    // 继承关系
    new->parent = m->analyser_current;
    m->analyser_current = new;

    return m->analyser_current;
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
static void analyser_fndef(module_t *m, ast_fndef_t *fndef) {
    fndef->parent_view_envs = ct_list_new(sizeof(ast_expr));

    analyser_current_init(m, fndef->name);

    analyser_typeuse(m, &fndef->return_type);

    // 开启一个新的 function 作用域
    analyser_function_begin(m);

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

    // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
    // free 是在外部环境构建 env 的。
    // current_function->env_unique_name = unique_var_ident(ENV_IDENT);
//    closure->env_name = m->analyser_current->env_unique_name;

    // 构造 env (通过 analyser block, 当前 block 中引用的所有外部变量都被收集了起来)
    for (int i = 0; i < m->analyser_current->frees->count; ++i) {
        // free 中包含了当前环境引用对外部对环境变量.这是基于定义域而不是调用栈对
        // 如果想要访问到当前 fndef, 则一定已经经过了当前 fndef 的定义域
        free_ident_t *free_var = m->analyser_current->frees->take[i];

        // 封装成 ast_expr 更利于 compiler
        ast_expr expr;

        // 调用函数中引用的逃逸变量就在当前作用域中
        if (free_var->is_local) {
            // ast_ident 表达式
            expr.assert_type = AST_EXPR_IDENT;
            expr.value = ast_new_ident(free_var->ident);
        } else {
            // ast_env_index 表达式, 这里时 parent 再次引用了 parent env 到意思
            expr.assert_type = AST_EXPR_ENV_ACCESS;
            ast_env_access *env_access = NEW(ast_env_access);
            env_access->fn_name = m->analyser_current->parent->fn_name;
            env_access->index = free_var->env_index;
            env_access->unique_ident = free_var->ident;
            expr.value = env_access;
        }

        ct_list_push(fndef->parent_view_envs, &expr);
    }

    // 对当前 fndef 中对所有 sub fndef 进行 analyser
    for (int i = 0; i < m->analyser_current->delay_fndefs->length; ++i) {
        delay_fndef_t *d = ct_list_value(m->analyser_current->delay_fndefs, i);
        // 子 fn 注册
        // 将所有对 fndef 都加入到 flat fndefs 中, 且没有 parent 关系想关联。
        slice_push(m->ast_fndefs, d->fndef);

        analyser_fndef(m, d->fndef);
    }

    analyser_function_end(m); // 退出当前 current
}

/**
 * @param current
 * @param is_local
 * @param index
 * @param ident
 * @return
 */
static int analyser_push_free(analyser_fndef_t *current, bool is_local, int index, string ident) {
    // if exists, return exists index
    free_ident_t *free = table_get(current->free_table, ident);
    if (free) {
        return free->index;
    }

    // 新增 free
    free = NEW(local_ident_t);
    free->is_local = is_local;
    free->env_index = index;
    free->ident = ident;
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
int8_t analyser_resolve_free(analyser_fndef_t *current, string*ident) {
    if (current->parent == NULL) {
        return -1;
    }

    local_scope_t *scope = current->parent->current_scope;
    for (int i = 0; i < scope->idents->count; ++i) {
        local_ident_t *local = scope->idents->take[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(*ident, local->ident) == 0) {
            local->is_capture = true;
            *ident = local->unique_ident;
            return (int8_t) analyser_push_free(current, true, i, *ident);
        }
    }

    // 一级 parent 没有找到，则继续向上 parent 递归查询
    int8_t parent_free_index = analyser_resolve_free(current->parent, ident);
    if (parent_free_index != -1) {
        // 在更高级的某个 parent 中找到了符号，则在 current 中添加对逃逸变量的引用处理
        return (int8_t) analyser_push_free(current, false, parent_free_index, *ident);
    }

    return -1;
}

/**
 * @param expr
 */
static void analyser_ident(module_t *m, ast_expr *expr) {
    ast_ident *temp = expr->value;
    // 避免如果存在两个位置引用了同一 ident 清空下造成同时改写两个地方的异常
    ast_ident *ident = NEW(ast_ident);
    ident->literal = temp->literal;
    expr->value = ident;

    // 在当前函数作用域中查找变量定义
    local_scope_t *current_scope = m->analyser_current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->idents->count; ++i) {
            local_ident_t *local = current_scope->idents->take[i];
            if (str_equal(ident->literal, local->ident)) {
                // 在本地变量中找到,则进行简单改写 (从而可以在符号表中有唯一名称,方便定位)
                expr->value = ast_new_ident(local->unique_ident);
                return;
            }
        }
        current_scope = current_scope->parent;
    }

    // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
    int8_t free_var_index = analyser_resolve_free(m->analyser_current, &ident->literal);
    if (free_var_index != -1) {
        // 如果使用的 ident 是逃逸的变量，则需要使用 access_env 代替
        // 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
        expr->assert_type = AST_EXPR_ENV_ACCESS;
        ast_env_access *env_access = NEW(ast_env_access);
        env_access->fn_name = m->analyser_current->fn_name;
        env_access->index = free_var_index;
        // 外部到 ident 可能已经修改了名字,这里进行冗于记录
        env_access->unique_ident = ident->literal;
        expr->value = env_access;
        return;
    }



    // 当前 module 中的全局符号是可以省略 module name 的, 所以需要在当前 module 的全局符号(当前 module 注册的符号都加上了前缀)中查找
    // 所以这里要拼接前缀
    char *global_ident = ident_with_module(m->ident, ident->literal);
    symbol_t *s = table_get(symbol_table, global_ident);
    if (s != NULL) {
        ident->literal = global_ident; // 完善访问名称
        return;
    }

    // free_var_index == -1 表示符号在当前的作用域链中无法找到，可能是全局符号
    // println/print/set 等全局符号都已经预注册完成了
    s = table_get(symbol_table, ident->literal);
    if (s != NULL) {
        // 当前模块内都全局符号引用什么都不需要做，名称已经是正确的了
        return;
    }

    assertf(false, "ident not found line: %d, identifier '%s' undeclared \n", expr->line, ident->literal);
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
    if (select->left.assert_type != AST_EXPR_IDENT) {
        analyser_expr(m, &select->left);
        return;
    }
    ast_ident *ident = select->left.value;
    ast_import *import = table_get(m->import_table, ident->literal);
    if (import) {
        // 这里直接将 module.select 改成了全局唯一名称，彻底消灭了select ！(不需要检测 import 服务是否存在，这在 linker 中会做的)
        char *unique_ident = ident_with_module(import->module_ident, select->key);
        expr->assert_type = AST_EXPR_IDENT;
        expr->value = ast_new_ident(unique_ident);
        return;
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
    for (int i = 0; i < expr->keys->length; ++i) {
        ast_expr *key = ct_list_value(expr->keys, i);
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
    for (int i = 0; i < expr->values->length; ++i) {
        ast_expr *value = ct_list_value(expr->values, i);
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
            analyser_fndef_name(m, expr->value);

            // 函数体延迟到最后进行处理
            delay_fndef_t d = {
                    .fndef = expr->value,
                    .is_stmt = false,
//                    .scope = m->analyser_current->current_scope
            };
            ct_list_push(m->analyser_current->delay_fndefs, &d);

            break;
        }
        default:
            return;
    }
}


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
            // 主要是 fn_name unique 处理
            analyser_fndef_name(m, stmt->value);

            delay_fndef_t d = {
                    .fndef = stmt->value,
                    .is_stmt = true,
//                    .scope = m->analyser_current->current_scope
            };
            ct_list_push(m->analyser_current->delay_fndefs, &d);

            break;
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

    // 跳过 import 语句开始计算
    for (int i = import_end_index; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type == AST_VAR_DECL) {
            ast_var_decl *var_decl = stmt->value;
            char *ident = ident_with_module(m->ident, var_decl->ident);

            symbol_t *s = symbol_table_set(ident, SYMBOL_VAR, var_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_STMT_VAR_DEF) {
            ast_vardef_stmt *vardef = stmt->value;
            ast_var_decl *var_decl = &vardef->var_decl;
            char *ident = ident_with_module(m->ident, var_decl->ident);

            symbol_t *s = symbol_table_set(ident, SYMBOL_VAR, var_decl, false);
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
            char *ident = ident_with_module(m->ident, type_decl->ident);
            symbol_t *s = symbol_table_set(ident, SYMBOL_TYPEDEF, type_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = stmt->value;
            // 这里就可以看到 fn name 没有做唯一性处理，只是加了 ident 限定
            fndef->name = ident_with_module(m->ident, fndef->name); // 全局函数改名

            symbol_t *s = symbol_table_set(fndef->name, SYMBOL_FN, fndef, false);
            slice_push(m->global_symbols, s);
            slice_push(fn_list, fndef);
            continue;
        }

        assert(false && "[analyser_module] stmt.code not allow, must var_decl/new_fn/type_decl");
    }

    // 添加 init fn
    ast_fndef_t *fn_init = NEW(ast_fndef_t);
    fn_init->name = ident_with_module(m->ident, FN_INIT_NAME);
    fn_init->return_type = type_basic_new(TYPE_VOID);
    fn_init->formals = ct_list_new(sizeof(ast_var_decl));
    fn_init->body = var_assign_list;

    // 加入到全局符号表，等着调用就好了
    symbol_t *s = symbol_table_set(fn_init->name, SYMBOL_FN, fn_init, false);
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

    // 此时所有对符号都已经主要到了全局变量表中，vardef 到右值则注册到 init fn 中，有下面到 fndef 进行改写
    // 遍历所有 fn list
    for (int i = 0; i < fn_list->count; ++i) {
        ast_fndef_t *fndef = fn_list->take[i];
        analyser_fndef(m, fndef);

        slice_push(m->ast_fndefs, fndef);
    }
}

/**
 * main.c 中定义的所有代码都会被丢到 main closure_t 中
 * @param t
 * @param stmt_list
 */
static void analyser_main(module_t *m, slice_t *stmt_list) {
    // 过滤处 import 部分, 其余部分再包到 main closure_t 中
    int import_end_index = 0;
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

    // init
    m->analyser_line = 0;

    // main 包裹
    ast_fndef_t *fndef = malloc(sizeof(ast_fndef_t));
    fndef->name = FN_MAIN_NAME;
    fndef->body = slice_new();
    fndef->return_type = type_basic_new(TYPE_VOID);
    fndef->formals = ct_list_new(sizeof(ast_var_decl));
    slice_concat(fndef->body, stmt_list);

    // 符号表注册
    symbol_t *s = symbol_table_set(FN_MAIN_NAME, SYMBOL_FN, fndef, true);
    slice_push(m->global_symbols, s);

    // 先注册主 fndef
    slice_push(m->ast_fndefs, fndef);

    analyser_fndef(m, fndef);
}

void analyser(module_t *m, slice_t *stmt_list) {
    if (m->type == MODULE_TYPE_MAIN) {
        analyser_main(m, stmt_list);
    } else {
        analyser_module(m, stmt_list);
    }
}

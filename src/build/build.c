#include <stdio.h>
#include <unistd.h>
#include "build.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/lower/amd64/amd64.h"
#include "src/lower/lower.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/amd64/opcode.h"
#include "src/assembler/linux_elf/elf.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "src/build/cross.h"
#include "src/build/config.h"
#include "src/build/build.h"


// nature build xxx.n
void build(char *build_target) {
    env_init();
    config_init();

    // 初始化全局符号表
    symbol_ident_table_init();
    var_unique_count = 0;

    // 获取当前 pwd
    string source_path = str_connect(WORK_DIR, "/");
    source_path = str_connect(source_path, build_target);

    table *module_table = table_new();
    slice_t *module_list = slice_new();
    module_t *root = module_front_build(source_path, true);
    slice_push(module_list, root);


    slice_t *work_list = slice_new();
    slice_push(work_list, root);
    // 图遍历构造一组 path
    while (work_list->count > 0) {
        slice_t *temp_list = slice_new(); // 下一层级暂存
        for (int i = 0; i < work_list->count; ++i) {
            module_t *m = work_list->take[i];
            for (int j = 0; j < m->imports->count; ++j) {
                ast_import *import = m->imports->take[j];
                bool exist = table_exist(module_table, import->full_path);
                if (exist) {
                    continue;
                }

                module_t *new_m = module_front_build(import->full_path, false);
                slice_push(temp_list, new_m);
                slice_push(module_list, new_m);
                table_set(module_table, import->full_path, new_m);
            }
        }
        work_list = temp_list;
    }

    // TODO 暂时只支持单进程，因为多个文件共享了全局的数据
    // 全局维度初始化


    // root module stmt add call all module init
    ast_closure_t *root_ast_closure = root->ast_closures->take[0];
    for (int i = 1; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];
        if (m->call_init_stmt == NULL) {
            error_exit("[build] module %s not found init fn stmt", m->module_unique_name);
        }
        slice_t *temp = slice_new();
        slice_push(temp, m->call_init_stmt);
        slice_append_free(temp, root_ast_closure->function->body);
        root_ast_closure->function->body = temp;
    }

    // infer + compiler
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];


        var_unique_count = 0;
        lir_line = 0;
        m->compiler_closures = slice_new();

        // 全局符号的定义也需要推导以下原始类型
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            infer_var_decl(s->decl); // 类型还原
        }

        for (int j = 0; j < m->ast_closures->count; ++j) {
            ast_closure_t *closure = m->ast_closures->take[j];
            // 类型推断
            infer(closure);
            // 编译
            slice_append_free(m->compiler_closures, compiler(closure)); // 都写入到 compiler_closure 中了
        }

        // 构造 cfg, 并转成目标架构编码
        for (int j = 0; j < m->compiler_closures->count; ++j) {
            closure *c = m->compiler_closures->take[j];
            cfg(c);
#ifdef DEBUG_CFG
            debug_cfg(c);
#endif
        }
    }

    // lower + assembler
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];
        m->var_decl_list = list_new();

        // symbol to var_decl(架构无关), assembler 会使用 var_decl
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->decl;
            lower_var_decl_t *decl = NEW(lower_var_decl_t);
            decl->name = s->ident;
            decl->size = type_base_sizeof(var_decl->type.base);
            decl->value = NULL; // TODO 如果是立即数可以直接赋值
            list_push(m->var_decl_list, decl);
        }

        cross_lower(m);
        cross_assembler(m);
    }

    cross_linker(module_list);
}

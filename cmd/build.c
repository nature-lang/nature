#include "build.h"
#include "stdio.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/lower/amd64/amd64.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/amd64/opcode.h"
#include "src/assembler/elf/elf.h"

// nature build xxx.n
void build(string build_target) {
    // 初始化阶段
    symbol_ident_table_init();
    var_unique_count = 0;

    // 获取当前 pwd
    work_dir = get_work_dir();
    base_ns = parser_base_ns(work_dir);

    string source_path = str_connect(work_dir, "/");
    source_path = str_connect(source_path, build_target);

    table *module_table = table_new();
    slice_t *module_list = slice_new();
    module_t *root = module_new(source_path, true);
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

                module_t *new_m = module_new(import->full_path, false);
                slice_push(temp_list, new_m);
                slice_push(module_list, new_m);
                table_set(module_table, import->full_path, new_m);
            }
        }
        work_list = temp_list;
    }

    // TODO root module stmt add call all module init
    // TODO 暂时只支持单进程，因为多个文件共享了全局的数据
    // TODO 根据架构选择对应的 lower 入口
    // 全局维度初始化
    amd64_register_init();
    opcode_init();

    // infer 处理
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];

        // 文件维度初始化
        amd64_lower_init(); // 每个文件都需要清空 var_decl 表, 所以暂时不支持多线程
        var_unique_count = 0;
        lir_line = 0;
        m->compiler_closures = slice_new();
        m->asm_insts = list_new();
        m->var_decl_list = list_new();

        // 全局符号的定义也需要推导以下原始类型
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[i];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            infer_var_decl(s->decl); // 类型还原
        }

        for (int j = 0; j < m->ast_closures->count; ++j) {
            ast_closure *closure = m->ast_closures->take[j];
            // 类型推断
            infer(closure);
            slice_append(m->compiler_closures, compiler(closure)); // 都写入到 compiler_closure 中了
        }

        // 构造 cfg, 并转成目标架构编码
        for (int j = 0; j < m->compiler_closures->count; ++j) {
            closure *c = m->compiler_closures->take[j];
            cfg(c);
#ifdef DEBUG_CFG
            debug_cfg(c);
#endif
            // lower to asm insts
            list_append(m->asm_insts, amd64_lower_closure(c));
        }

        m->var_decl_list = amd64_decl_list;
        // symbol to var_decl
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[i];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->decl;
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = s->ident;
            decl->size = type_base_sizeof(var_decl->type.base);
            decl->value = NULL;
            list_push(m->var_decl_list, decl);
        }


        // 构造 elf
        elf_init(m->source_path);
        // 数据段遍历
        elf_var_decl_list_build(m->var_decl_list);
        // 代码段编译
        elf_text_inst_list_build(m->asm_insts);
        elf_text_inst_list_second_build();
        // 编码为 elf 二进制格式
        elf_t elf = elf_new(); // 基于全局变量
        m->elf_binary = elf_encoding(elf, &m->elf_count);
    }

    // 遍历 path 列表进行编译和目标文件生成(temp_dir)

    // 解析外挂 ld 进行连接(TODO runtime 在哪里?, binary_path)

    printf("hello in build");
}


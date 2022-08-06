#include "build.h"
#include "stdio.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"

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

    // infer 处理
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];
        for (int j = 0; j < m->ast_closures->count; ++j) {
            ast_closure *closure = m->ast_closures->take[j];
            infer(closure);
            compiler(m, closure); // 都写入到 compiler_closure 中了
        }

        // 构造 cfg
        for (int j = 0; j < m->compiler_closures->count; ++j) {
            closure *c = m->compiler_closures->take[j];
            cfg(c);
#ifdef DEBUG_CFG
            debug_cfg(c);
#endif
        }
    }

    // 遍历 path 列表进行编译和目标文件生成(temp_dir)

    // 解析外挂 ld 进行连接(TODO runtime 在哪里?, binary_path)

    printf("hello in build");
}


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
#include "src/assembler/amd64/register.h"
#include "src/assembler/amd64/opcode.h"
#include "src/assembler/linux_elf/elf.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "src/build/cross.h"

#define LINUX_BUILD_DIR  "/tmp/nature-build.XXXXXX"

char *lib_dir = "/home/vagrant/Code/nature/debug/lib/linux_amd64";

char *ld_path = "/usr/bin/ld";
char *output_name = "main";

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
    // TODO 根据架构选择对应的 lower 入口
    // 全局维度初始化
    amd64_register_init();
    opcode_init();

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

    // 文件维度编译，最终会生成 elf_binary target
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
            // lower to asm insts
            list_append(m->asm_insts, amd64_lower_closure(c));
        }


        // TODO 按架构编译

        m->var_decl_list = amd64_decl_list;
        // symbol to var_decl
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->decl;
            amd64_asm_var_decl *decl = NEW(amd64_asm_var_decl);
            decl->name = s->ident;
            decl->size = type_base_sizeof(var_decl->type.base);
            decl->value = NULL;
            list_push(m->var_decl_list, decl);
        }
    }

    // 遍历 path 列表进行编译和目标文件生成(temp_dir)
    char build_dir[] = LINUX_BUILD_DIR;
    char *temp_dir = mkdtemp(build_dir);
    if (temp_dir == NULL) {
        error_exit("[build] mk temp dir failed");
    }

    // ld
    slice_t *ld_params = slice_new();
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];

        // 写入到 tmp 目录
        char *file = file_join(temp_dir, m->linker_file_name);
        linux_elf_to_file(m->elf_binary, m->elf_count, file);

        // 工作目录再 temp_dir 中,所以之类使用相对路径即可
        slice_push(ld_params, m->linker_file_name);
    }

    slice_push(ld_params, "-Bstatic"); // 静态链接
    slice_push(ld_params, "-nostdinc"); // 忽略标准库头文件
    slice_push(ld_params, "-nostdlib"); // 忽略标准库
    // 引入标准库
    char *libstart_path = file_join(lib_dir, "crt1.o");
    char *runtime_path = file_join(lib_dir, "libruntime.a");
    char *libc_path = file_join(lib_dir, "libc.a");
    slice_push(ld_params, runtime_path);
    slice_push(ld_params, libstart_path);
    slice_push(ld_params, libc_path);

    slice_push(ld_params, "-o"); // 输出名称
    slice_push(ld_params, "a.out"); // 固定输出名称

    // 解析外挂 ld 进行连接
    exec(temp_dir, "ld", ld_params);

    // copy 移动
    char *src_path = file_join(temp_dir, "a.out");
    char *dst_path = file_join(work_dir, output_name);
    copy(dst_path, src_path, 0755);

//    printf("hello in build,\n temp_dir: %s\n work_dir: %s", temp_dir, work_dir);
}

void build_arg(int argc, char **argv) {
    char *build_file = argv[argc - 1];
    if (!ends_with(build_file, ".n")) {
        error_exit("[build_arg] named files must be .n files: %s", build_file);
        return;
    }

    // -o 参数解析
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        switch (c) {
            case 'o':
                output_name = optarg;
                break;
        }
    }

    build(build_file);
}


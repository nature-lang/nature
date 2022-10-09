#include "build.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/native/amd64/amd64.h"
#include "src/native/native.h"
#include "src/register/register.h"
#include "src/binary/opcode/amd64/opcode.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/output.h"
#include "src/ssa.h"
#include "utils/error.h"
#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/**
 * base on ${NATURE_ROOT}/lib/${BUILD_OS}_${BUILD_ARCH} + file
 * @return
 */
static char *lib_file_path(char *file) {
    char *_os_arch = str_connect(os_to_string(BUILD_OS), "_");
    _os_arch = str_connect(_os_arch, arch_to_string(BUILD_ARCH));

    char *dir = file_join(NATURE_ROOT, "lib");
    dir = file_join(dir, _os_arch);
    return file_join(dir, file);
}

/**
 * @param m
 * @return  m->opcodes
 */
void cross_native(module_t *m) {
    native_var_decls = slice_new();
    m->operations = slice_new();

    // pre
    register_init();
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_opcode_init();
    }

    // native by closure
    for (int i = 0; i < m->compiler_closures->count; ++i) {
        closure *c = m->compiler_closures->take[i];
        if (BUILD_ARCH == ARCH_AMD64) {
            slice_t *operands = amd64_native_closure(c);
            slice_append(m->operations, operands);
        }
    }

    // post
    slice_append(m->var_decls, native_var_decls);

    return;
    ERROR:
    error_exit("[cross_native] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
}

/**
 * 汇编器目前只支持 linux elf amd64
 * @param m
 */
void build_assembler(module_t *m) {
    if (BUILD_OS == OS_LINUX) {
        char *object_file_name = str_connect(m->module_unique_name, ".n.o");
        str_replace_char(object_file_name, '/', '.');

        char *output = file_join(TEMP_DIR, object_file_name);
        elf_context *ctx = elf_context_new(output, OUTPUT_OBJECT);
        linkable_object_format(ctx, m->operations, m->var_decls);
        elf_output(ctx);

        // 完整输出路径
        printf(" --> assembler: %s\n", output);
        m->object_file = output;
    } else {
        goto ERROR;
    }

    return;
    ERROR:
    error_exit("[build_assembler] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
}

/**
 * modules module_list
 * @param module_list
 */
void build_linker(slice_t *module_list) {
    // 检测是否生成
    int fd;
    char *output = file_join(TEMP_DIR, LINKER_OUTPUT);
    elf_context *ctx = elf_context_new(output, OUTPUT_EXECUTABLE);

    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];

        fd = open(m->object_file, O_RDONLY | O_BINARY);
        elf_load_object_file(ctx, fd, 0);
    }

    // crt1.o
    fd = open(lib_file_path(LIB_START_FILE), O_RDONLY | O_BINARY);
    elf_load_object_file(ctx, fd, 0);

    // libruntime.a
    fd = open(lib_file_path(LIB_RUNTIME_FILE), O_RDONLY | O_BINARY);
    elf_load_archive(ctx, fd);

    // libc.a
    fd = open(lib_file_path(LIB_C_FILE), O_RDONLY | O_BINARY);
    elf_load_archive(ctx, fd);

    executable_file_format(ctx);

    elf_output(ctx);
    if (!file_exists(output)) {
        error_exit("[build_linker] linker failed");
    }
    char *dst_path = file_join(WORK_DIR, BUILD_OUTPUT);
    copy(dst_path, output, 0755);
    printf("linker output--> %s\n", output);
    printf("build output--> %s\n", dst_path);
}

/**
 * nature build main.n, build_entry = "main.n"
 * nature build client/main.n build_entry = "client/main.n"
 * @param build_entry
 */
void build(char *build_entry) {
    env_init();
    // 获取当前 pwd
    config_init();
    string source_path = file_join(WORK_DIR, build_entry);

    printf("NATURE_ROOT: %s\n", NATURE_ROOT);
    printf("BUILD_OS: %s\n", os_to_string(BUILD_OS));
    printf("BUILD_ARCH: %s\n", arch_to_string(BUILD_ARCH));
    printf("BUILD_OUTPUT: %s\n", BUILD_OUTPUT);
    printf("WORK_DIR: %s\n", WORK_DIR);
    printf("BASE_NS: %s\n", BASE_NS);
    printf("TERM_DIR: %s\n", TEMP_DIR);
    printf("build_entry: %s\n", build_entry);
    printf("source_path: %s\n", source_path);

    // 初始化全局符号表
    symbol_ident_table_init();
    var_unique_count = 0;

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
            // 构造 ssa
            ssa(c);
#ifdef DEBUG_CFG
            debug_cfg(c);
#endif
        }
    }

    // native + assembler
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];
        // 首次初始化符号定义
        m->var_decls = slice_new();

        // symbol to var_decl(架构无关), assembler 会使用 var_decl
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->decl;
            native_var_decl_t *decl = NEW(native_var_decl_t);
            decl->name = s->ident;
            decl->size = type_base_sizeof(var_decl->type.base);
            decl->value = NULL; // TODO 如果是立即数可以直接赋值
            slice_push(m->var_decls, decl);
        }

        cross_native(m);
        build_assembler(m);
    }

    // 链接
    build_linker(module_list);
}

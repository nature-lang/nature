#include "build.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/native/amd64.h"
#include "src/native/native.h"
#include "src/register/register.h"
#include "src/binary/opcode/amd64/opcode.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/output.h"
#include "src/ssa.h"
#include "src/lower/lower.h"
#include "src/register/linearscan.h"
#include "src/binary/opcode/opcode.h"
#include "utils/error.h"
#include "config.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

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
 * 汇编器目前只支持 linux elf amd64
 * @param m
 */
static void assembler(module_t *m) {
    if (BUILD_OS == OS_LINUX) {
        char *object_file_name = str_connect(m->ident, ".n.o");
        str_replace_char(object_file_name, '/', '.');

        char *output = file_join(TEMP_DIR, object_file_name);
        elf_context *ctx = elf_context_new(output, OUTPUT_OBJECT);
        // TODO 基于 closures asm_operations 进行 encoding
        for (int i = 0; i < m->closures->count; ++i) {
            closure_t *c = m->closures->take[i];
            linkable_object_load_closure(ctx, c);
        }
        // 输出目标文件
        elf_output(ctx);

        // 完整输出路径
        printf(" --> assembler: %s\n", output);
        m->object_file = output;
        return;
    }

    assert(false && dsprintf("unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH));
}

/**
 * modules module_list
 * @param module_list
 */
static void linker(slice_t *module_list) {
    // 检测是否生成
    int fd;
    char *output = file_join(TEMP_DIR, LINKER_OUTPUT);
    elf_context *ctx = elf_context_new(output, OUTPUT_EXECUTABLE);

    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];

        fd = open(m->object_file, O_RDONLY | O_BINARY);
        elf_load_object_file(ctx, fd, 0); // 加载并解析目标文件
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

    // load rtype,fndef,symdef and add symbol

    executable_file_format(ctx);

    elf_output(ctx);
    if (!file_exists(output)) {
        error_exit("[linker] linker failed");
    }

    copy(BUILD_OUTPUT, output, 0755);
    printf("linker output--> %s\n", output);
    printf("build output--> %s\n", BUILD_OUTPUT);
}

/**
 * nature build main.n, build_entry = "main.n"
 * nature build client/main.n build_entry = "client/main.n"
 * @param build_entry
 */
void build(char *build_entry) {
    assert(strlen(build_entry) > 2 && "build entry exception");

    // 全局 init
    env_init();
    config_init();
    symbol_init();
    reg_init();
    opcode_init();

    string source_path = file_join(WORK_DIR, build_entry);
    printf("NATURE_ROOT: %s\n", NATURE_ROOT);
    printf("BUILD_OS: %s\n", os_to_string(BUILD_OS));
    printf("BUILD_ARCH: %s\n", arch_to_string(BUILD_ARCH));
    printf("BUILD_OUTPUT_NAME: %s\n", BUILD_OUTPUT_NAME);
    printf("BUILD_OUTPUT_DIR: %s\n", BUILD_OUTPUT_DIR);
    printf("BUILD_OUTPUT: %s\n", BUILD_OUTPUT);
    printf("WORK_DIR: %s\n", WORK_DIR);
    printf("BASE_NS: %s\n", BASE_NS);
    printf("TERM_DIR: %s\n", TEMP_DIR);
    printf("build_entry: %s\n", build_entry);
    printf("source_path: %s\n", source_path);


    table_t *module_table = table_new();
    slice_t *modules = slice_new();
    module_t *root = module_build(source_path, true);
    slice_push(modules, root);

    list *work_list = list_new();
    list_push(work_list, root);
    // 图遍历构造一组 path
    while (work_list->count > 0) {
        module_t *m = list_pop(work_list);;
        for (int j = 0; j < m->imports->count; ++j) {
            ast_import *import = m->imports->take[j];
            if (table_exist(module_table, import->full_path)) {
                continue;
            }

            module_t *new_module = module_build(import->full_path, false);
            list_push(work_list, new_module);
            slice_push(modules, new_module);
            table_set(module_table, import->full_path, new_module);
        }
    }

    // root module stmt add call all module init for all module tree
    ast_closure_t *root_ast_closure = root->ast_closures->take[0];
    for (int i = 1; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        assertf(m->call_init_stmt != NULL, "module %s not found init fn stmt", m->ident);

        slice_t *temp = slice_new();
        slice_push(temp, m->call_init_stmt);
        slice_concat_free(temp, root_ast_closure->fn->body);
        root_ast_closure->fn->body = temp;
    }

    // infer + compiler
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        lir_init();

        // 全局符号的定义也需要推导以下原始类型
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            infer_var_decl(s->ast_value); // 类型还原
        }

        for (int j = 0; j < m->ast_closures->count; ++j) {
            ast_closure_t *ast_closure = m->ast_closures->take[j];
            // 类型推断
            infer(ast_closure);
            // 编译
            slice_concat_free(m->closures, compiler(m, ast_closure)); // 都写入到 compiler_closure 中了
        }

        // 构造 cfg, 并转成目标架构编码
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];

            cfg(c);

            // 构造 ssa
            ssa(c);

            debug_lir(c);

            lower(c);

            linear_scan(c);

            debug_lir(c);

            native(c);

            debug_asm(c);
        }
    }

    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // symbol to var_decl(架构无关), assembler 会使用 var_decl
        // 由全局符号产生
        for (int j = 0; j < m->symbols->count; ++j) {
            symbol_t *s = m->symbols->take[j];
            if (s->type != SYMBOL_TYPE_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->ast_value;
            asm_var_decl_t *decl = NEW(asm_var_decl_t);
            decl->name = s->ident;
            decl->size = type_kind_sizeof(var_decl->type.kind);
            decl->value = NULL; // TODO 如果是立即数可以直接赋值
            slice_push(m->asm_var_decls, decl);
        }

        // 合并 closure 中的临时 var_decl
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            slice_concat(m->asm_var_decls, c->asm_var_decls);
            slice_concat(m->asm_operations, c->asm_operations);
        }

        assembler(m);
    }

    // 链接
    linker(modules);
}

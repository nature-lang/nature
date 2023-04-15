#include "build.h"
#include "src/cross.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/native/amd64.h"
#include "src/native/native.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/output.h"
#include "src/ssa.h"
#include "src/register/linearscan.h"
#include "utils/error.h"
#include "config.h"
#include "utils/links.h"

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

    char *dir = path_join(NATURE_ROOT, "lib");
    dir = path_join(dir, _os_arch);
    return path_join(dir, file);
}

/**
 * 汇编器目前只支持 linux elf amd64
 * @param m
 */
static void assembler_module(module_t *m) {
    if (BUILD_OS == OS_LINUX) { // elf 就是 linux 独有都
        char *object_file_name = str_connect(m->ident, ".n.o");
        str_replace_char(object_file_name, '/', '.');

        char *output = path_join(TEMP_DIR, object_file_name);
        elf_context *ctx = elf_context_new(output, OUTPUT_OBJECT);

        // 将全局变量写入到数据段或者符号表 (这里应该叫 global var)
        object_load_symbols(ctx, m->asm_global_symbols);

        for (int i = 0; i < m->closures->count; ++i) {
            closure_t *c = m->closures->take[i];
            object_load_operations(ctx, c);
        }
        // 输出目标文件
        elf_output(ctx);

        // 完整输出路径
        DEBUGF(" --> assembler: %s\n", output);
        m->object_file = output;
        return;
    }

    assert(false && dsprintf("unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH));
}

/**
 * modules modules
 * @param modules
 */
static void build_linker(slice_t *modules) {
    // 检测是否生成
    int fd;
    char *output = path_join(TEMP_DIR, LINKER_OUTPUT);
    elf_context *ctx = elf_context_new(output, OUTPUT_EXECUTABLE);

    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        fd = check_open(m->object_file, O_RDONLY | O_BINARY);
        elf_load_object_file(ctx, fd, 0); // 加载并解析目标文件
    }

    // crt1.o (包含入口 label _start, 其进行初始化后会调用 main label)
    fd = check_open(lib_file_path(LIB_START_FILE), O_RDONLY | O_BINARY);
    elf_load_object_file(ctx, fd, 0);

    // libruntime.a
    fd = check_open(lib_file_path(LIB_RUNTIME_FILE), O_RDONLY | O_BINARY);
    elf_load_archive(ctx, fd);

    // libucontext.a
    fd = check_open(lib_file_path(LIBUCONTEXT_FILE), O_RDONLY | O_BINARY);
    elf_load_archive(ctx, fd);

    // libc.a (runtime 依赖了 c 标准库)
    fd = check_open(lib_file_path(LIBC_FILE), O_RDONLY | O_BINARY);
    elf_load_archive(ctx, fd);


    executable_file_format(ctx);

    elf_output(ctx);
    if (!file_exists(output)) {
        error_exit("[linker] linker failed");
    }

    copy(BUILD_OUTPUT, output, 0755);
    printf("linker output--> %s\n", output);
    printf("build output--> %s\n", BUILD_OUTPUT);
}

static void build_init(char *build_entry) {
    env_init();
    config_init();
    symbol_init();
    cross_reg_init();
    cross_opcode_init();

    // 全局 init
    BUILD_ENTRY = build_entry;
    SOURCE_PATH = path_join(WORK_DIR, BUILD_ENTRY);

    // type ct_rtype_table
    ct_rtype_table = table_new();
    ct_rtype_list = ct_list_new(sizeof(rtype_t));
    ct_rtype_data = NULL;
    ct_rtype_count = 0;
    ct_rtype_size = 0;
}

static void config_print() {
    DEBUGF("NATURE_ROOT: %s", NATURE_ROOT);
    DEBUGF("BUILD_OS: %s", os_to_string(BUILD_OS));
    DEBUGF("BUILD_ARCH: %s", arch_to_string(BUILD_ARCH));
    DEBUGF("BUILD_OUTPUT_NAME: %s", BUILD_OUTPUT_NAME);
    DEBUGF("BUILD_OUTPUT_DIR: %s", BUILD_OUTPUT_DIR);
    DEBUGF("BUILD_OUTPUT: %s", BUILD_OUTPUT);
    DEBUGF("WORK_DIR: %s", WORK_DIR);
    DEBUGF("BASE_NS: %s", BASE_NS);
    DEBUGF("TERM_DIR: %s", TEMP_DIR);
    DEBUGF("BUILD_ENTRY: %s", BUILD_ENTRY);
    DEBUGF("SOURCE_PATH: %s", SOURCE_PATH);
}

static void build_assembler(slice_t *modules) {
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // symbol to var_decl(架构无关), assembler 会使用 var_decl
        // 由全局符号产生
        for (int j = 0; j < m->global_symbols->count; ++j) {
            symbol_t *s = m->global_symbols->take[j];
            if (s->type != SYMBOL_VAR) {
                continue;
            }
            ast_var_decl *var_decl = s->ast_value;
            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            symbol->name = s->ident;
            symbol->size = type_kind_sizeof(var_decl->type.kind);
            symbol->value = NULL;
            slice_push(m->asm_global_symbols, symbol);
        }

        // native closure，如果遇到 c_string, 需要在 symtab + data 中注册一条记录，然后在 .text 引用，
        // 所以有了这里的临时 closure var decls, 原则上， var_decl = global var，其和 module 挂钩
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            slice_concat(m->asm_global_symbols, c->asm_symbols);
            slice_concat(m->asm_operations, c->asm_operations);
        }

        assembler_module(m);
    }
}

static void import_builtin() {
    // nature_root
    char *source_path = path_join(NATURE_ROOT, "std/builtin/builtin.n");
    assertf(file_exists(source_path), "builtin.n not found");
    // build 中包含 analyser 已经将相关 symbol 写入了, 无论是后续都 analyser 或者 infer 都能够使用
    module_build(source_path, MODULE_TYPE_BUILTIN);
}

static slice_t *build_modules() {
    assertf(strlen(SOURCE_PATH) > 0, "SOURCE_PATH empty");

    table_t *module_table = table_new();
    slice_t *modules = slice_new();
    module_t *root = module_build(SOURCE_PATH, true);
    slice_push(modules, root);

    linked_t *work_list = linked_new();
    linked_push(work_list, root);

    while (work_list->count > 0) {
        module_t *m = linked_pop(work_list);;
        for (int j = 0; j < m->imports->count; ++j) {
            ast_import *import = m->imports->take[j];
            if (table_exist(module_table, import->full_path)) {
                continue;
            }

            module_t *module_new = module_build(import->full_path, false);
            linked_push(work_list, module_new);
            slice_push(modules, module_new);
            table_set(module_table, import->full_path, module_new);
        }
    }

    // 将所有模块都 init 函数都注册到 root body 的最前面
    ast_fndef_t *root_fndef = root->ast_fndefs->take[0];
    for (int i = 1; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        assertf(m->call_init_stmt != NULL, "module %s not found init fn stmt", m->ident);

        slice_t *body = slice_new();
        slice_push(body, m->call_init_stmt);
        slice_concat_free(body, root_fndef->body);
        root_fndef->body = body;
    }

    return modules;
}

static void build_compiler(slice_t *modules) {
    // infer + compiler
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // 全局符号的定义也需要推导以下原始类型
        for (int j = 0; j < m->global_symbols->count; ++j) {
            symbol_t *s = m->global_symbols->take[j];
            if (s->type != SYMBOL_VAR) {
                continue;
            }
            infer_var_decl(m, s->ast_value); // 类型还原
        }

        // 类型推断
        infer(m);

        compiler(m);


        // 构造 cfg, 并转成目标架构编码
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];

            cfg(c);

            // 构造 ssa
            ssa(c);

            debug_lir(c);

            cross_lower(c);

            linear_scan(c);

            debug_lir(c);

            cross_native(c);

            debug_asm(c);
        }
    }
}

/**
 * nature build main.n, build_entry = "main.n"
 * nature build client/main.n build_entry = "client/main.n"
 * @param build_entry
 */
void build(char *build_entry) {
    assert(strlen(build_entry) > 2 && "build entry exception");

    // 配置初始化
    build_init(build_entry);

    // debug
    config_print();

    // 前端处理
    import_builtin();

    slice_t *modules = build_modules();

    // 编译
    build_compiler(modules);

    // 汇编
    build_assembler(modules);

    // 链接
    build_linker(modules);
}

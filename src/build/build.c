#include "build.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/output.h"
#include "src/cfg.h"
#include "src/cross.h"
#include "src/debug/debug.h"
#include "src/linear.h"
#include "src/native/amd64.h"
#include "src/register/linearscan.h"
#include "src/semantic/analyzer.h"
#include "src/semantic/infer.h"
#include "src/ssa.h"
#include "utils/custom_links.h"
#include "utils/error.h"

// char*, 支持 .o 或者 .a 文件后缀
static slice_t *linker_libs;

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

static char *custom_link_object_path() {
    char *output = path_join(TEMP_DIR, "custom_links.n.o");
    return output;
}

/**
 * to custom_link.n.o
 * .data
 * .symbol
 * .data.rtype
 * .data.fndef
 * .data.symdef
 */
static void assembler_custom_links() {
    assertf(BUILD_OS == OS_LINUX, "only support built to linux");
    elf_context *ctx = elf_context_new(custom_link_object_path(), OUTPUT_OBJECT);

    ctx->data_rtype_section = elf_new_section(ctx, ".data.rtype", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->data_fndef_section = elf_new_section(ctx, ".data.fndef", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->data_symdef_section = elf_new_section(ctx, ".data.symdef", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);

    // rtype --------------------------------------------------------------------------
    ct_rtype_data = rtypes_serialize();
    elf_put_data(ctx->data_rtype_section, ct_rtype_data, ct_rtype_size);
    Elf64_Sym sym = {
            .st_shndx = ctx->data_rtype_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_rtype_size,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_RTYPE_DATA);
    elf_put_global_symbol(ctx, SYMBOL_RTYPE_COUNT, &ct_rtype_count, cross_number_size());

    // fndef --------------------------------------------------------------------------
    ct_fndef_size = collect_fndef_list(ctx);
    ct_fndef_data = fndefs_serialize();
    elf_put_data(ctx->data_fndef_section, ct_fndef_data, ct_fndef_size);
    sym = (Elf64_Sym){
            .st_shndx = ctx->data_fndef_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_fndef_size,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_FNDEF_DATA);
    elf_put_global_symbol(ctx, SYMBOL_FNDEF_COUNT, &ct_fndef_count, cross_number_size());

    // symdef --------------------------------------------------------------------------
    ct_symdef_size = collect_symdef_list(ctx);
    ct_symdef_data = symdefs_serialize();
    elf_put_data(ctx->data_symdef_section, ct_symdef_data, ct_symdef_size);
    sym = (Elf64_Sym){
            .st_shndx = ctx->data_symdef_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_symdef_size,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_SYMDEF_DATA);
    elf_put_global_symbol(ctx, SYMBOL_SYMDEF_COUNT, &ct_symdef_count, cross_number_size());

    // custom_global symbol
    // ------------------------------------------------------------------------------------------------------
    double float_mask = -0.0;
    elf_put_global_symbol(ctx, FLOAT_NEG_MASK_IDENT, &float_mask, cross_number_size());

    object_file_format(ctx);
    elf_output(ctx);
    log_debug(" --> assembler: %s\n", custom_link_object_path());
}

/**
 * 汇编器目前只支持 linux elf amd64
 * @param m
 */
static void assembler_module(module_t *m) {
    if (BUILD_OS == OS_LINUX) {// elf 就是 linux 独有都
        char *object_file_name = analyzer_force_unique_ident(m);
        str_replace_char(object_file_name, '/', '.');

        char *output = path_join(TEMP_DIR, object_file_name);
        elf_context *ctx = elf_context_new(output, OUTPUT_OBJECT);

        // 将全局变量写入到数据段或者符号表 (这里应该叫 global var)
        object_load_symbols(ctx, m->asm_global_symbols);

        cross_opcode_encodings(ctx, m->closures);

        object_file_format(ctx);

        // 输出目标文件
        elf_output(ctx);

        // 完整输出路径
        log_debug(" --> assembler: %s\n", output);
        m->object_file = output;
        return;
    }

    assertf(false, "unsupported BUILD_OS %s", BUILD_OS);
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
        elf_load_object_file(ctx, fd, 0);// 加载并解析目标文件
    }

    // 将相关符号都加入来
    slice_push(linker_libs, custom_link_object_path());
    slice_push(linker_libs, lib_file_path(LIB_START_FILE));
    slice_push(linker_libs, lib_file_path(LIB_RUNTIME_FILE));
    slice_push(linker_libs, lib_file_path(LIBUV_FILE));
    slice_push(linker_libs, lib_file_path(LIBC_FILE));
    for (int i = 0; i < linker_libs->count; ++i) {
        char *path = linker_libs->take[i];
        fd = check_open(path, O_RDONLY | O_BINARY);

        if (ends_with(path, ".o")) {
            elf_load_object_file(ctx, fd, 0);
            continue;
        }

        if (ends_with(path, ".a")) {
            elf_load_archive(ctx, fd);
            continue;
        }

        assertf(false, "cannot linker file '%s'", path);
    }

    // - core
    executable_file_format(ctx);

    // - core
    elf_output(ctx);
    if (!file_exists(output)) {
        error_exit("[linker] linker failed");
    }

    remove(BUILD_OUTPUT);
    copy(BUILD_OUTPUT, output, 0755);
    log_debug("linker output--> %s\n", output);
    log_debug("build output--> %s\n", BUILD_OUTPUT);
}

static void build_init(char *build_entry) {
    env_init();
    config_init();
    symbol_init();
    cross_reg_init();
    cross_opcode_init();

    // 全局 init
    BUILD_ENTRY = build_entry;

    char temp_path[PATH_MAX] = "";
    if (realpath(build_entry, temp_path) == NULL) {
        assertf(false, "entry file='%s' not found", build_entry);
    }

    // copy
    strcpy(SOURCE_PATH, temp_path);
    assertf(file_exists(SOURCE_PATH), "full entry file=%s not found", SOURCE_PATH);
    assertf(!dir_exists(SOURCE_PATH), "build output='%s' cannot be a directory", BUILD_OUTPUT);

    // type ct_rtype_table
    ct_rtype_table = table_new();
    ct_rtype_vec = ct_list_new(sizeof(rtype_t));
    ct_rtype_data = NULL;
    ct_rtype_count = 0;
    ct_rtype_size = 0;

    linker_libs = slice_new();
}

static void config_print() {
    log_debug("NATURE_ROOT: %s", NATURE_ROOT);
    log_debug("BUILD_OS: %s", os_to_string(BUILD_OS));
    log_debug("BUILD_ARCH: %s", arch_to_string(BUILD_ARCH));
    log_debug("BUILD_OUTPUT_NAME: %s", BUILD_OUTPUT_NAME);
    log_debug("BUILD_OUTPUT_DIR: %s", BUILD_OUTPUT_DIR);
    log_debug("BUILD_OUTPUT: %s", BUILD_OUTPUT);
    log_debug("WORK_DIR: %s", WORKDIR);
    log_debug("BASE_NS: %s", BASE_NS);
    log_debug("TERM_DIR: %s", TEMP_DIR);
    log_debug("BUILD_ENTRY: %s", BUILD_ENTRY);
    log_debug("SOURCE_PATH: %s", SOURCE_PATH);
}

static void build_assembler(slice_t *modules) {
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // symbol to var_decl(架构无关), assembler 会使用 var_decl
        // 由全局符号产生
        for (int j = 0; j < m->global_vardef->count; ++j) {
            ast_vardef_stmt_t *vardef = m->global_vardef->take[j];
            assert(vardef->var_decl.type.status == REDUCTION_STATUS_DONE);

            ast_var_decl_t *var_decl = &vardef->var_decl;
            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            symbol->name = var_decl->ident;
            symbol->size = type_kind_sizeof(var_decl->type.kind);
            symbol->value = NULL;
            slice_push(m->asm_global_symbols, symbol);
        }

        log_debug("[build_assembler] module=%s", m->source_path);
        // native closure，如果遇到 c_string, 需要在 symtab + data 中注册一条记录，然后在 .text 引用，
        // 所以有了这里的临时 closure var decls, 原则上， var_decl = global var，其和 module 挂钩

        slice_t *closures = slice_new();
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            // 基于 symbol_name 读取引用次数
            symbol_t *s = symbol_table_get_noref(c->symbol_name);
            if (s->ref_count == 0 && !str_equal(c->symbol_name, FN_MAIN_NAME)) {
                continue;
            }

            slice_concat(m->asm_global_symbols, c->asm_symbols);

            debug_asm(c);

            slice_push(closures, c);
        }
        m->closures = closures;

        assembler_module(m);
    }

    assembler_custom_links();
}

static void build_tpls(slice_t *templates) {
    slice_t *modules = slice_new();// module_t*
    // 开始编译 templates, impl 实现注册到 build.c 中即可
    for (int i = 0; i < templates->count; ++i) {
        char *full_path = templates->take[i];

        // 编译并注册 temp 文件 (template 不需要 import 所以可以直接走 analyzer/generic/infer 逻辑)
        module_t *tpl_module = module_build(NULL, full_path, MODULE_TYPE_TPL);
        slice_push(modules, tpl_module);
    }

    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        analyzer(m, m->stmt_list);

        pre_infer(m);
    }
}

static slice_t *build_modules(toml_table_t *package_conf) {
    assertf(strlen(SOURCE_PATH) > 0, "SOURCE_PATH empty");

    table_t *module_table = table_new();
    slice_t *modules = slice_new();
    slice_t *tpls = slice_new();
    slice_t *builtin_modules = slice_new();

    // builtin tpl list, default import
    char *template_dir = path_join(NATURE_ROOT, "std/temps");
    char *full_path = path_join(template_dir, "builtin_temp.n");
    assertf(file_exists(full_path), "builtin_temp.n not found in %s/std/temps", NATURE_ROOT);
    slice_push(tpls, full_path);

    // builtin modules
    char *builtin_dir = path_join(NATURE_ROOT, "std/builtin");
    // scan all builtin package, module build
    DIR *dir = opendir(builtin_dir);
    assertf(dir, "cannot found builtin dir %s", builtin_dir);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 读取该目录下的所有文件(一级目录)
        if (entry->d_type == DT_REG) {
            char *filename = strdup(entry->d_name);

            // filename 必须以 .n 结尾
            if (!ends_with(filename, ".n")) {
                continue;
            }

            full_path = path_join(builtin_dir, filename);
            slice_push(builtin_modules, full_path);
        }
    }

    // builtin module build
    linked_t *work_list = linked_new();

    for (int i = 0; i < builtin_modules->count; ++i) {
        char *builtin_file = builtin_modules->take[i];
        module_t *builtin = module_build(NULL, builtin_file, MODULE_TYPE_BUILTIN);
        slice_push(modules, builtin);
        linked_push(work_list, builtin);
    }

    // main build
    ast_import_t main_import = {
            .package_dir = WORKDIR,
            .package_conf = package_conf,
            .module_ident = FN_MAIN_NAME,
    };

    // main [links] 自动注册
    slice_t *links = package_links(main_import.package_dir, main_import.package_conf);
    if (links && links->count > 0) {
        for (int i = 0; i < links->count; ++i) {
            char *link_path = links->take[i];
            slice_push(linker_libs, link_path);
        }
    }

    table_t *links_handled = table_new();
    table_set(links_handled, main_import.package_dir, (void *) 1);

    module_t *main = module_build(&main_import, SOURCE_PATH, MODULE_TYPE_MAIN);
    slice_push(modules, main);

    linked_push(work_list, main);

    table_t *import_tpl_table = table_new();

    while (work_list->count > 0) {
        // module_build time has perfected import
        module_t *m = linked_pop(work_list);

        for (int j = 0; j < m->imports->count; ++j) {
            ast_import_t *import = m->imports->take[j];
            if (table_exist(module_table, import->full_path)) {
                continue;
            }

            if (import->use_links && !table_exist(links_handled, import->package_dir)) {
                links = package_links(import->package_dir, import->package_conf);
                if (links && links->count > 0) {
                    for (int i = 0; i < links->count; ++i) {
                        char *link_path = links->take[i];
                        slice_push(linker_libs, link_path);
                    }
                }

                table_set(links_handled, import->package_dir, import);
            }

            if (import->module_type == MODULE_TYPE_TPL) {
                assertf(import->full_path, "import temp path empty");

                if (!table_exist(import_tpl_table, import->full_path)) {
                    table_set(import_tpl_table, import->full_path, import);
                    slice_push(tpls, import->full_path);
                }
                continue;
            }

            // new module dep all imports handled
            module_t *new_module = module_build(import, import->full_path, import->module_type);

            linked_push(work_list, new_module);
            table_set(module_table, import->full_path, new_module);
            slice_push(modules, new_module);
        }
    }

    // - tpl 没有依赖关系，可以进行预先构建
    build_tpls(tpls);

    // modules contains
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        assert(m->type != MODULE_TYPE_TPL);

        // analyzer => ast_fndefs(global)
        // analyzer 需要将 global symbol 注册完成，否则在 pre_infer 时找不到相关的符号
        analyzer(m, m->stmt_list);
    }

    // register all module init to main module body
    assert(main->ast_fndefs->count > 0);
    ast_fndef_t *root_fndef = main->ast_fndefs->take[0];

    slice_t *new_body = slice_new();
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        if (m->type == MODULE_TYPE_MAIN || m->type == MODULE_TYPE_TPL) {
            continue;
        }
        if (m->call_init_stmt) {
            slice_push(new_body, m->call_init_stmt);
        }
    }
    if (new_body) {
        slice_concat(new_body, root_fndef->body);
        root_fndef->body = new_body;
    }

    return modules;
}

static void build_compiler(slice_t *modules) {
    for (int i = 0; i < modules->count; ++i) {
        pre_infer(modules->take[i]);
    }

    // infer + compiler
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // 类型推断
        infer(m);

        // 编译为 lir
        linear(m);

        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            debug_lir(c);

            // 构造 cfg
            cfg(c);

            debug_block_lir(c, "cfg");

            // 构造 ssa
            ssa(c);

            debug_block_lir(c, "ssa");

            // lir 向 arch 靠拢
            cross_lower(c);

            debug_block_lir(c, "lower");

            // 线性扫描寄存器分配
            reg_alloc(c);

            debug_block_lir(c, "reg_alloc");

            // 基于 arch 生成汇编
            cross_native(c);
        }
    }
}

/**
 * nature build main.n, build_entry = "main.n"
 * nature build client/main.n build_entry = "client/main.n"
 * @param build_entry
 */
void build(char *build_entry) {
    assertf(strlen(build_entry) > 2, "build entry=%s exception", build_entry);

    // 配置初始化
    build_init(build_entry);

    // debug
    config_print();

    // 解析 package_conf
    toml_table_t *package_conf = NULL;
    char *package_file = path_join(WORKDIR, PACKAGE_TOML);
    if (file_exists(package_file)) {
        package_conf = package_parser(package_file);
    }

    slice_t *modules = build_modules(package_conf);

    // 编译(所有的模块都编译完成后再统一进行汇编与链接)
    build_compiler(modules);

    // 汇编
    build_assembler(modules);

    // 链接
    build_linker(modules);
}

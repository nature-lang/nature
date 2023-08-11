#include "build.h"
#include "src/cross.h"
#include "src/semantic/checking.h"
#include "src/linear.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/native/amd64.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/output.h"
#include "src/ssa.h"
#include "src/register/linearscan.h"
#include "src/semantic/analyzer.h"
#include "utils/error.h"
#include "config.h"
#include "utils/custom_links.h"
#include "src/semantic/generic.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

static char *std_templates[] = {
        "builtin.n",
        "libc.n",
        "syscall.n"
};

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
    sym = (Elf64_Sym) {
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
    sym = (Elf64_Sym) {
            .st_shndx = ctx->data_symdef_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_symdef_size,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_SYMDEF_DATA);
    elf_put_global_symbol(ctx, SYMBOL_SYMDEF_COUNT, &ct_symdef_count, cross_number_size());


    // custom_global symbol ------------------------------------------------------------------------------------------------------
    double float_mask = -0.0;
    elf_put_global_symbol(ctx, FLOAT_NEG_MASK_IDENT, &float_mask, cross_number_size());


    object_file_format(ctx);
    elf_output(ctx);
    DEBUGF(" --> assembler: %s\n", custom_link_object_path());
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

        cross_opcode_encodings(ctx, m->closures);

        object_file_format(ctx);

        // 输出目标文件
        elf_output(ctx);

        // 完整输出路径
        DEBUGF(" --> assembler: %s\n", output);
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
        elf_load_object_file(ctx, fd, 0); // 加载并解析目标文件
    }

    // 将相关符号都加入来
    slice_push(linker_libs, custom_link_object_path());
    slice_push(linker_libs, lib_file_path(LIB_START_FILE));
    slice_push(linker_libs, lib_file_path(LIB_RUNTIME_FILE));
    slice_push(linker_libs, lib_file_path(LIBUCONTEXT_FILE));
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

    copy(BUILD_OUTPUT, output, 0755);
    DEBUGF("linker output--> %s\n", output);
    DEBUGF("build output--> %s\n", BUILD_OUTPUT);
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
    ct_rtype_list = ct_list_new(sizeof(rtype_t));
    ct_rtype_data = NULL;
    ct_rtype_count = 0;
    ct_rtype_size = 0;

    linker_libs = slice_new();
}

static void config_print() {
    DEBUGF("NATURE_ROOT: %s", NATURE_ROOT);
    DEBUGF("BUILD_OS: %s", os_to_string(BUILD_OS));
    DEBUGF("BUILD_ARCH: %s", arch_to_string(BUILD_ARCH));
    DEBUGF("BUILD_OUTPUT_NAME: %s", BUILD_OUTPUT_NAME);
    DEBUGF("BUILD_OUTPUT_DIR: %s", BUILD_OUTPUT_DIR);
    DEBUGF("BUILD_OUTPUT: %s", BUILD_OUTPUT);
    DEBUGF("WORK_DIR: %s", WORKDIR);
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
            ast_var_decl_t *var_decl = s->ast_value;
            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            symbol->name = s->ident;
            symbol->size = type_kind_sizeof(var_decl->type.kind);
            symbol->value = NULL;
            slice_push(m->asm_global_symbols, symbol);
        }

        DEBUGF("[build_assembler] module=%s", m->ident);
        // native closure，如果遇到 c_string, 需要在 symtab + data 中注册一条记录，然后在 .text 引用，
        // 所以有了这里的临时 closure var decls, 原则上， var_decl = global var，其和 module 挂钩

        slice_t *closures = slice_new();
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            // 基于 symbol_name 读取引用次数
            symbol_t *s = symbol_table_get_noref(c->symbol_name);
            if (s->ref_count == 0) {
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

static void build_temps(toml_table_t *package_conf) {
    slice_t *templates = slice_new();

    // 都已经在 libruntime.a 中都实现了, 所以不需要做 impl 配置了
    char *template_dir = path_join(NATURE_ROOT, "std/templates");
    // 将 nature_root 中的 template 文件先都注册进来
    for (int i = 0; i < sizeof(std_templates) / sizeof(std_templates[0]); ++i) {
        char *name = std_templates[i];
        char *source_path = path_join(template_dir, name);
        assertf(file_exists(source_path), "template file=%s not found, please reinstall nature", source_path);

        template_t *t = NEW(template_t);
        t->path = source_path;
        t->impl = NULL;
        slice_push(templates, t);
    }

    if (package_conf) {
        slice_t *src_templates = package_templates(package_conf);
        if (src_templates && src_templates > 0) {
            slice_concat(templates, src_templates);
        }
    }

    slice_t *modules = slice_new(); // module_t*
    // 开始编译 templates, impl 实现注册到 build.c 中即可
    for (int i = 0; i < templates->count; ++i) {
        template_t *t = templates->take[i];
        if (t->impl) {
            assertf(ends_with(t->path, ".a"), "only support .a file");
            slice_push(linker_libs, t->impl);
        }

        // 编译并注册 temp 文件 (template 不需要 import 所以可以直接走 analyzer/generic/checking 逻辑)
        module_t *temp_module = module_build(NULL, t->path, MODULE_TYPE_TEMP);
        slice_push(modules, temp_module);
    }

    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        analyzer(m, m->stmt_list);

        generic(m);

        checking(m);
    }

}

static slice_t *build_modules(toml_table_t *package_conf) {
    assertf(strlen(SOURCE_PATH) > 0, "SOURCE_PATH empty");

    table_t *module_table = table_new();
    slice_t *modules = slice_new();


    ast_import_t main_import = {
            .package_dir = WORKDIR,
            .package_conf = package_conf,
            .module_ident = FN_MAIN_NAME,
    };

    module_t *main = module_build(&main_import, SOURCE_PATH, MODULE_TYPE_MAIN);

    slice_push(modules, main);

    linked_t *work_list = linked_new();
    linked_push(work_list, main);

    while (work_list->count > 0) {
        module_t *m = linked_pop(work_list);

        for (int j = 0; j < m->imports->count; ++j) {
            ast_import_t *import = m->imports->take[j];
            if (table_exist(module_table, import->full_path)) {
                continue;
            }

            module_t *new_module = module_build(import,
                                                import->full_path,
                                                MODULE_TYPE_COMMON);
            linked_push(work_list, new_module);
            slice_push(modules, new_module);
            table_set(module_table, import->full_path, new_module);
        }
    }

    // 遍历所有 module 开始进行 analyzer 和 import
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        // analyzer => ast_fndefs(global)
        analyzer(m, m->stmt_list);

        // generic => ast_fndef(global+local flat)
        generic(m);
    }

    // register all module init to main module body
    assert(main->ast_fndefs->count > 0);
    ast_fndef_t *root_fndef = main->ast_fndefs->take[0];

    slice_t *new_body = slice_new();
    for (int i = 1; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        assertf(m->call_init_stmt != NULL, "module %s not found init fn stmt", m->ident);
        slice_push(new_body, m->call_init_stmt);
    }
    slice_concat(new_body, root_fndef->body);
    root_fndef->body = new_body;

    return modules;
}

static void build_compiler(slice_t *modules) {
    // checking + compiler
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        // 类型推断
        checking(m);

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

            // lir 向 arch 靠拢
            cross_lower(c);

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

    // 导入自建模块 build templates


    // 解析 package_conf
    toml_table_t *package_conf = NULL;
    char *package_file = path_join(WORKDIR, PACKAGE_TOML);
    if (file_exists(package_file)) {
        package_conf = package_parser(package_file);
    }

    build_temps(package_conf);

    slice_t *modules = build_modules(package_conf);

    // 编译(所有的模块都编译完成后再统一进行汇编与链接)
    build_compiler(modules);

    // 汇编
    build_assembler(modules);

    // 链接
    build_linker(modules);
}

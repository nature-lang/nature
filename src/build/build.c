#include "build.h"
#include "src/cross.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/native/amd64.h"
#include "src/binary/elf/linker.h"
#include "src/binary/elf/amd64.h"
#include "src/binary/elf/output.h"
#include "src/ssa.h"
#include "src/register/linearscan.h"
#include "utils/error.h"
#include "config.h"
#include "utils/custom_links.h"

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
    fd = check_open(custom_link_object_path(), O_RDONLY | O_BINARY);
    elf_load_object_file(ctx, fd, 0);


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

        DEBUGF("[build_assembler] module=%s", m->ident);
        // native closure，如果遇到 c_string, 需要在 symtab + data 中注册一条记录，然后在 .text 引用，
        // 所以有了这里的临时 closure var decls, 原则上， var_decl = global var，其和 module 挂钩
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];
            slice_concat(m->asm_global_symbols, c->asm_symbols);
            debug_asm(c);
        }


        assembler_module(m);
    }

    assembler_custom_links();
}

static void import_builtin() {
    // nature_root
    char *source_path = path_join(NATURE_ROOT, "std/builtin/builtin.n");
    assertf(file_exists(source_path), "builtin.n not found");
    // build 中包含 analyser 已经将相关 symbol 写入了, 无论是后续都 analyser 或者 infer 都能够使用
    module_t *builtin_module = module_build(source_path, MODULE_TYPE_BUILTIN);

    // infer type
    infer(builtin_module);
}

static slice_t *build_modules() {
    assertf(strlen(SOURCE_PATH) > 0, "SOURCE_PATH empty");

    table_t *module_table = table_new();
    slice_t *modules = slice_new();
    module_t *root = module_build(SOURCE_PATH, MODULE_TYPE_MAIN);
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

            module_t *module_new = module_build(import->full_path, MODULE_TYPE_COMMON);
            linked_push(work_list, module_new);
            slice_push(modules, module_new);
            table_set(module_table, import->full_path, module_new);
        }
    }

    // 将所有模块都 init 函数都注册到 root body 的最前面
    assert(root->ast_fndefs->count > 0);
    ast_fndef_t *root_fndef = root->ast_fndefs->take[0];

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
    // infer + compiler
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // 类型推断
        infer(m);

        // 全局符号的定义也需要推导以下原始类型
        for (int j = 0; j < m->global_symbols->count; ++j) {
            symbol_t *s = m->global_symbols->take[j];
            if (s->type != SYMBOL_VAR) {
                continue;
            }
            infer_var_decl(m, s->ast_value); // 类型还原
        }

        compiler(m);

        // 构造 cfg, 并转成目标架构编码
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];

            debug_lir(c);

            cfg(c);

            debug_block_lir(c, "cfg");

            // 构造 ssa
            ssa(c);

            cross_lower(c);

            linear_scan(c);

            debug_block_lir(c, "linear scan");

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

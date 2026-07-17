#include "build.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "src/binary/arch/amd64.h"
#include "src/binary/arch/arm64.h"
#include "src/binary/arch/riscv64.h"
#include "src/binary/coff/coff_amd64.h"
#include "src/binary/coff/coff_writer.h"
#include "src/binary/mach/mach.h"
#include "src/build/test_runner.h"
#include "src/build/windows_linker.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/ld/ld.h"
#include "src/linear.h"
#include "src/lower/amd64.h"
#include "src/lower/arm64.h"
#include "src/lower/riscv64.h"
#include "src/native/amd64.h"
#include "src/native/arm64.h"
#include "src/native/riscv64.h"
#include "src/peephole/amd64.h"
#include "src/peephole/arm64.h"
#include "src/register/linearscan.h"
#include "src/schedule.h"
#include "src/semantic/analyzer.h"
#include "src/semantic/generics.h"
#include "src/semantic/global_eval.h"
#include "src/semantic/infer.h"
#include "src/ssa.h"
#include "utils/helper.h"
#include "utils/log.h"

// 新增的默认版本定义
#define DEFAULT_MIN_MACOS_VERSION "11.0"
#define DEFAULT_MACOS_SDK_VERSION "14.0" // 当动态获取失败时的后备 SDK 版本

// char*, 支持 .o 或者 .a 文件后缀
static slice_t *linker_libs;

static void ld_diagnostic(void *context, ld_diag_level_t level,
                          const char *message);

static int build_path_compare(const void *left, const void *right) {
    const char *left_path = *(const char *const *) left;
    const char *right_path = *(const char *const *) right;
    return strcmp(left_path, right_path);
}

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
    char *output = path_join(
            TEMP_DIR,
            BUILD_OS == OS_WINDOWS ? "custom_links.n.obj" : "custom_links.n.o");
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
static void elf_custom_links() {
    assertf(BUILD_OS == OS_LINUX, "only support build to linux/darwin");
    elf_context_t *ctx = elf_context_new(custom_link_object_path(), OUTPUT_OBJ);

    ctx->data_rtype_section = elf_section_new(ctx, ".data.rtype", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->data_caller_section = elf_section_new(ctx, ".data.caller", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->data_fndef_section = elf_section_new(ctx, ".data.fndef", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->data_symdef_section = elf_section_new(ctx, ".data.symdef", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);

    ctx->ndata_section = elf_section_new(ctx, ".ndata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->nstrtable_section = elf_section_new(ctx, ".nstrtable", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);

    Elf64_Sym sym;

    // rtype --------------------------------------------------------------------------
    ct_rtype_data = rtypes_serialize();
    elf_put_data(ctx->data_rtype_section, ct_rtype_data, ct_rtype_size);
    sym = (Elf64_Sym) {
            .st_shndx = ctx->data_rtype_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_rtype_size,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_RTYPE_DATA);
    elf_put_global_symbol(ctx, SYMBOL_RTYPE_COUNT, &ct_rtype_count, QWORD);

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
    elf_put_global_symbol(ctx, SYMBOL_FNDEF_COUNT, &ct_fndef_count, QWORD);

    // caller - --------------------------------------------------------------------------
    ct_caller_data = callers_serialize();
    elf_put_data(ctx->data_caller_section, ct_caller_data, ct_caller_list->length * sizeof(caller_t));
    sym = (Elf64_Sym) {
            .st_shndx = ctx->data_caller_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_rtype_size,
    };
    // 注册段名称与 runtime 中的符号进行绑定
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_CALLER_DATA);
    elf_put_global_symbol(ctx, SYMBOL_CALLER_COUNT, &ct_caller_list->length, QWORD);

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
    elf_put_global_symbol(ctx, SYMBOL_SYMDEF_COUNT, &ct_symdef_count, QWORD);

    // ndata --------------------------------------------------------------------------
    elf_put_data(ctx->ndata_section, ct_data, ct_data_len);
    sym = (Elf64_Sym) {
            .st_shndx = ctx->ndata_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_data_len,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_DATA);

    // nstrtable --------------------------------------------------------------------------
    elf_put_data(ctx->nstrtable_section, (uint8_t *) ct_strtable_data, ct_strtable_len);
    sym = (Elf64_Sym) {
            .st_shndx = ctx->nstrtable_section->sh_index,
            .st_value = 0,
            .st_other = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_size = ct_strtable_len,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, SYMBOL_STRTABLE_DATA);

    // custom_global symbol
    // ------------------------------------------------------------------------------------------------------
    double f64_mask = -0.0;
    elf_put_global_symbol(ctx, F64_NEG_MASK_IDENT, &f64_mask, QWORD);
    float f32_mast = (float) (-0.0);
    elf_put_global_symbol(ctx, F32_NEG_MASK_IDENT, &f32_mast, DWORD);


    elf_file_format(ctx);

    elf_output(ctx);
    log_debug(" --> assembler: %s\n", custom_link_object_path());
}

static void mach_custom_links() {
    mach_context_t *ctx = mach_context_new(custom_link_object_path());

    ctx->data_rtype_section = mach_section_new(ctx, "__data_rtype", "__DATA", S_REGULAR);
    ctx->data_fndef_section = mach_section_new(ctx, "__data_fndef", "__DATA", S_REGULAR);
    ctx->data_symdef_section = mach_section_new(ctx, "__data_symdef", "__DATA", S_REGULAR);
    ctx->data_caller_section = mach_section_new(ctx, "__data_caller", "__DATA", S_REGULAR);

    ctx->ndata_section = mach_section_new(ctx, "__ndata", "__DATA", S_REGULAR);
    ctx->nstrtable_section = mach_section_new(ctx, "__nstrtable", "__DATA", S_REGULAR);


    // rtype --------------------------------------------------------------------------
    ct_rtype_data = rtypes_serialize();
    mach_put_data(ctx->data_rtype_section, ct_rtype_data, ct_rtype_size);
    // 创建符号指向自定义数据段 __data.rtype
    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->data_rtype_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_RTYPE_DATA);

    macho_put_global_symbol(ctx, SYMBOL_RTYPE_COUNT, &ct_rtype_count, QWORD);

    // fndef --------------------------------------------------------------------------
    ct_fndef_size = collect_fndef_list(ctx);
    ct_fndef_data = fndefs_serialize();
    mach_put_data(ctx->data_fndef_section, ct_fndef_data, ct_fndef_size);

    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->data_fndef_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_FNDEF_DATA);
    macho_put_global_symbol(ctx, SYMBOL_FNDEF_COUNT, &ct_fndef_count, QWORD);


    // caller --------------------------------------------------------------------------
    ct_caller_data = callers_serialize();
    mach_put_data(ctx->data_caller_section, ct_caller_data, ct_caller_list->length * sizeof(caller_t));
    // 注册段名称与 runtime 中的符号进行绑定
    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->data_caller_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_CALLER_DATA);
    macho_put_global_symbol(ctx, SYMBOL_CALLER_COUNT, &ct_caller_list->length, QWORD);


    // symdef --------------------------------------------------------------------------
    ct_symdef_size = collect_symdef_list(ctx);
    ct_symdef_data = symdefs_serialize();
    mach_put_data(ctx->data_symdef_section, ct_symdef_data, ct_symdef_size);

    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->data_symdef_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_SYMDEF_DATA);
    macho_put_global_symbol(ctx, SYMBOL_SYMDEF_COUNT, &ct_symdef_count, QWORD);

    // ndata --------------------------------------------------------------------------
    mach_put_data(ctx->ndata_section, ct_data, ct_data_len);
    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->ndata_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_DATA);

    // strtable
    mach_put_data(ctx->nstrtable_section, (uint8_t *) ct_strtable_data, ct_strtable_len);
    mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                              .n_type = N_SECT | N_EXT,
                                              .n_sect = ctx->nstrtable_section->sh_index,
                                              .n_value = 0, // in section data offset
                                      },
                 SYMBOL_STRTABLE_DATA);


    // custom_global symbol
    // ------------------------------------------------------------------------------------------------------
    double f64_mask = -0.0;
    macho_put_global_symbol(ctx, F64_NEG_MASK_IDENT, &f64_mask, QWORD);
    float f32_mast = (float) (-0.0);
    macho_put_global_symbol(ctx, F32_NEG_MASK_IDENT, &f32_mast, DWORD);

    mach_output_object(ctx);
    log_debug(" --> assembler: %s\n", custom_link_object_path());
}

static void coff_require(coff_object_t *object,
                         coff_writer_status_t status,
                         const char *operation) {
    assertf(status == COFF_WRITER_OK,
            "COFF metadata %s failed: %s (%s)", operation,
            coff_writer_status_string(status),
            object && coff_object_last_error(object)
                    ? coff_object_last_error(object)
                    : "no detail");
}

static coff_section_t *coff_metadata_section(coff_object_t *object,
                                             const char *name,
                                             bool read_only) {
    coff_section_t *section = NULL;
    uint32_t characteristics =
            LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ;
    if (!read_only) characteristics |= LD_COFF_SCN_MEM_WRITE;
    coff_require(object,
                 coff_object_add_section(object, name, characteristics, 8U,
                                         &section),
                 name);
    return section;
}

static uint32_t coff_put_metadata(coff_object_t *object,
                                  coff_section_t *section,
                                  const char *name, const void *data,
                                  uint64_t size) {
    assertf(size <= UINT32_MAX,
            "COFF metadata symbol '%s' exceeds 4 GiB", name);
    /* Keep a resolvable section contribution even when the logical table is
       empty.  Its separately emitted count remains zero, so the byte is never
       observed by the runtime. */
    uint8_t empty = 0U;
    size_t stored_size = size != 0U ? (size_t) size : 1U;
    uint32_t offset = 0U;
    coff_require(object,
                 coff_section_append(section, size != 0U ? data : &empty,
                                     stored_size, 8U, &offset),
                 name);
    coff_require(object,
                 coff_object_define_symbol(
                         object, name, section, offset, 0U,
                         LD_COFF_STORAGE_CLASS_EXTERNAL, NULL),
                 name);
    return offset;
}

static void coff_add_addr64_relocation(coff_object_t *object,
                                       coff_section_t *section,
                                       uint32_t offset,
                                       const char *target,
                                       uint16_t target_type) {
    uint32_t symbol_index = 0U;
    coff_require(object,
                 coff_object_get_or_add_symbol_reference(
                         object, target, true, target_type, &symbol_index),
                 target);
    coff_require(object,
                 coff_section_add_relocation_with_addend(
                         section, offset, symbol_index,
                         LD_COFF_REL_AMD64_ADDR64, 0),
                 target);
}

static void coff_relocate_fndefs(coff_object_t *object,
                                 coff_section_t *section,
                                 uint32_t section_offset) {
    uint64_t index = 0U;
    SLICE_FOR(symbol_fn_list) {
        symbol_t *symbol = SLICE_VALUE(symbol_fn_list);
        ast_fndef_t *fn = symbol->ast_value;
        closure_t *closure = fn->closure;
        if (!closure || closure->text_count == 0U) continue;
        uint64_t relocation = (uint64_t) section_offset +
                              index * sizeof(fndef_t) +
                              offsetof(fndef_t, base);
        assertf(relocation <= UINT32_MAX,
                "COFF fndef relocation offset overflows");
        coff_add_addr64_relocation(object, section, (uint32_t) relocation,
                                   closure->linkident,
                                   COFF_SYMBOL_TYPE_FUNCTION);
        index++;
    }
    assertf(index == ct_fndef_count,
            "COFF fndef relocation count mismatch: %lu != %lu", index,
            ct_fndef_count);
}

static void coff_relocate_symdefs(coff_object_t *object,
                                  coff_section_t *section,
                                  uint32_t section_offset) {
    uint64_t index = 0U;
    SLICE_FOR(symbol_var_list) {
        symbol_t *symbol = SLICE_VALUE(symbol_var_list);
        if (symbol->is_local) continue;
        ast_var_decl_t *var_decl = symbol->ast_value;
        uint64_t relocation = (uint64_t) section_offset +
                              index * sizeof(symdef_t) +
                              offsetof(symdef_t, base);
        assertf(relocation <= UINT32_MAX,
                "COFF symdef relocation offset overflows");
        coff_add_addr64_relocation(object, section, (uint32_t) relocation,
                                   var_decl->ident, 0U);
        index++;
    }
    assertf(index == ct_symdef_count,
            "COFF symdef relocation count mismatch: %lu != %lu", index,
            ct_symdef_count);
}

static void coff_custom_links() {
    assertf(BUILD_OS == OS_WINDOWS && BUILD_ARCH == ARCH_AMD64,
            "COFF metadata is only available for windows_amd64");
    coff_object_t *object = coff_object_create_amd64("custom_links.n");
    assertf(object, "cannot create Windows COFF metadata object");

    /* `$` suffixes preserve the Nature table grouping/order while allowing
       the PE writer to merge the contributions into ordinary .data/.rdata. */
    coff_section_t *rtype =
            coff_metadata_section(object, ".data$rtype", false);
    coff_section_t *caller =
            coff_metadata_section(object, ".data$caller", false);
    coff_section_t *fndef =
            coff_metadata_section(object, ".data$fndef", false);
    coff_section_t *symdef =
            coff_metadata_section(object, ".data$symdef", false);
    coff_section_t *ndata =
            coff_metadata_section(object, ".data$ndata", false);
    coff_section_t *nstrtable =
            coff_metadata_section(object, ".rdata$nstrtable", true);
    coff_section_t *globals = coff_object_data(object);
    assertf(globals, "COFF metadata object has no .data section");

    ct_rtype_data = rtypes_serialize();
    coff_put_metadata(object, rtype, SYMBOL_RTYPE_DATA, ct_rtype_data,
                      ct_rtype_size);
    coff_put_metadata(object, globals, SYMBOL_RTYPE_COUNT, &ct_rtype_count,
                      sizeof(ct_rtype_count));

    ct_fndef_size = collect_fndef_list(object);
    ct_fndef_data = fndefs_serialize();
    uint32_t fndef_offset = coff_put_metadata(
            object, fndef, SYMBOL_FNDEF_DATA, ct_fndef_data, ct_fndef_size);
    coff_relocate_fndefs(object, fndef, fndef_offset);
    coff_put_metadata(object, globals, SYMBOL_FNDEF_COUNT, &ct_fndef_count,
                      sizeof(ct_fndef_count));

    ct_caller_data = callers_serialize();
    uint64_t caller_size =
            (uint64_t) ct_caller_list->length * sizeof(caller_t);
    coff_put_metadata(object, caller, SYMBOL_CALLER_DATA, ct_caller_data,
                      caller_size);
    uint64_t caller_count = (uint64_t) ct_caller_list->length;
    coff_put_metadata(object, globals, SYMBOL_CALLER_COUNT, &caller_count,
                      sizeof(caller_count));

    ct_symdef_size = collect_symdef_list(object);
    ct_symdef_data = symdefs_serialize();
    uint32_t symdef_offset = coff_put_metadata(
            object, symdef, SYMBOL_SYMDEF_DATA, ct_symdef_data,
            ct_symdef_size);
    coff_relocate_symdefs(object, symdef, symdef_offset);
    coff_put_metadata(object, globals, SYMBOL_SYMDEF_COUNT, &ct_symdef_count,
                      sizeof(ct_symdef_count));

    coff_put_metadata(object, ndata, SYMBOL_DATA, ct_data, ct_data_len);
    coff_put_metadata(object, nstrtable, SYMBOL_STRTABLE_DATA,
                      ct_strtable_data, ct_strtable_len);

    double f64_mask = -0.0;
    float f32_mask = (float) -0.0;
    coff_put_metadata(object, globals, F64_NEG_MASK_IDENT, &f64_mask,
                      sizeof(f64_mask));
    coff_put_metadata(object, globals, F32_NEG_MASK_IDENT, &f32_mask,
                      sizeof(f32_mask));

    coff_require(object,
                 coff_object_write_file(object, custom_link_object_path()),
                 "write custom_links.n.obj");
    coff_object_destroy(object);
    log_debug(" --> assembler: %s\n", custom_link_object_path());
}

static void assembler_custom_links() {
    if (BUILD_OS == OS_LINUX) {
        return elf_custom_links();
    }

    if (BUILD_OS == OS_DARWIN) {
        return mach_custom_links();
    }

    if (BUILD_OS == OS_WINDOWS) {
        return coff_custom_links();
    }
}

/**
 * 汇编器目前只支持 linux elf amd64 和 darwin macho amd64
 * @param m
 */
static void elf_assembler_module(module_t *m) {
    char *object_file_name = analyzer_force_unique_ident(m);
    str_replace_char(object_file_name, '/', '.');

    char *output = path_join(TEMP_DIR, object_file_name);

    elf_context_t *ctx = elf_context_new(output, OUTPUT_OBJ);

    // 将全局变量写入到数据段或者符号表 (这里应该叫 global var)
    for (int i = 0; i < m->asm_global_symbols->count; ++i) {
        asm_global_symbol_t *symbol = m->asm_global_symbols->take[i];
        // 写入到数据段
        uint64_t offset = elf_put_data(ctx->data_section, symbol->value, symbol->size);

        // 写入符号表
        Elf64_Sym sym = {
                .st_size = symbol->size,
                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
                .st_other = 0,
                .st_shndx = ctx->data_section->sh_index, // 定义符号的段
                .st_value = offset, // 定义符号的位置，基于段的偏移
        };
        elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, symbol->name);
    }

    if (BUILD_ARCH == ARCH_AMD64) {
        elf_amd64_operation_encodings(ctx, m);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        elf_arm64_operation_encodings(ctx, m);
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        elf_riscv64_operation_encodings(ctx, m);
    } else {
        assert(false);
    }


    // 可链接文件格式化
    elf_file_format(ctx);

    // 输出 elf 目标文件 (目前不完全支持 macho 链接器，无法链接 macho 格式的 obj 文件)
    elf_output(ctx);

    // 完整输出路径
    log_debug(" --> assembler: %s\n", output);
    m->object_file = output;
}

static void mach_assembler_module(module_t *m) {
    char *object_file_name = analyzer_force_unique_ident(m);
    str_replace_char(object_file_name, '/', '.');

    char *output = path_join(TEMP_DIR, object_file_name);

    mach_context_t *ctx = mach_context_new(output);

    // 将全局变量写入到数据段或者符号表 (这里应该叫 global var)
    for (int i = 0; i < m->asm_global_symbols->count; ++i) {
        asm_global_symbol_t *symbol = m->asm_global_symbols->take[i];
        // 写入到数据段
        uint64_t offset = mach_put_data(ctx->data_section, symbol->value, symbol->size);

        // 写入符号表
        mach_put_sym(ctx->symtab_command, &(struct nlist_64) {
                                                  .n_type = N_SECT | N_EXT,
                                                  .n_sect = ctx->data_section->sh_index,
                                                  .n_value = offset, // in section data offset
                                          },
                     symbol->name);
    }

    if (BUILD_ARCH == ARCH_AMD64) {
        mach_amd64_operation_encodings(ctx, m->closures);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        mach_arm64_operation_encodings(ctx, m->closures);
    } else {
        assert(false);
    }

    // 可链接文件格式化
    bool result = mach_output_object(ctx);
    if (!result) {
        m->object_file = NULL;
        return;
    }

    // 完整输出路径
    log_debug(" --> assembler: %s\n", output);
    m->object_file = output;
}

static void coff_assembler_module_windows(module_t *m) {
    assertf(BUILD_OS == OS_WINDOWS && BUILD_ARCH == ARCH_AMD64,
            "COFF assembler is only available for windows_amd64");
    char *object_file_name = analyzer_force_unique_ident(m);
    str_replace_char(object_file_name, '/', '.');
    char *object_with_suffix = str_connect(object_file_name, ".obj");
    char *output = path_join(TEMP_DIR, object_with_suffix);
    char error[1024] = {0};
    coff_writer_status_t status =
            coff_assembler_module(m, output, error, sizeof(error));
    assertf(status == COFF_WRITER_OK,
            "cannot assemble Windows COFF module '%s': %s (%s)",
            m->source_path, coff_writer_status_string(status),
            error[0] ? error : "no detail");
    assertf(m->object_file && file_exists(m->object_file),
            "Windows COFF assembler did not create '%s'", output);
    log_debug(" --> assembler: %s\n", output);
}

static void assembler_module(module_t *m) {
    if (BUILD_OS == OS_LINUX) {
        return elf_assembler_module(m);
    }

    if (BUILD_OS == OS_DARWIN) {
        return mach_assembler_module(m);
    }

    if (BUILD_OS == OS_WINDOWS) {
        return coff_assembler_module_windows(m);
    }

    assertf(false, "unsupported object format for target %s_%s",
            os_to_string(BUILD_OS), arch_to_string(BUILD_ARCH));
}

/**
 * modules modules
 * @param modules
 */
static void ld_elf_exe(slice_t *modules, char *ldflags) {
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.output_path = BUILD_OUTPUT;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;

    if (BUILD_ARCH == ARCH_ARM64) {
        options.arch = LD_ARCH_ARM64;
    } else if (BUILD_ARCH == ARCH_AMD64) {
        options.arch = LD_ARCH_AMD64;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        options.arch = LD_ARCH_RISCV64;
    } else {
        assertf(false, "unsupported Linux ELF target architecture");
    }

    assertf(ld_add_input(&options, lib_file_path(LIB_START_FILE)) == LD_OK,
            "cannot add Linux ELF startup object");
    for (int i = 0; i < modules->count; i++) {
        module_t *module = modules->take[i];
        assertf(ld_add_input(&options, module->object_file) == LD_OK,
                "cannot add Linux ELF module object '%s'", module->object_file);
    }
    assertf(ld_add_input(&options, custom_link_object_path()) == LD_OK,
            "cannot add Linux ELF metadata object");
    for (int i = 0; i < linker_libs->count; i++) {
        char *path = linker_libs->take[i];
        assertf(ld_add_input(&options, path) == LD_OK,
                "cannot add Linux package link '%s'", path);
    }
    assertf(ld_add_input(&options, lib_file_path(LIB_RUNTIME_FILE)) == LD_OK,
            "cannot add Linux runtime archive");
    assertf(ld_add_input(&options, lib_file_path(LIBUV_FILE)) == LD_OK,
            "cannot add Linux libuv archive");
    assertf(ld_add_input(&options, lib_file_path(LIBC_FILE)) == LD_OK,
            "cannot add Linux libc archive");
    assertf(ld_add_input(&options, lib_file_path(LIBGCC_FILE)) == LD_OK,
            "cannot add Linux libgcc archive");

    char library_dir[PATH_MAX];
    int library_dir_length = snprintf(
            library_dir, sizeof(library_dir), "%s/lib/%s_%s", NATURE_ROOT,
            os_to_string(BUILD_OS), arch_to_string(BUILD_ARCH));
    assertf(library_dir_length > 0 &&
                    (size_t) library_dir_length < sizeof(library_dir),
            "Nature library directory path is too long");
    assertf(ld_add_library_path(&options, library_dir) == LD_OK,
            "cannot add Nature Linux library search path");

    int parse_result = ld_parse_flags(&options, ldflags ? ldflags : "");
    assertf(parse_result == LD_OK,
            "unsupported Linux linker flags (error %d)", parse_result);
    int result = ld_link(&options);
    assertf(result == LD_OK, "internal Linux ELF linker failed (error %d)",
            result);
    assertf(file_exists(BUILD_OUTPUT),
            "internal Linux ELF linker did not create '%s'", BUILD_OUTPUT);
    log_debug("internal ELF linker output --> %s", BUILD_OUTPUT);
    ld_options_deinit(&options);
}

static void ld_windows_add_sysroot_input(ld_options_t *options,
                                         const char *file,
                                         bool required) {
    char *path = lib_file_path((char *) file);
    if (!file_exists(path)) {
        assertf(!required,
                "windows_amd64 sysroot is incomplete: required file '%s' "
                "is missing (expected '%s')",
                file, path);
        return;
    }
    assertf(ld_add_input(options, path) == LD_OK,
            "cannot add Windows sysroot input '%s'", path);
}

static void ld_windows_exe(slice_t *modules, char *ldflags) {
    assertf(BUILD_OS == OS_WINDOWS && BUILD_ARCH == ARCH_AMD64,
            "internal COFF linker only supports windows_amd64");

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = BUILD_OUTPUT;
    options.entry_symbol = "mainCRTStartup";
    options.pie = true;
    /* The COFF backend can retain DWARF from external objects, but Nature's
       compiler does not emit it yet.  Ordinary release builds therefore do
       not carry sysroot debug sections. */
    options.debug_mode = LD_DEBUG_NONE;
    options.diagnostic = ld_diagnostic;

    char map_path[PATH_MAX];
    size_t output_length = strlen(BUILD_OUTPUT);
    if (output_length >= 4U &&
        strcmp(BUILD_OUTPUT + output_length - 4U, ".exe") == 0) {
        assertf(output_length - 4U + sizeof(".map") <= sizeof(map_path),
                "Windows map path is too long");
        memcpy(map_path, BUILD_OUTPUT, output_length - 4U);
        memcpy(map_path + output_length - 4U, ".map", sizeof(".map"));
    } else {
        int map_length = snprintf(map_path, sizeof(map_path), "%s.map",
                                  BUILD_OUTPUT);
        assertf(map_length > 0 && (size_t) map_length < sizeof(map_path),
                "Windows map path is too long");
    }
    options.map_path = map_path;

    char library_dir[PATH_MAX];
    int library_dir_length = snprintf(
            library_dir, sizeof(library_dir), "%s/lib/windows_amd64",
            NATURE_ROOT);
    assertf(library_dir_length > 0 &&
                    (size_t) library_dir_length < sizeof(library_dir),
            "Nature windows_amd64 sysroot path is too long");
    options.sysroot = library_dir;
    assertf(ld_add_library_path(&options, library_dir) == LD_OK,
            "cannot add Windows sysroot search path '%s'", library_dir);

    /* The order is part of the controlled sysroot contract.  CRT startup is
       direct, user objects precede runtime archives, and import libraries are
       lazy providers at the end. */
    ld_windows_add_sysroot_input(&options, "crt2.obj", true);
    for (int i = 0; i < modules->count; i++) {
        module_t *module = modules->take[i];
        if (!module->object_file) continue;
        assertf(ld_add_input(&options, module->object_file) == LD_OK,
                "cannot add Windows module object '%s'",
                module->object_file);
    }
    assertf(ld_add_input(&options, custom_link_object_path()) == LD_OK,
            "cannot add Windows Nature metadata object");
    for (int i = 0; i < linker_libs->count; i++) {
        char *path = linker_libs->take[i];
        assertf(ld_add_input(&options, path) == LD_OK,
                "cannot add Windows package link '%s'", path);
    }

    size_t sysroot_library_count;
    const windows_sysroot_library_t *sysroot_libraries =
            windows_sysroot_libraries(&sysroot_library_count);
    for (size_t i = 0; i < sysroot_library_count; i++)
        ld_windows_add_sysroot_input(&options, sysroot_libraries[i].name,
                                    sysroot_libraries[i].required);

    int parse_result = ld_parse_flags(&options, ldflags ? ldflags : "");
    assertf(parse_result == LD_OK,
            "unsupported Windows linker flags (error %d)", parse_result);
    int result = ld_link(&options);
    assertf(result == LD_OK,
            "internal Windows COFF linker failed (error %d)", result);
    assertf(file_exists(BUILD_OUTPUT),
            "internal Windows COFF linker did not create '%s'", BUILD_OUTPUT);
    log_debug("internal COFF linker output --> %s", BUILD_OUTPUT);
    ld_options_deinit(&options);
}

static void custom_ld_elf_exe(slice_t *modules, char *use_ld, char *ldflags) {
    assert(strlen(use_ld) > 0);

    // 检测当前设备是否安装了 ld 命令
    if (!build_command_exists(use_ld)) {
        assertf(false, "'%s' command not found. Please ensure it is installed and in your PATH.", use_ld);
    }

    // 检测 ldflags 是否包含 -lc（链接 libc）
    bool has_lc = strstr(ldflags, "-lc") != NULL;

    // 将 modules 中的 obj output_file 添加到文件 objects.txt 中, 用来作为 ld 的参数
    char *objects_file = path_join(TEMP_DIR, "objects.txt");
    FILE *obj_list = fopen(objects_file, "w");
    if (!obj_list) {
        assertf(false, "unable to create objects list file: %s", objects_file);
    }
    for (int i = 0; i < modules->count; i++) {
        module_t *m = modules->take[i];
        if (m->object_file) {
            fprintf(obj_list, "%s\n", m->object_file);
        }
    }

    fprintf(obj_list, "%s\n", custom_link_object_path());
    fclose(obj_list);

    // 添加必要的库文件
    slice_push(linker_libs, lib_file_path(LIB_START_FILE));

    // 如果 ldflags 链接了 -lc，则在 LIB_START_FILE 后引入 crti.o 和 crtn.o
    if (has_lc) {
        slice_push(linker_libs, lib_file_path("crti.o"));
        slice_push(linker_libs, lib_file_path("crtn.o"));
    }

    slice_push(linker_libs, str_connect("@", objects_file));
    slice_push(linker_libs, lib_file_path(LIB_RUNTIME_FILE));
    slice_push(linker_libs, lib_file_path(LIBUV_FILE));

    if (!has_lc) {
        slice_push(linker_libs, lib_file_path(LIBC_FILE));
        slice_push(linker_libs, lib_file_path(LIBGCC_FILE));
    }

    char libs_str[4096] = ""; // 用于存储库文件路径字符串
    // 拼接 linker_libs 中的库文件路径
    for (int i = 0; i < linker_libs->count; i++) {
        char *lib = linker_libs->take[i];
        strcat(libs_str, lib);
        strcat(libs_str, " ");
    }

    // 拼接出 ld 参数
    char *output = path_join(TEMP_DIR, LINKER_OUTPUT);
    char cmd[8192];

    char *no_warning = "--no-warn-mismatch --no-warn-search-mismatch";

    // 对于 ELF 格式，链接命令格式不同于 Mach-O
    // 当链接了 -lc 时，将 objects_file 和 libs_str 提取到 ldflags 的前面
    if (has_lc) {
        snprintf(cmd, sizeof(cmd),
                 "%s %s -o %s %s %s",
                 use_ld,
                 no_warning,
                 output,
                 libs_str,
                 ldflags);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "%s %s -o %s %s %s",
                 use_ld,
                 no_warning,
                 output,
                 ldflags,
                 libs_str);
    }

    // 使用 ld 命令执行链接
    log_debug("%s", cmd);
    int result = system(cmd);
    if (result != 0) {
        assertf(false, "Linking failed. ld command error: %d", result);
    }

    if (!file_exists(output)) {
        assertf(false, "Linking failed. output file '%s' was not created", output);
    }

    remove(BUILD_OUTPUT);
    copy(BUILD_OUTPUT, output, 0755);
    log_debug("linker output --> %s", output);
    log_debug("build output --> %s", BUILD_OUTPUT);
}


/**
 * 查找 macOS 系统的 syslibroot 目录
 * 首先尝试使用 xcrun 命令获取 SDK 路径
 * 如果失败，则尝试一些常见的默认位置
 * @return syslibroot 路径，如果找不到则返回 NULL
 */
static char *find_syslibroot() {
    if (BUILD_OS != OS_DARWIN) {
        return NULL;
    }

#ifndef __DARWIN
    return NULL;
#else
    // 尝试使用 xcrun 命令获取 SDK 路径
    FILE *fp = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (fp) {
        char path[PATH_MAX] = {0};
        char *result = NULL;
        if (fgets(path, PATH_MAX, fp) != NULL) {
            // 移除末尾的换行符
            size_t len = strlen(path);
            if (len > 0 && path[len - 1] == '\n') {
                path[len - 1] = '\0';
            }
            // 检查路径是否存在
            if (strlen(path) > 0 && dir_exists(path)) {
                result = strdup(path);
            }
        }
        pclose(fp);
        if (result) {
            return result;
        }
    }

    // 尝试常见的默认位置
    char *default_paths[] = {
            "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
            "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk",
            NULL};

    for (int i = 0; default_paths[i] != NULL; i++) {
        if (dir_exists(default_paths[i])) {
            return strdup(default_paths[i]);
        }
    }

    // 如果找不到，返回 NULL
    return NULL;
#endif
}

// 新增函数：获取 macOS SDK 版本
static char *get_macos_sdk_version() {
    if (BUILD_OS != OS_DARWIN) {
        return NULL;
    }

#ifndef __DARWIN
    return NULL;
#else
    // 尝试使用 xcrun 命令获取 SDK 版本
    FILE *fp = popen("xcrun --sdk macosx --show-sdk-version 2>/dev/null", "r");
    if (!fp) {
        log_warn("Warning: Failed to run xcrun to get SDK version (popen failed)");
        return NULL;
    }

    char version_str[64] = {0};
    char *result_dup_str = NULL;

    if (fgets(version_str, sizeof(version_str) - 1, fp) != NULL) {
        size_t len = strlen(version_str);
        // 移除末尾的换行符
        if (len > 0 && version_str[len - 1] == '\n') {
            version_str[len - 1] = '\0';
            len--;
        }

        result_dup_str = strdup(version_str);
    }

    // 关闭管道。pclose 会等待命令完成。
    if (pclose(fp) == -1) {
        log_warn("Warning: pclose failed after trying to get SDK version");
    }

    if (!result_dup_str) {
        log_warn("Warning: Could not determine a valid SDK version via xcrun. Raw output from xcrun: '%s'. Using default SDK version.",
                 version_str);
    }

    return result_dup_str; // 可能返回 NULL
#endif
}

/**
 * ld -w -arch arm64 -dynamic -platform_version macos 11.7.1 14.0 -e _runtime_main -o a.out ${ldflags} libruntime.a libuv.a libSystem.tbd  @objects.txt
 * ld -w -arch x86_64 -dynamic -platform_version macos 11.7.1 14.0 -e _runtime_main -o a.out libruntime.a libuv.a libSystem.tbd @objects.txt
 */
static void custom_ld_mach_exe(slice_t *modules, const char *use_ld,
                               char *ldflags) {
    assert(strlen(use_ld) > 0);

    // 检测当前设备是否安装了 ld 命令
    if (!build_command_exists(use_ld)) {
        assertf(false, "'%s' command not found. Please ensure it is installed and in your PATH.", use_ld);
    }

    // 将 modules 中的 obj output_file 添加到文件 objects.txt 中, 用来作为 ld 的参数
    const char *objects_file = path_join(TEMP_DIR, "objects.txt");
    FILE *obj_list = fopen(objects_file, "w");
    if (!obj_list) {
        assertf(false, "unable to create objects list file: %s", objects_file);
    }
    for (int i = 0; i < modules->count; i++) {
        module_t *m = modules->take[i];
        if (m->object_file) {
            fprintf(obj_list, "%s\n", m->object_file);
        }
    }


    fprintf(obj_list, "%s\n", custom_link_object_path());
    fclose(obj_list);

    char *darwin_ld_arch;
    if (BUILD_ARCH == ARCH_AMD64) {
        darwin_ld_arch = "x86_64";
    } else if (BUILD_ARCH == ARCH_ARM64) {
        darwin_ld_arch = "arm64";
    } else {
        assert(false);
    }

    // 相关文件必须存在
    assert(file_exists(lib_file_path(LIB_RUNTIME_FILE)));
    assert(file_exists(lib_file_path(LIBUV_FILE)));

    slice_push(linker_libs, lib_file_path(LIB_RUNTIME_FILE));
    slice_push(linker_libs, lib_file_path(LIBUV_FILE));

    char libs_str[4096] = ""; // 用于存储库文件路径字符串, 比如 libruntime.a
    // 拼接 linker_libs 中的库文件路径
    for (int i = 0; i < linker_libs->count; i++) {
        char *lib = linker_libs->take[i];
        strcat(libs_str, lib);
        strcat(libs_str, " ");
    }

    // 获取 syslibroot 路径
    char *syslibroot = find_syslibroot();
    char syslibroot_option[PATH_MAX + 32] = "";
    if (syslibroot) {
        snprintf(syslibroot_option, sizeof(syslibroot_option), "-syslibroot %s ", syslibroot);
    }

    // 获取 SDK 版本
    char *sdk_version_dynamic = get_macos_sdk_version();
    char actual_sdk_version[64];
    if (sdk_version_dynamic) {
        strncpy(actual_sdk_version, sdk_version_dynamic, sizeof(actual_sdk_version) - 1);
        actual_sdk_version[sizeof(actual_sdk_version) - 1] = '\0'; // 确保 null 结尾
        free(sdk_version_dynamic); // 释放 get_macos_sdk_version 返回的内存
    } else {
        // 如果动态获取失败，使用默认值
        strncpy(actual_sdk_version, DEFAULT_MACOS_SDK_VERSION, sizeof(actual_sdk_version) - 1);
        actual_sdk_version[sizeof(actual_sdk_version) - 1] = '\0';
        fprintf(stderr, "Warning: Using default SDK version %s for linking.\n", actual_sdk_version);
    }

    // 构建 platform_version 参数字符串: "macos <min_version> <sdk_version>"
    char platform_version_args[128];
    snprintf(platform_version_args, sizeof(platform_version_args), "macos %s %s",
             DEFAULT_MIN_MACOS_VERSION, actual_sdk_version);

    // 拼接出 ld 参数, libruntime.a/libuv.a/libSystem.tbd 都在 lib_file_path(LIBMACH_C_FILE)
    char *output = path_join(TEMP_DIR, LINKER_OUTPUT);
    char cmd[16384];

    snprintf(cmd, sizeof(cmd),
             "%s -w -arch %s -dynamic -platform_version %s %s"
             "-o %s %s %s @%s -lSystem",
             use_ld,
             darwin_ld_arch,
             platform_version_args,
             syslibroot_option,
             output,
             ldflags,
             libs_str,
             objects_file);


    // 使用 ld 命令执行 ld 相关参数，并参考 build_elf_exe 进行打印 copy debug 操作
    log_debug("%s", cmd);
    int result = system(cmd);
    if (result != 0) {
        assertf(false, "Linking failed. ld command error: %d", result);
    }

    if (!file_exists(output)) {
        assertf(false, "Linking failed. output file '%s' was not created", output);
    }

    remove(BUILD_OUTPUT);
    copy(BUILD_OUTPUT, output, 0755);
    log_debug("linker output --> %s", output);
    log_debug("build output --> %s", BUILD_OUTPUT);
}

static void custom_ld_windows_exe(slice_t *modules, const char *use_ld,
                                  char *ldflags) {
    assertf(BUILD_OS == OS_WINDOWS && BUILD_ARCH == ARCH_AMD64,
            "external COFF fallback only supports windows_amd64");
    assertf(use_ld && *use_ld, "Windows external linker is empty");
    assertf(build_command_exists(use_ld),
            "'%s' command not found. Please ensure it is installed and in "
            "your PATH.",
            use_ld);

    char *response_path = path_join(TEMP_DIR, "windows-link.rsp");
    FILE *response = fopen(response_path, "w");
    assertf(response, "unable to create Windows linker response file: %s",
            response_path);
    fprintf(response,
            "/nologo\n/subsystem:console\n/entry:mainCRTStartup\n"
            "/out:\"%s\"\n/libpath:\"%s/lib/windows_amd64\"\n",
            BUILD_OUTPUT, NATURE_ROOT);
    if (ldflags && *ldflags) fprintf(response, "%s\n", ldflags);
    fprintf(response, "\"%s\"\n", lib_file_path("crt2.obj"));
    for (int i = 0; i < modules->count; i++) {
        module_t *module = modules->take[i];
        if (module->object_file)
            fprintf(response, "\"%s\"\n", module->object_file);
    }
    fprintf(response, "\"%s\"\n", custom_link_object_path());
    for (int i = 0; i < linker_libs->count; i++)
        fprintf(response, "\"%s\"\n", (char *) linker_libs->take[i]);
    char sysroot[PATH_MAX];
    int sysroot_length = snprintf(sysroot, sizeof(sysroot),
                                  "%s/lib/windows_amd64", NATURE_ROOT);
    assertf(sysroot_length > 0 && (size_t) sysroot_length < sizeof(sysroot),
            "Nature windows_amd64 sysroot path is too long");
    assertf(windows_linker_write_default_libraries(response, sysroot),
            "cannot write Windows linker response file '%s'", response_path);
    assertf(fclose(response) == 0,
            "cannot finish Windows linker response file '%s'",
            response_path);

    char command[PATH_MAX * 2U + 16U];
    int length = snprintf(command, sizeof(command), "\"%s\" @\"%s\"",
                          use_ld, response_path);
    assertf(length > 0 && (size_t) length < sizeof(command),
            "Windows external linker command is too long");
    log_debug("%s", command);
    int result = system(command);
    assertf(result == 0, "Windows external linking failed: %d", result);
    assertf(file_exists(BUILD_OUTPUT),
            "Windows external linker did not create '%s'", BUILD_OUTPUT);
}

static void ld_diagnostic(void *context, ld_diag_level_t level, const char *message) {
    (void) context;
    if (level == LD_DIAG_ERROR) {
        log_error("[ld] %s", message);
    } else if (level == LD_DIAG_WARNING) {
        log_warn("[ld] %s", message);
    } else {
        log_debug("[ld] %s", message);
    }
}

static void ld_mach_exe(slice_t *modules, char *ldflags) {
    assertf(BUILD_ARCH == ARCH_ARM64, "internal Darwin linker currently supports arm64 only");
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = BUILD_OUTPUT;
    options.entry_symbol = LD_ENTRY;
    options.diagnostic = ld_diagnostic;
    options.min_os_version = ld_macos_version(11, 0, 0);

    char *sysroot = find_syslibroot();
    char *sdk_version = get_macos_sdk_version();
    options.sysroot = sysroot;
    char nature_library_path[PATH_MAX];
    int nature_library_length = snprintf(nature_library_path, sizeof(nature_library_path), "%s/lib/%s_%s",
                                         NATURE_ROOT, os_to_string(BUILD_OS), arch_to_string(BUILD_ARCH));
    assertf(nature_library_length >= 0 && (size_t) nature_library_length < sizeof(nature_library_path),
            "Nature library search path is too long");
    assertf(ld_add_library_path(&options, nature_library_path) == LD_OK,
            "cannot add Nature library search path '%s'", nature_library_path);
    if (sdk_version) {
        unsigned major = 0, minor = 0, patch = 0;
        sscanf(sdk_version, "%u.%u.%u", &major, &minor, &patch);
        options.sdk_version = ld_macos_version(major, minor, patch);
    }
    for (int i = 0; i < modules->count; i++) {
        module_t *module = modules->take[i];
        if (module->object_file) {
            assertf(ld_add_input(&options, module->object_file) == LD_OK,
                    "cannot add object '%s' to internal Darwin linker", module->object_file);
        }
    }
    assertf(ld_add_input(&options, custom_link_object_path()) == LD_OK,
            "cannot add custom metadata object to internal Darwin linker");
    for (int i = 0; i < linker_libs->count; i++) {
        char *path = linker_libs->take[i];
        assertf(ld_add_input(&options, path) == LD_OK,
                "cannot add package link '%s' to internal Darwin linker", path);
    }
    assertf(ld_add_input(&options, lib_file_path(LIB_RUNTIME_FILE)) == LD_OK,
            "cannot add Darwin runtime to internal linker");
    assertf(ld_add_input(&options, lib_file_path(LIBUV_FILE)) == LD_OK,
            "cannot add Darwin libuv to internal linker");
    int parse_result = ld_parse_flags(&options, ldflags ? ldflags : "");
    assertf(parse_result == LD_OK, "unsupported Darwin linker flags (error %d)", parse_result);
    int result = ld_link(&options);
    assertf(result == LD_OK, "internal Darwin linker failed (error %d)", result);
    assertf(file_exists(BUILD_OUTPUT), "internal Darwin linker did not create '%s'", BUILD_OUTPUT);
    log_debug("internal linker output --> %s", BUILD_OUTPUT);
    log_debug("build output --> %s", BUILD_OUTPUT);
    ld_options_deinit(&options);
    free(sysroot);
    free(sdk_version);
}

static void build_init(char *build_entry) {
    env_init();
    config_init();
    symbol_init();
    reg_init();
    global_var_unique_count = 0;

    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_opcode_init();
    }

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

    ct_caller_list = ct_list_new(sizeof(caller_t));
    ct_caller_data = 0;

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
            assert(vardef->var_decl.type.kind != TYPE_UNKNOWN);

            ast_var_decl_t *var_decl = &vardef->var_decl;
            asm_global_symbol_t *symbol = NEW(asm_global_symbol_t);
            symbol->name = var_decl->ident;
            symbol->size = var_decl->type.storage_size;
            symbol->value = vardef->global_data;
            slice_push(m->asm_global_symbols, symbol);
        }

        log_debug("[build_assembler] module=%s", m->source_path);
        // native closure，如果遇到 c_string, 需要在 symtab + data 中注册一条记录，然后在 .text 引用，
        // 所以有了这里的临时 closure var decls, 原则上， var_decl = global var，其和 module 挂钩

        slice_t *closures = slice_new();
        for (int j = 0; j < m->closures->count; ++j) {
            closure_t *c = m->closures->take[j];

            slice_concat(m->asm_global_symbols, c->asm_symbols);

            // 基于 symbol_name 读取引用次数, 如果没有被引用过则不做编译
            symbol_t *s = symbol_table_get_noref(c->fndef->symbol_name);
            assert(s);
            if (s->ref_count == 0 && !str_equal(c->fndef->symbol_name, FN_MAIN_LINKID)) {
                continue;
            }

            debug_asm(c);

            slice_push(closures, c);
        }
        m->closures = closures;

        assembler_module(m);
    }

    // append custom type
    ct_register_rtype(type_kind_new(TYPE_RAW_STRING));

    assembler_custom_links();
}

static slice_t *build_modules(toml_table_t *package_conf) {
    assertf(strlen(SOURCE_PATH) > 0, "SOURCE_PATH empty");

    table_t *module_table = table_new();
    slice_t *modules = slice_new();
    slice_t *builtin_modules = slice_new();

    // builtin modules
    char *builtin_dir = path_join(NATURE_ROOT, "std/builtin");
    // scan all builtin package, module build
    DIR *dir = opendir(builtin_dir);
    assertf(dir, "cannot found builtin dir %s", builtin_dir);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *filename = strdup(entry->d_name);
        if (!ends_with(filename, ".n")) {
            free(filename);
            continue;
        }

        char *full_path = path_join(builtin_dir, filename);
        free(filename);

        struct stat info;
        if (stat(full_path, &info) == 0 && S_ISREG(info.st_mode)) {
            slice_push(builtin_modules, full_path);
        } else {
            free(full_path);
        }
    }
    closedir(dir);

    qsort(builtin_modules->take, (size_t) builtin_modules->count,
          sizeof(builtin_modules->take[0]), build_path_compare);

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
            .module_ident = PACKAGE_MAIN_IDENT,
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

    module_t *main_package = module_build(&main_import, SOURCE_PATH, MODULE_TYPE_MAIN);
    slice_push(modules, main_package);

    linked_push(work_list, main_package);

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

            // new module dep all imports handled
            module_t *new_module = module_build(import, import->full_path, import->module_type);

            linked_push(work_list, new_module);
            table_set(module_table, import->full_path, new_module);

            // 按照层级进入到 modules 中(广度优先)
            slice_push(modules, new_module);
        }
    }

    if (BUILD_TEST) {
        test_inject_main(main_package);
    }

    // modules contains, 倒叙遍历处理依赖关系
    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];

        // analyzer => ast_fndefs(global)
        // analyzer 前需要将 global symbol 注册完成，否则在 pre_infer 时找不到相关的符号
        analyzer(m, m->stmt_list);
    }

    return modules;
}

static void cross_lower(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_lower(c);
        return;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        arm64_lower(c);
        return;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        riscv64_lower(c);
        return;
    }

    assert(false && "not support arch");
}

static inline void cross_peephole(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_peephole_pre(c);
        return;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        arm64_peephole_pre(c);
        return;
    }
    // RISCV64 暂不进行窥孔优化
}

static inline void cross_native(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_native(c);
        return;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        arm64_native(c);
        return;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        riscv64_native(c);
        return;
    }

    assert(false && "not support arch");
}

/**
 * Safe cleanup of TEMP_DIR after build completion
 * This function ensures all temporary files and directories are properly removed
 */
static void cleanup_temp_dir() {
    // verbose 模式下不删除编译临时目录，方便调试
    if (VERBOSE) {
        log_info("verbose mode: keeping temporary directory: %s", TEMP_DIR);
        return;
    }

    if (!TEMP_DIR || strlen(TEMP_DIR) == 0) {
        log_debug("TEMP_DIR is not set, skipping cleanup");
        return;
    }

    // Additional safety check: ensure TEMP_DIR looks like a temporary directory
    if (!strstr(TEMP_DIR, "/tmp/") && !strstr(TEMP_DIR, "nature-build")) {
        log_warn("TEMP_DIR path looks suspicious, skipping cleanup: %s", TEMP_DIR);
        return;
    }

    log_debug("Cleaning up temporary directory: %s", TEMP_DIR);

    int result = rmdir_recursive(TEMP_DIR);
    if (result == 0) {
        log_debug("Successfully cleaned up TEMP_DIR: %s", TEMP_DIR);
    } else {
        log_warn("Failed to clean up TEMP_DIR: %s (this may be normal if files are still in use)", TEMP_DIR);
    }
}

static void build_compiler(slice_t *modules) {
    // generics pass
    // module 基于广度 import 进入，倒叙遍历解决依赖问题
    for (int i = modules->count - 1; i >= 0; --i) {
        module_t *m = modules->take[i];
        generics(m);
    }

    // pre infer pass
    // 优先处理 builtin_module
    for (int i = modules->count - 1; i >= 0; --i) {
        module_t *m = modules->take[i];
        if (m->type == MODULE_TYPE_BUILTIN) {
            pre_infer(m);
        }
    }

    // module 基于广度 import 进入，倒叙遍历解决依赖问题
    for (int i = modules->count - 1; i >= 0; --i) {
        module_t *m = modules->take[i];
        if (m->type == MODULE_TYPE_BUILTIN) {
            continue;
        }
        pre_infer(m);
    }

    // global eval pass
    // 对全局变量做编译期求值与内存布局初始化
    for (int i = modules->count - 1; i >= 0; --i) {
        module_t *m = modules->take[i];
        global_eval(m);
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
            debug_lir(c, "linear");

            // 构造 cfg
            cfg(c);

            debug_block_lir(c, "cfg");

            // 构造 ssa
            ssa(c);

            debug_block_lir(c, "ssa");
            mark_number(c);

            // lir 向 arch 靠拢
            cross_lower(c);
            debug_block_lir(c, "lower");

            cross_peephole(c);
            debug_block_lir(c, "peephole");

            schedule(c);
            debug_block_lir(c, "schedule");

            // 线性扫描寄存器分配
            reg_alloc(c);
            debug_block_lir(c, "reg_alloc");

            // 基于 arch 生成汇编
            cross_native(c);

            debug_asm(c);
        }
    }
}


/**
 * 构建成 libmain.a 文件
 */
static void build_archive(slice_t *modules) {
    char *cmd = mallocz(10240 * sizeof(char));
    strcpy(cmd, "ar -rcs ");
    char *output = path_join(TEMP_DIR, "libmain.a");
    strcat(cmd, output);

    for (int i = 0; i < modules->count; ++i) {
        module_t *m = modules->take[i];
        char *obj_file = m->object_file;
        if (obj_file == NULL) {
            continue;
        }

        // 将每个模块的object文件添加到命令中
        strcat(cmd, " ");
        strcat(cmd, obj_file);
    }
    strcat(cmd, " ");
    strcat(cmd, custom_link_object_path());

    int result = system(cmd);
    if (result != 0) {
        assertf(false, "Compiling static library failed. ar command error: %d", result);
    }

    strcpy(BUILD_OUTPUT, path_join(BUILD_OUTPUT_DIR, "libmain.a"));

    remove(BUILD_OUTPUT);
    copy(BUILD_OUTPUT, output, 0755);
    log_debug("build temp output--> %s\n", output);
    log_debug("build output--> %s\n", BUILD_OUTPUT);
}

/**
 * nature build main.n, build_entry = "main.n"
 * nature build client/main.n build_entry = "client/main.n"
 * @param build_entry
 */
void build(char *build_entry, bool is_archive) {
    assertf(strlen(build_entry) > 2, "build entry=%s exception", build_entry);

#ifdef TEST_MODE
    printf("In test mode!\n");
#endif

#ifdef DEBUG_LOG
    log_set_level(LOG_DEBUG);
#else
    // verbose 模式下输出 log_debug 日志
    if (VERBOSE) {
        log_set_level(LOG_DEBUG);
    } else {
        log_set_level(LOG_INFO);
    }
#endif

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

    // if custom ld(macos default use linker ld)

    if (is_archive) {
        build_archive(modules);
        cleanup_temp_dir();
        return;
    }

    if (strlen(USE_LD) > 0) {
        if (BUILD_OS == OS_LINUX) {
            custom_ld_elf_exe(modules, USE_LD, LDFLAGS);
        } else if (BUILD_OS == OS_DARWIN) {
            custom_ld_mach_exe(modules, USE_LD, LDFLAGS);
        } else {
            assertf(BUILD_OS == OS_WINDOWS,
                    "unsupported external linker target");
            custom_ld_windows_exe(modules, USE_LD, LDFLAGS);
        }
    } else {
        if (BUILD_OS == OS_DARWIN) {
            if (BUILD_ARCH == ARCH_ARM64) {
                ld_mach_exe(modules, LDFLAGS);
            } else {
                assertf(BUILD_ARCH == ARCH_AMD64,
                        "unsupported Darwin target architecture");
#ifdef __DARWIN
                custom_ld_mach_exe(modules, "ld", LDFLAGS);
#else
                assertf(false,
                        "Darwin amd64 linking requires an external Mach-O "
                        "linker; specify one with --ld");
#endif
            }
        } else if (BUILD_OS == OS_LINUX) {
            ld_elf_exe(modules, LDFLAGS);
        } else {
            assertf(BUILD_OS == OS_WINDOWS,
                    "unsupported internal linker target");
            ld_windows_exe(modules, LDFLAGS);
        }
    }

    // Cleanup TEMP_DIR after successful build
    cleanup_temp_dir();
}

#ifndef NATURE_LINKER_H
#define NATURE_LINKER_H

/**
 * 包含 loader 和 linker 两部分
 */
#include <stdlib.h>
#include <stdint.h>
#include "src/binary/elf/elf.h"
#include "src/binary/mach/mach.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "src/types.h"
#include "src/build/config.h"
#include "utils/custom_links.h"
#include "src/symbol/symbol.h"
#include "src/lir.h"

#define CONFIG_NEW_MACHO 1

#define SEC_TACK(_sh_index) ((section_t *) ctx->sections->take[_sh_index])

#define SEC_START(_type, _section) ((_type*) _section->data)

#define SEC_END(_type, _section) ((_type*) (_section->data + _section->data_count))

#define addr_t uint64_t

# define REL_SECTION_FMT ".rela%s"


/* Whether to generate a GOT/PLT entry and when. NO_GOTPLT_ENTRY is first so
   that unknown relocation don't create a GOT or PLT entry */
enum gotplt_entry {
    NO_GOTPLT_ENTRY,    /* never generate (eg. GLOB_DAT & JMP_SLOT relocs) */
    BUILD_GOT_ONLY,    /* only build GOT (eg. TPOFF relocs) */
    AUTO_GOTPLT_ENTRY,    /* generate if sym is UNDEF */
    ALWAYS_GOTPLT_ENTRY    /* always generate (eg. PLTOFF relocs) */
};

/* relocation type for 32 bit data relocation */
#define R_DATA_32   R_X86_64_32S
#define R_DATA_PTR  R_X86_64_64
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_GLOB_DAT  R_X86_64_GLOB_DAT
#define R_COPY      R_X86_64_COPY
#define R_RELATIVE  R_X86_64_RELATIVE


/* special flag to indicate that the section should not be linked to the other ones */
#define SHF_PRIVATE 0x80000000
/* section is dynsymtab_section */
#define SHF_DYNSYM 0x40000000

#define ST_ASM_SET 0x04

#define ARMAG  "!<arch>\012"    /* For COFF and a.out archives */
#define ARFMAG "`\n"

typedef struct {
    char ar_name[16];           /* name of this member */
    char ar_date[12];           /* file mtime */
    char ar_uid[6];             /* owner uid; printed as decimal */
    char ar_gid[6];             /* owner gid; printed as decimal */
    char ar_mode[8];            /* file mode, printed as octal   */
    char ar_size[10];           /* file size, printed as decimal */
    char ar_fmag[2];            /* should contain ARFMAG */
} archive_header_t;


typedef struct {
    section_t *section; // 引用全局 section
    uint64_t offset;
    bool is_new; // 是否为当前 object file 中第一次定义的 section
    bool link_once;
} local_section_t;

/**
 * 加载归档文件
 * @param ctx
 */
void load_archive(elf_context_t *ctx, int fd);

/**
 * 加载可重定位目标文件文件到全局 section 中
 */
void load_object_file(elf_context_t *ctx, int fd, uint64_t file_offset);

/**
 * 从文件中加载数据到 section data 中
 * @param l
 */
void *elf_file_load_data(int fd, uint64_t offset, uint64_t size);


section_t *elf_section_new(elf_context_t *ctx, char *name, uint64_t sh_type, uint64_t sh_flags);

/**
 * 全局 section data 写入点
 * size 位需要写入的数据的长度
 * @return
 */
void *section_ptr_add(section_t *section, addr_t size);

/**
 * data_count forward
 * @return
 */
size_t elf_section_data_forward(section_t *section, addr_t size, uint64_t align);

void elf_section_realloc(section_t *section, uint64_t new_size);

uint64_t elf_set_sym(elf_context_t *ctx, Elf64_Sym *sym, char *name);

uint64_t elf_put_sym(section_t *symtab_section, table_t *symtab_hash, Elf64_Sym *sym, char *name);

uint64_t elf_put_str(section_t *s, char *str);

uint64_t elf_put_data(section_t *s, uint8_t *data, uint64_t size);

void elf_resolve_common_symbols(elf_context_t *ctx);

void elf_build_got_entries(elf_context_t *ctx, uint64_t got_sym_index);

Elf64_Rela *elf_put_relocate(elf_context_t *ctx, section_t *sym_section, section_t *apply_section,
                             uint64_t offset, int type, int sym_index, int64_t addend);

Elf64_Rela *
elf_put_rel_data(elf_context_t *ctx, section_t *apply_section, uint64_t rel_offset, char *name, uint64_t symbol_type);

void elf_relocate_symbols(elf_context_t *ctx, section_t *sym_section);

void elf_relocate_sections(elf_context_t *ctx);

void elf_relocate_section(elf_context_t *ctx, section_t *apply_section, section_t *rel_section);

sym_attr_t *elf_get_sym_attr(elf_context_t *ctx, uint64_t sym_index, bool alloc);

uint64_t elf_get_sym_addr(elf_context_t *ctx, char *name, bool force);

Elf64_Sym *elf_find_sym(elf_context_t *ctx, char *name);

void elf_fill_got(elf_context_t *ctx);

void elf_fill_got_entry(elf_context_t *ctx, Elf64_Rela *rel);

int tidy_section_headers(elf_context_t *ctx);

void sort_symbols(elf_context_t *ctx, section_t *s);

elf_context_t *elf_context_new(char *output, uint8_t type);

void alloc_section_names(elf_context_t *ctx, bool is_obj);

uint64_t elf_put_global_symbol(elf_context_t *ctx, char *name, void *value, uint8_t value_size);

/**
 * 基于 symbol fn 生成基础的 fn list
 */
static inline uint64_t collect_fndef_list(void *ctx) {
    ct_fndef_list = mallocz(symbol_fn_list->count * sizeof(fndef_t));

    uint64_t rel_offset = 0;

    uint64_t count = 0;
    uint64_t size_with_bits = 0;
    // - 遍历全局符号表中的所有 fn 数据就行了
    SLICE_FOR(symbol_fn_list) {
        symbol_t *s = SLICE_VALUE(symbol_fn_list);

        ast_fndef_t *fn = s->ast_value;
        closure_t *c = fn->closure;
        // builtin continue
        if (!c) {
            continue;
        }

        if (c->text_count == 0) {
            continue;
        }

        fndef_t *f = &ct_fndef_list[count++];
        f->fn_runtime_reg = c->fn_runtime_reg;
        f->fn_runtime_stack = c->fn_runtime_stack;
        f->stack_size = c->stack_offset;// native 的时候已经进行了 16byte 对齐了
        f->gc_bits = c->stack_gc_bits->bits;

        size_with_bits += sizeof(fndef_t);
        size_with_bits += calc_gc_bits_size(f->stack_size, POINTER_SIZE);

        strcpy(f->name, c->linkident);
        strcpy(f->rel_path, c->fndef->rel_path);
        f->line = c->fndef->line;
        f->column = c->fndef->column;

        f->base = 0;// 等待符号重定位
        assert(c->text_count > 0);
        f->size = c->text_count;// 至少要等所有等 module 都 assembly 完成才能计算出 text_count

        // 按从 base ~ top 的入栈顺序写入
        for (int i = 0; i < c->stack_vars->count; ++i) {
            lir_var_t *var = c->stack_vars->take[i];
            int64_t stack_slot = var_stack_slot(c, var);
            assert(stack_slot < 0);
            stack_slot = var_stack_slot(c, var) * -1;

            log_debug(
                    "[collect_fndef_list.%s] var ident=%s, type=%s, size=%d, is_ptr=%d, bit_index=%ld, stack_slot=BP-%ld",
                    c->linkident, var->ident, type_format(var->type), type_sizeof(var->type),
                    type_is_pointer_heap(var->type),
                    (stack_slot / POINTER_SIZE) - 1, stack_slot);
        }

        log_debug(
                "[collect_fndef_list] success, fn name=%s, base=0x%lx, size=%lu, stack=%lu,"
                "fn_runtime_stack=0x%lx, fn_runtime_reg=0x%lx, gc_bits(%lu)=%s",
                f->name, f->base, f->size, f->stack_size, f->fn_runtime_stack, f->fn_runtime_reg,
                f->stack_size / POINTER_SIZE,
                bitmap_to_str(f->gc_bits, f->stack_size / POINTER_SIZE));

        if (BUILD_OS == OS_LINUX) {
            elf_context_t *elf_ctx = ctx;
            elf_put_rel_data(ctx, elf_ctx->data_fndef_section, rel_offset, c->linkident, STT_FUNC);
        } else if (BUILD_OS == OS_DARWIN) {
            mach_context_t *mach_ctx = ctx;
            mach_put_rel_data(ctx, mach_ctx->data_fndef_section, rel_offset, c->linkident, N_UNDF | N_EXT);
        }

        rel_offset += sizeof(fndef_t);
    }
    ct_fndef_count = count;
    log_debug("[collect_fndef_list] count=%lu, size_with_bits=%lu", ct_fndef_count, size_with_bits);
    return size_with_bits;
}

static inline uint64_t collect_symdef_list(void *ctx) {
    uint64_t size = symbol_var_list->count * sizeof(symdef_t);
    ct_symdef_list = mallocz(size);
    uint64_t rel_offset = 0;
    uint64_t count = 0;
    SLICE_FOR(symbol_var_list) {
        symbol_t *s = SLICE_VALUE(symbol_var_list);
        if (s->is_local) {
            continue;
        }

        ast_var_decl_t *var_decl = s->ast_value;
        symdef_t *symdef = &ct_symdef_list[count++];
        symdef->need_gc = type_is_pointer_heap(var_decl->type);
        symdef->size = type_sizeof(var_decl->type);// 符号的大小
        symdef->base = 0;                          // 这里引用了全局符号表段地址
        strcpy(symdef->name, var_decl->ident);

        if (BUILD_OS == OS_LINUX) {
            elf_context_t *elf_ctx = ctx;
            elf_put_rel_data(ctx, elf_ctx->data_symdef_section, rel_offset, var_decl->ident, STT_OBJECT);
        } else if (BUILD_OS == OS_DARWIN) {
            mach_context_t *mach_ctx = ctx;
            mach_put_rel_data(ctx, mach_ctx->data_symdef_section, rel_offset, var_decl->ident, N_UNDF | N_EXT);
        }

        rel_offset += sizeof(symdef_t);
    }
    ct_symdef_count = count;
    size = ct_symdef_count * sizeof(symdef_t);
    ct_symdef_list = realloc(ct_symdef_list, size);
    log_debug("[collect_symdef_list] count=%lu, size=%ld", ct_symdef_count, size);

    return size;
}


#define START_LABEL "_start"

/**
 * 包含可重定位文件和可执行文件的输出
 */
void elf_output(elf_context_t *ctx);

/**
 * - 编译指令
 */
static inline void elf_file_format(elf_context_t *ctx) {
    alloc_section_names(ctx, 1);

    size_t file_offset = sizeof(Elf64_Ehdr);
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        file_offset = (file_offset + 15) & -16;
        s->sh_offset = file_offset;
        if (s->sh_type != SHT_NOBITS) {
            file_offset += s->sh_size;
        }
    }
    ctx->file_offset = file_offset;
}


/**
 * 构造 elf 可执行文件结构,依旧是段结构数据
 */
void elf_exe_file_format(elf_context_t *ctx);

static void *linker_realloc(void *ptr, unsigned long size) {
    void *ptr1;
    if (size == 0) {
        free(ptr);
        ptr1 = NULL;
    } else {
        ptr1 = realloc(ptr, size);
        assertf(ptr1, "memory full");
    }
    return ptr1;
}

#endif //NATURE_LINKER_H

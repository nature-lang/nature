#ifndef NATURE_MACH_H
#define NATURE_MACH_H

#include "src/build/config.h"
#include "src/types.h"
#include "utils/helper.h"
#include "utils/slice.h"
#include "utils/string_view.h"
#include "utils/table.h"
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>


enum {
    ARM64_RELOC_UNSIGNED = 0,
    ARM64_RELOC_SUBTRACTOR = 1,
    ARM64_RELOC_BRANCH26 = 2,
    ARM64_RELOC_PAGE21 = 3,
    ARM64_RELOC_PAGEOFF12 = 4,
    ARM64_RELOC_GOT_LOAD_PAGE21 = 5,
    ARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6,
    ARM64_RELOC_POINTER_TO_GOT = 7,
    ARM64_RELOC_TLVP_LOAD_PAGE21 = 8,
    ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9,
    ARM64_RELOC_ADDEND = 10,
};

enum {
    X86_64_RELOC_UNSIGNED = 0,
    X86_64_RELOC_SIGNED = 1,
    X86_64_RELOC_BRANCH = 2,
    X86_64_RELOC_GOT_LOAD = 3,
    X86_64_RELOC_GOT = 4,
    X86_64_RELOC_SUBTRACTOR = 5,
    X86_64_RELOC_SIGNED_1 = 6,
    X86_64_RELOC_SIGNED_2 = 7,
    X86_64_RELOC_SIGNED_4 = 8,
    X86_64_RELOC_TLV = 9,
};

typedef struct mach_section_t {
    struct section_64 section;

    int64_t data_offset;
    int64_t data_capacity; // 极限容量
    uint8_t *data; // 段二进制数据, 可以通过 realloc 扩容
    uint32_t sh_index;
    char name[1024];

    struct mach_section_t *relocate; // 当前段对应的重定位段
} mach_section_t;


// 需要用到两种类型的 load_command
// LC_SEGMENT_64 和 LC_SEGMENT_64
typedef struct {
    struct segment_command_64 sc;
} mach_segment_lc;

typedef struct {
    struct symtab_command sc;
    mach_section_t *symbols;

    mach_section_t *str_table; // str_data 数据存储
} mach_symtab_lc;

typedef struct {
    struct dysymtab_command dc;
} mach_dysymtab_command;

/**
 * 链接器核心数据结构，通用于 elf/macho
 */
typedef struct {
    // 常用 section 列表
    mach_section_t *data_section;
    mach_section_t *text_section;

    mach_section_t *data_rtype_section;
    mach_section_t *data_fndef_section;
    mach_section_t *data_symdef_section;
    mach_section_t *data_caller_section;

    // segment 在 obj 文件中并不会有实际的数据结构存储，而是包含在 section->segname 中
    // obj 文件通常只有一个 segment_command LC_SEGMENT_64 引导所有的 sections
    struct segment_command_64 segment_command;
    mach_symtab_lc *symtab_command;
    struct dysymtab_command dysymtab_command;

    slice_t *sections; // lc_segment_64 包含的所有 section

    uint64_t file_offset;
    char *output; // 完整路径名称
    uint8_t output_type;
} mach_context_t;

static inline void macho_section_realloc(mach_section_t *section, int64_t new_size) {
    int64_t size = section->data_capacity;
    if (size == 0) {
        size = 1;
    }
    while (size < new_size) {
        size = size * 2;
    }
    uint8_t *data = realloc(section->data, size);
    memset(data + section->data_capacity, 0, size - section->data_capacity); // 多余位置配置为 0
    section->data = data;
    section->data_capacity = size;
}

/**
 * align 要区分 elf 的 sh_addralign, elf 的 align 是直接的字节对齐，0 或者 1 表示不需要字节对齐
 * macho 对齐 2 的 ^
 */
static inline size_t macho_section_data_forward(mach_section_t *ms, uint64_t size, uint64_t align) {
    align = 1 << align;

    size_t offset, offset_end;
    offset = (ms->data_offset + align - 1) & -align;
    offset_end = offset + size;

    if (offset_end > ms->data_capacity) {
        macho_section_realloc(ms, offset_end);
    }
    ms->data_offset = offset_end; // forward

    if (align > ms->section.align) {
        ms->section.align = align;
    }

    return offset;
}

static inline void *mach_section_ptr_add(mach_section_t *ms, uint64_t size) {
    size_t offset = macho_section_data_forward(ms, size, 0); // 0 表示 1 字节对齐
    return ms->data + offset;
}

static inline int64_t mach_put_data(mach_section_t *ms, uint8_t *data, int64_t size) {
    char *ptr = mach_section_ptr_add(ms, size);

    if (data) {
        memmove(ptr, data, size);
    } else {
        memset(ptr, 0, size);
    }

    return (int64_t) ptr - (int64_t) ms->data;
}

static uint64_t mach_put_str(mach_section_t *str_table, string_view_t *str) {
    size_t len = str->size + 1; // 预留 \0
    uint64_t offset = str_table->data_offset;

    char *ptr = mach_section_ptr_add(str_table, len);

    memmove(ptr, str->data, len);
    ptr[str->size] = '\0';
    return offset;
}

/**
 * macos 下所有 symbol 都以 "_" 开头
 */
static inline int64_t mach_put_sym(mach_symtab_lc *lc, struct nlist_64 *sym, char *name) {
    // 开辟一点空间处理符号
    struct nlist_64 *new_sym = mach_section_ptr_add(lc->symbols, sizeof(struct nlist_64));
    uint64_t name_offset = 0; // name 在 str_table 中的 offset
    if (name && name[0]) {
        char *prefixed_name = mallocz(strlen(name) + 2); // +2 为了 "_" 和 '\0'
        sprintf(prefixed_name, "_%s", name);


        name_offset = mach_put_str(lc->str_table, string_view_create(prefixed_name, strlen(prefixed_name)));
    }

    memcpy(new_sym, sym, sizeof(struct nlist_64));
    new_sym->n_un.n_strx = name_offset;

    int64_t sym_index = new_sym - (struct nlist_64 *) lc->symbols->data;

    lc->sc.nsyms += 1;
    return sym_index;
}

static inline int64_t macho_put_global_symbol(mach_context_t *ctx, char *name, void *value, int64_t value_size) {
    int64_t offset = mach_put_data(ctx->data_section, value, value_size);

    struct nlist_64 sym = {
            .n_type = N_SECT | N_EXT,
            .n_sect = ctx->data_section->sh_index, // 定义符号值的的段的索引
            .n_value = offset, // 定义符号值在段的便宜
            .n_desc = 0,
    };

    mach_put_sym(ctx->symtab_command, &sym, name);
    return offset;
}

static inline mach_section_t *mach_section_new(mach_context_t *ctx, char *sectname, char *segname, uint32_t flag) {
    mach_section_t *mc = NEW(mach_section_t);

    strncpy(mc->name, sectname, 1024);
    strncpy(mc->section.sectname, sectname, 16);
    strncpy(mc->section.segname, segname, 16);

    // 设置默认值
    mc->section.align = 2; // 默认对齐 2*2 = 4byte
    mc->section.flags = flag; // 默认标志
    mc->sh_index = ctx->sections->count + 1; // 从 1 开始计数

    slice_push(ctx->sections, mc);

    return mc;
}


/**
 * relocation_info 结构体通常存储在每个需要重定位的段（section）之后
 */
static inline struct relocation_info *
mach_put_relocate(mach_context_t *ctx, mach_section_t *apply_section, int32_t offset, uint32_t type,
                  int32_t symbol_index) {
    if (!apply_section->relocate) {
        apply_section->relocate = NEW(mach_section_t);
    }

    mach_section_t *ms = apply_section->relocate;

    // 创建重定位信息
    struct relocation_info *rel = mach_section_ptr_add(ms, sizeof(struct relocation_info));
    rel->r_address = offset;
    rel->r_symbolnum = symbol_index;
    rel->r_pcrel = 0; // 默认不是 PC 相对重定位
    rel->r_length = 2; // 默认为 4 字节 (2^2 = 4)
    rel->r_extern = 1; // 默认为外部符号
    rel->r_type = type;

    // 根据重定位类型调整字段
    if (BUILD_ARCH == ARCH_AMD64) {
        switch (type) {
            // X86_64 重定位类型
            case X86_64_RELOC_SIGNED:
            case X86_64_RELOC_BRANCH:
            case X86_64_RELOC_GOT_LOAD:
            case X86_64_RELOC_GOT:
            case X86_64_RELOC_TLV:
                rel->r_pcrel = 1;
                break;
            case X86_64_RELOC_UNSIGNED:
                rel->r_length = 3; // 8字节 (2^3 = 8)
                break;
        }
    } else {
        switch (type) {
            // ARM64 重定位类型
            case ARM64_RELOC_UNSIGNED:
                rel->r_length = 3; // 8字节
                break;
            case ARM64_RELOC_BRANCH26:
            case ARM64_RELOC_PAGE21:
            case ARM64_RELOC_GOT_LOAD_PAGE21:
            case ARM64_RELOC_GOT_LOAD_PAGEOFF12:
            case ARM64_RELOC_POINTER_TO_GOT:
            case ARM64_RELOC_TLVP_LOAD_PAGE21:
                rel->r_pcrel = 1;
                break;
            case ARM64_RELOC_TLVP_LOAD_PAGEOFF12:
            case ARM64_RELOC_ADDEND:
                // ADDEND 类型通常不需要特殊处理
                break;
                // 可以根据需要添加更多类型的处理
        }
    }

    // 更新重定位计数
    apply_section->section.nreloc++;
    return rel;
}


/**
 * 符号总是使用
 */
static inline struct relocation_info *
mach_put_rel_data(mach_context_t *ctx, mach_section_t *apply_section, int32_t rel_offset,
                  char *name, uint64_t symbol_type) {

    uint64_t sym_index = mach_put_sym(ctx->symtab_command,
                                      &(struct nlist_64) {
                                              .n_sect = NO_SECT, // 重定位符号无法知道符号定义的 section
                                              .n_type = symbol_type,
                                              .n_value = 0},
                                      name);

    return mach_put_relocate(ctx, apply_section, rel_offset, X86_64_RELOC_UNSIGNED, sym_index);
}

static inline void reorder_symtab(mach_context_t *ctx) {
    mach_symtab_lc *symtab = ctx->symtab_command;
    struct nlist_64 *symbols = (struct nlist_64 *) symtab->symbols->data;
    uint32_t nsyms = symtab->sc.nsyms;
    assert(nsyms > 0);

    // 创建临时数组来存储排序后的符号
    struct nlist_64 *sorted_symbols = mallocz(nsyms * sizeof(struct nlist_64));
    uint32_t *old_to_new_index = mallocz(nsyms * sizeof(uint32_t));
    for (uint32_t i = 0; i < nsyms; i++) {
        old_to_new_index[i] = i; // 初始化为原始索引
    }

    uint32_t local_count = 0;
    uint32_t external_count = 0;
    uint32_t undefined_count = 0;

    // 第一遍：计算每种类型的符号数量
    for (uint32_t i = 0; i < nsyms; i++) {
        // 符号校验，n_sect = NO_SECT 时， n_type 不能是 N_SECT
        if (symbols[i].n_sect == NO_SECT && (symbols[i].n_type & N_SECT) != 0) {
            assert(false);
        }

        if ((symbols[i].n_type & N_EXT) == 0) {
            local_count++;
        } else if ((symbols[i].n_type & N_TYPE) == N_UNDF) {
            undefined_count++;
        } else {
            external_count++;
        }
    }

    // 第二遍：重新排序符号, 并调整 section index
    uint32_t local_index = 0;
    uint32_t external_index = local_count;
    uint32_t undefined_index = local_count + external_count;

    for (uint32_t i = 0; i < nsyms; i++) {
        // section 可能调整过，所以需要更新 section index
        uint8_t n_sect = symbols[i].n_sect;
        if (n_sect > 0) {
            mach_section_t *ms = ctx->sections->take[n_sect - 1];
            symbols[i].n_sect = ms->sh_index;
            //            log_debug("symbol sect index change %d -> %d", n_sect, ms->sh_index);

            // 基于 section addr 从新计算 symbol 在段表中的偏移
            symbols[i].n_value = ms->section.addr + symbols[i].n_value;
        }


        if ((symbols[i].n_type & N_EXT) == 0) {
            sorted_symbols[local_index] = symbols[i];
            old_to_new_index[i] = local_index++;
        } else if ((symbols[i].n_type & N_TYPE) == N_UNDF) {
            sorted_symbols[undefined_index] = symbols[i];
            old_to_new_index[i] = undefined_index++;
        } else {
            sorted_symbols[external_index] = symbols[i];
            old_to_new_index[i] = external_index++;
        }
    }

    // 将排序后的符号复制回原数组
    memcpy(symbols, sorted_symbols, nsyms * sizeof(struct nlist_64));
    free(sorted_symbols);

    // 更新 dysymtab_command 的相关值
    ctx->dysymtab_command.ilocalsym = 0;
    ctx->dysymtab_command.nlocalsym = local_count;
    ctx->dysymtab_command.iextdefsym = local_count;
    ctx->dysymtab_command.nextdefsym = external_count;
    ctx->dysymtab_command.iundefsym = local_count + external_count;
    ctx->dysymtab_command.nundefsym = undefined_count;

    // 更新重定位条目
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (sec->relocate) {
            struct relocation_info *rel = (struct relocation_info *) sec->relocate->data;

            for (uint32_t j = 0; j < sec->section.nreloc; j++) {
                uint32_t old_idx = rel[j].r_symbolnum;
                rel[j].r_symbolnum = old_to_new_index[old_idx];
                //                rel[j].r_address = sec->section.addr + rel[j].r_address;
            }
        }
    }
}

/**
 * [Mach-O Header]
[LC_SEGMENT_64 Command]
    [segment_command_64 structure]
    [section_64 structure for section 1]
    [section_64 structure for section 2]
    ...
    [section_64 structure for section n]
[LC_SYMTAB Command]
[LC_DYSYMTAB Command]
[Actual section data]
[Symbol table]
[String table]
[Other data if any]
 */
static inline bool mach_output_object(mach_context_t *ctx) {
    assertf(ctx->output_type == OUTPUT_OBJ, "only support output object");
    // 无符号返回
    if (ctx->symtab_command->symbols->data_offset == 0) {
        return false;
    }

    FILE *f;
    unlink(ctx->output);
    int fd = open(ctx->output, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0777);
    if (fd < 0 || (f = fdopen(fd, "wb")) == NULL) {
        assertf(false, "[elf_output] could not write '%s: %s'", ctx->output);
        return false;
    }

    size_t file_offset = 0;

    // header 写入
    struct mach_header_64 hdr = {};
    hdr.magic = MH_MAGIC_64;
    if (BUILD_ARCH == ARCH_AMD64) {
        hdr.cputype = CPU_TYPE_X86_64;
        hdr.cpusubtype = CPU_SUBTYPE_X86_64_ALL;
    } else {
        hdr.cputype = CPU_TYPE_ARM64;
        hdr.cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    }

    hdr.filetype = MH_OBJECT;
    hdr.ncmds = 0; /* to be modified */
    hdr.sizeofcmds = 0; /* to be modified */
    hdr.flags = MH_SUBSECTIONS_VIA_SYMBOLS;
    hdr.ncmds = 3; // segment + symtab + dysymtab

    uint32_t sec_struct_size = 0;
    uint32_t nsects = 0;
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (sec->data_offset == 0) { // 空 section 不写入 segment
            continue;
        }

        sec->section.size = sec->data_offset;
        sec_struct_size += sizeof(struct section_64);
        nsects += 1;
        sec->sh_index = nsects; // index 从 1 开始计算
    }

    hdr.sizeofcmds = sizeof(struct segment_command_64) + sec_struct_size + sizeof(struct symtab_command) +
                     sizeof(struct dysymtab_command);

    fwrite(&hdr, 1, sizeof(hdr), f);
    file_offset += sizeof(struct mach_header_64);

    uint32_t sec_offset = sizeof(struct mach_header_64) + hdr.sizeofcmds;
    uint32_t addr = 0;
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (sec->data_offset == 0) { // 空 section 不写入 segment
            continue;
        }

        sec->section.size = sec->data_offset;
        sec->section.addr = addr;
        sec->section.offset = sec_offset;

        uint32_t new_addr = addr + align_up(sec->section.size, 1 << sec->section.align);
        sec_offset += (new_addr - addr);
        addr = new_addr;
    }

    uint32_t sec_data_size = sec_offset - sizeof(struct mach_header_64) - hdr.sizeofcmds;

    // write segment load command and then section struct
    struct segment_command_64 segment = ctx->segment_command;
    segment.cmdsize = sizeof(struct segment_command_64) + sec_struct_size;

    segment.filesize = sec_data_size;
    segment.vmsize = sec_data_size;
    segment.fileoff = hdr.sizeofcmds + sizeof(hdr);
    segment.nsects = nsects;
    fwrite(&segment, 1, sizeof(segment), f);
    file_offset += sizeof(segment);

    // write section struct
    size_t rel_offset = sec_offset;
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (sec->data_offset == 0) {
            continue;
        }

        if (sec->relocate) {
            sec->section.reloff = rel_offset;
            rel_offset += sec->relocate->data_offset;
        }

        // 无论段中是否有数据，都写入段表结构，避免后续的异常
        fwrite(&sec->section, 1, sizeof(struct section_64), f);
        file_offset += sizeof(struct section_64);
    }

    // write symbol command
    ctx->symtab_command->sc.symoff = rel_offset;
    ctx->symtab_command->sc.strsize = ctx->symtab_command->str_table->data_offset;
    ctx->symtab_command->sc.stroff = ctx->symtab_command->sc.symoff + ctx->symtab_command->symbols->data_offset;

    reorder_symtab(ctx);

    // debug println 所有的 symbol 名称，sect, 以及 type
    //    struct nlist_64 *symbols = (struct nlist_64 *) ctx->symtab_command->symbols->data;
    //    for (int i = 0; i < ctx->symtab_command->sc.nsyms; i++) {
    //        struct nlist_64 *sym = &symbols[i];
    //        log_debug("index: %d, symbol: %s, sect: %d, type: %d\n", i, ctx->symtab_command->str_table->data + sym->n_un.n_strx, sym->n_sect, sym->n_type);
    //    }

    fwrite(&ctx->symtab_command->sc, 1, sizeof(struct symtab_command), f);
    file_offset += sizeof(struct symtab_command);

    // write dysymtab
    fwrite(&ctx->dysymtab_command, 1, sizeof(struct dysymtab_command), f);
    file_offset += sizeof(struct dysymtab_command);

    // write sections data
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (sec->data_offset == 0) {
            continue;
        }


        // file_offset 对齐, 对不起的地方补 0
        while (file_offset < sec->section.offset) {
            fputc(0, f);
            file_offset++;
        }

        fwrite(sec->data, 1, sec->section.size, f);
        file_offset += sec->section.size;
    }

    while (file_offset < sec_offset) {
        fputc(0, f);
        file_offset++;
    }

    assert(file_offset == sec_offset);

    // write rel data
    for (int i = 0; i < ctx->sections->count; ++i) {
        mach_section_t *sec = ctx->sections->take[i];
        if (!sec->relocate) {
            continue;
        }

        assert(file_offset == sec->section.reloff);

        // log_debug
        //        struct relocation_info *rel = (struct relocation_info *) sec->relocate->data;
        //        for (size_t j = 0; j < sec->section.nreloc; j++) {
        //            assert(rel[j].r_address < sec->section.size);
        //            log_debug("relocate: %d, symbol_index: %d", rel[j].r_address, rel[j].r_symbolnum);
        //        }

        fwrite(sec->relocate->data, 1, sec->relocate->data_offset, f);
        file_offset += sec->relocate->data_offset;
    }

    // write symbol data (symbol value section in file offset + current_offset)
    assert(ctx->symtab_command->sc.symoff == file_offset);
    fwrite(ctx->symtab_command->symbols->data, 1, ctx->symtab_command->symbols->data_offset, f);
    file_offset += ctx->symtab_command->symbols->data_offset;


    // write string table
    fwrite(ctx->symtab_command->str_table->data, 1, ctx->symtab_command->str_table->data_offset, f);
    file_offset += ctx->symtab_command->str_table->data_offset;

    fclose(f);

    return true;
}

static inline mach_context_t *mach_context_new(char *output) {
    mach_context_t *ctx = NEW(mach_context_t);

    ctx->output = output;
    // only support output object
    ctx->output_type = OUTPUT_OBJ;

    ctx->sections = slice_new();

    ctx->text_section = mach_section_new(ctx, "__text", "__TEXT",
                                         S_REGULAR | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS);
    ctx->text_section->section.align = 4; // 2^4按照 16 byte 对齐

    ctx->data_section = mach_section_new(ctx, "__data", "__DATA", S_REGULAR);


    ctx->segment_command.cmd = LC_SEGMENT_64;
    ctx->segment_command.maxprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    ctx->segment_command.initprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;

    ctx->symtab_command = NEW(mach_symtab_lc);
    ctx->symtab_command->symbols = NEW(mach_section_t); // 只使用到了 data 段
    ctx->symtab_command->str_table = NEW(mach_section_t);
    mach_put_str(ctx->symtab_command->str_table, string_view_create("", 0));


    ctx->symtab_command->sc.cmd = LC_SYMTAB;
    ctx->symtab_command->sc.cmdsize = sizeof(struct symtab_command);

    ctx->dysymtab_command.cmd = LC_DYSYMTAB;
    ctx->dysymtab_command.cmdsize = sizeof(struct dysymtab_command);

    return ctx;
}

#endif //NATURE_MACH_H

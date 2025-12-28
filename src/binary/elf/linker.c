#include "src/binary/linker.h"

#include <ar.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"
#include "src/binary/arch/amd64.h"
#include "src/binary/arch/arm64.h"
#include "src/binary/arch/riscv64.h"
#include "utils/custom_links.h"
#include "utils/error.h"

static uint64_t elf_create_plt_entry(elf_context_t *ctx, uint64_t got_offset, sym_attr_t *attr) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_create_plt_entry(ctx, got_offset, attr);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_create_plt_entry(ctx, got_offset, attr);
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return riscv64_create_plt_entry(ctx, got_offset, attr);
    }

    assert(false && "not support arch");
}

static int8_t elf_is_code_relocate(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_is_code_relocate(relocate_type);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_is_code_relocate(relocate_type);
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return riscv64_is_code_relocate(relocate_type);
    }
    assert(false && "not support arch");
}

static int elf_got_rel_type(bool is_code_rel) {
    if (BUILD_ARCH == ARCH_AMD64) {
        if (is_code_rel) {
            return R_X86_64_JUMP_SLOT;
        } else {
            return R_X86_64_GLOB_DAT;
        }
    } else if (BUILD_ARCH == ARCH_ARM64) {
        if (is_code_rel) {
            return R_AARCH64_JUMP_SLOT;
        } else {
            return R_AARCH64_GLOB_DAT;
        }
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        if (is_code_rel) {
            return R_RISCV_JUMP_SLOT;
        } else {
            return R_RISCV_64;
        }
    }

    assert(false && "not support arch");
}

static int elf_gotplt_entry_type(uint64_t relocate_type) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_gotplt_entry_type(relocate_type);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_gotplt_entry_type(relocate_type);
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return riscv64_gotplt_entry_type(relocate_type);
    }
    assert(false && "not support arch");
}

static inline uint64_t elf_start_addr() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ELF_START_ADDR;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return ARM64_ELF_START_ADDR;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return RISCV64_ELF_START_ADDR;
    }
    assert(false && "not support arch");
}

static inline uint64_t elf_page_size() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_64_ELF_PAGE_SIZE;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return ARM64_ELF_PAGE_SIZE;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return RISCV64_ELF_PAGE_SIZE;
    }
    assert(false && "not support arch");
    return 0;
}

static inline void elf_relocate(elf_context_t *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return elf_amd64_relocate(l, rel, type, ptr, addr, val);
    }

    if (BUILD_ARCH == ARCH_ARM64) {
        return elf_arm64_relocate(l, rel, type, ptr, addr, val);
    }

    if (BUILD_ARCH == ARCH_RISCV64) {
        return elf_riscv64_relocate(l, rel, type, ptr, addr, val);
    }

    assert(false && "not support arch");
}

static inline uint16_t elf_ehdr_machine() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return EM_X86_64;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return EM_AARCH64;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return EM_RISCV;
    }

    assert(false && "not support arch");
}


static int sort_sections(elf_context_t *ctx) {
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        int base_weight;
        int sub_weight;
        {
            if (s->sh_flags & SHF_ALLOC) {
                base_weight = 0x100;
                if (s->sh_flags & SHF_WRITE) {
                    base_weight = 0x200;
                }
                //                if (s->sh_flags & SHF_TLS) {
                //                    base_weight += 0x200;
                //                }
            } else if (s->sh_name) {
                base_weight = 0x700;
            } else {
                base_weight = 0x900; // 符号表 section 最终会在这里被排除掉
            }

            if (s->sh_type == SHT_SYMTAB || s->sh_type == SHT_DYNSYM) {
                sub_weight = 0x10;
            } else if (s->sh_type == SHT_STRTAB && !str_equal(s->name, ".stabstr")) {
                sub_weight = 0x11;
                if (sh_index == ctx->sections->count - 1) {
                    sub_weight = 0xff;
                }
            } else if (s->sh_type == SHT_HASH) {
                sub_weight = 0x12;
            } else if (s->sh_type == SHT_RELA) {
                sub_weight = 0x20;
                if (ctx->plt && s == ctx->plt->relocate) {
                    sub_weight = 0x21;
                }
            } else if (str_equal(s->name, ".data.rel.ro")) {
                sub_weight = 0x45;
            } else if (s->sh_type == SHT_DYNAMIC) {
                sub_weight = 0x46;
            } else if (s == ctx->got) {
                sub_weight = 0x47;
            } else {
                sub_weight = 0x50;
                if (s->sh_type == SHT_NOTE) {
                    sub_weight = 0x60;
                }
                if (s->sh_flags & SHF_EXECINSTR) {
                    sub_weight = 0x40; // 使 .text 排在 .rodata 之前
                }
                if (s->sh_type == SHT_NOBITS) {
                    sub_weight = 0x80;
                }
            }
        }

        int weight = base_weight + sub_weight;
        int n;
        int temp;
        // 倒序遍历(大-> 小) 选择一个刚好能卡住 k 的位置
        // 对于每一个比 k 大的值，都将其位置后移一位
        for (n = sh_index; n > 1 && weight < (temp = SEC_TACK(n - 1)->actual_sh_weight); --n) {
            SEC_TACK(n)->actual_sh_weight = temp; // 前移一位，留出空位，直到遇到一个 f 小于 k 的部分，就退出循环
            SEC_TACK(n)->actual_sh_index = SEC_TACK(n - 1)->actual_sh_index; // 位置后移一位， actual_sh_index 中存储了 sh_index
        }

        SEC_TACK(n)->actual_sh_weight = weight;
        SEC_TACK(n)->actual_sh_index = sh_index;
    }
    // SEC_TACK(0)->actual_sh_index = 0;

    // 写入 order flags
    int phdr_count = 0;
    uint64_t prev_flags = 0;
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        // 读取应该存在 sh_index 位置的 section
        int weight = SEC_TACK(sh_index)->actual_sh_weight;
        int actual_sh_index = SEC_TACK(sh_index)->actual_sh_index;
        section_t *s = SEC_TACK(actual_sh_index);

        uint64_t flags = 0;
        if (weight >= 0x700) {
            s->phdr_flags = flags;
            continue;
        }
        flags = s->sh_flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR);
        // 忽略掉前4位，只看后面几位，
        // 只要第 (sh_info link) 和 9 (Section is member of a group.) 位有值
        // 就表名该 section 为 RELRO sections
        // 0xfff0 = 1111 1111 1111 0000
        // 0x240 =  0000 0010 0100 0000
        if ((weight & 0xfff0) == 0x240) {
            flags |= 1 << 4; // 开启标志位 SHF_MERGE,Might be merged
        } /* RELRO (Relocation Read-Only) sections */

        // 添加标识位
        if (flags != prev_flags) {
            // log_debug("new segment: %s", s->name);
            prev_flags = flags;
            ++phdr_count; // 开启一个新的段, phdr_count 表示程序表的段的数量
            // 把 00000001 -8-> 10000000  第 8 位是预留位，可以随便定义
            // 此处用来标志开启了新的 segment
            flags |= 1 << 8;
        } /* start new header when flags changed or relro */
        // log_debug("section_name: %s, sh_index: %d, actual_sh_index: %d", s->name, s->sh_index, s->actual_sh_index);

        s->phdr_flags = flags;
    }

    // 预留一个 PT_TLS 段
    if (ctx->tdata_section) {
        ++phdr_count;
    }

    return phdr_count;
}

/**
 * 同类型段进行排序
 * @param ctx
 */
static void layout_sections(elf_context_t *ctx) {
    int phdr_count = sort_sections(ctx);
    ctx->phdr_count = phdr_count;
    ctx->phdr_list = mallocz(phdr_count * sizeof(Elf64_Phdr));

    uint64_t file_offset = sizeof(Elf64_Ehdr) + phdr_count * sizeof(Elf64_Phdr);
    addr_t s_align = elf_page_size();
    addr_t addr = elf_start_addr();
    addr_t base = addr; // elf 文件的起始虚拟内存地址
    addr = addr + (file_offset & (s_align - 1));
    int n = 0;
    Elf64_Phdr *phdr;

    // 记录 TLS 区间
    uint64_t tls_vaddr = 0, tls_offset = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 0;
    int tls_found = 0;

    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        int order_index = SEC_TACK(sh_index)->actual_sh_index;
        section_t *s = SEC_TACK(order_index);
        uint64_t flags = s->phdr_flags;
        uint64_t align = s->sh_addralign - 1;
        if (flags == 0) { // no alloc in memory
            file_offset = (file_offset + align) & ~align; // ~align 按位取反
            s->sh_offset = file_offset;
            if (s->sh_type != SHT_NOBITS) {
                file_offset += s->sh_size;
            }
            continue;
        }

        // 右数第 8 位如果为 1，标识可装载(can alloc)
        if ((flags & 1 << 8) && n) {
            if ((addr & (s_align - 1)) != 0) {
                addr += s_align;
            }
        }

        uint64_t tmp = addr;
        addr = (addr + align) & ~align;
        file_offset += (int) (addr - tmp); // 对齐差值，多个 section 共用一个 page 从而可以节省内存
        s->sh_offset = file_offset;
        s->sh_addr = addr;

        // 记录 TLS 区间（只记录第一个 SHF_TLS 段的起始，最后一个的结束）
        if (s->sh_flags & SHF_TLS) {
            if (!tls_found) {
                tls_vaddr = addr;
                tls_offset = file_offset;
                tls_align = s->sh_addralign;
            }
            tls_found = 1;
            tls_filesz += (s->sh_type != SHT_NOBITS) ? s->sh_size : 0;
            tls_memsz += s->sh_size;
        }

        // 位移优先级高于按位与
        // x = 1 << 8 =  00000000 00000000 00000000 100000000
        // f & x  = 最终判断第 8 位的值是否为 1, 为 1 表示需要创建一个新的 program header
        // pt load 段写入(可装载段)
        if (flags & 1 << 8) {
            phdr = &ctx->phdr_list[n]; // ph fill 看起来像是预留的段空间
            /* set new program header */
            phdr->p_type = PT_LOAD;
            phdr->p_align = s_align;
            phdr->p_flags = PF_R;
            if (flags & SHF_WRITE) {
                phdr->p_flags |= PF_W;
            }
            if (flags & SHF_EXECINSTR) {
                phdr->p_flags |= PF_X;
            }
            // if (flags & SHF_TLS) {
            // phdr->p_type = PT_TLS;
            // phdr->p_align = align + 1;
            // }

            phdr->p_offset = file_offset;
            phdr->p_vaddr = addr;
            if (n == 0) {
                /* Make the first PT_LOAD segment include the program
                   headers itself (and the ELF header as well), it'll
                   come out with same memory use but will make various
                   tools like binutils strip work better.  */
                phdr->p_offset = 0;
                phdr->p_vaddr = base;
            }
            phdr->p_paddr = phdr->p_vaddr;
            ++n;
        }

        addr += s->sh_size;
        // nobit 不需要真的申请文件位置，比如 data [1000] 你都没有初始化，直接在数据段申请 1000 个空间，那进程的空间就太大了
        if (s->sh_type != SHT_NOBITS) {
            file_offset += s->sh_size;
        }

        phdr->p_filesz = file_offset - phdr->p_offset;
        phdr->p_memsz = addr - phdr->p_vaddr;
    }

    // 额外添加 PT_TLS 段，描述 TLS 区间
    if (tls_found) {
        phdr = &ctx->phdr_list[n++];
        phdr->p_type = PT_TLS;
        phdr->p_offset = tls_offset;
        phdr->p_vaddr = tls_vaddr;
        phdr->p_paddr = tls_vaddr;
        phdr->p_filesz = tls_filesz;
        phdr->p_memsz = tls_memsz;
        phdr->p_flags = PF_R;
        phdr->p_align = tls_align ? tls_align : 1;
    }

    ctx->file_offset = file_offset;
}

/**
 * @param ctx
 * @param is_obj 是否为可重定位目标文件 (.o) 只有目标文件需要 sh_size
 */
void alloc_section_names(elf_context_t *ctx, bool is_obj) {
    section_t *shstr_section;
    shstr_section = elf_section_new(ctx, ".shstrtab", SHT_STRTAB, 0);
    elf_put_str(shstr_section, "");
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = ctx->sections->take[sh_index];
        if (is_obj) {
            s->sh_size = s->data_count;
        }
        if (s == shstr_section || s->sh_size || (s->sh_flags & SHF_ALLOC)) {
            s->sh_name = elf_put_str(shstr_section, s->name);
        }
    }

    shstr_section->sh_size = shstr_section->data_count;
}

static int set_section_sizes(elf_context_t *ctx) {
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = ctx->sections->take[sh_index];
        if (s->data_count == 0) {
            continue;
        }

        if (s->sh_flags & SHF_ALLOC || s->sh_type == SHT_SYMTAB || s->sh_type == SHT_STRTAB) {
            s->sh_size = s->data_count; // 直接使用 data_count 覆盖
        }
    }
    return 0;
}

static uint64_t build_got(elf_context_t *ctx) {
    ctx->got = elf_section_new(ctx, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    ctx->got->sh_entsize = 4;
    section_ptr_add(ctx->got, 3 * POINTER_SIZE);

    Elf64_Sym sym = {
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_other = 0,
            .st_shndx = ctx->got->sh_index,
    };
    return elf_set_sym(ctx, &sym, "_GLOBAL_OFFSET_TABLE_");
}

sym_attr_t *elf_get_sym_attr(elf_context_t *ctx, uint64_t sym_index, bool alloc) {
    if (sym_index >= ctx->sym_attrs_count) {
        if (!alloc) {
            return ctx->sym_attrs;
        }

        uint64_t n = 1;
        while (sym_index >= n) {
            n *= 2;
        }
        sym_attr_t *ptr = realloc(ctx->sym_attrs, n * sizeof(*ctx->sym_attrs));
        ctx->sym_attrs = ptr;
        // 初始化刚刚申请的空间
        memset(ctx->sym_attrs + ctx->sym_attrs_count, 0, (n - ctx->sym_attrs_count) * sizeof(*ctx->sym_attrs));
        ctx->sym_attrs_count = n;
    }

    return &ctx->sym_attrs[sym_index]; // 符号始终在 ctx 中
}

/**
 * 符号在符号表未定义，或者重定位的类型为 PLT/GOT
 * @param ctx
 * @param relocate_type
 * @param sym_index
 * @return
 */
static sym_attr_t *put_got_entry(elf_context_t *ctx, int relocate_type, uint64_t sym_index) {
    char plt_name[200];
    bool need_plt_entry = relocate_type == R_X86_64_JUMP_SLOT;

    if (BUILD_ARCH == ARCH_ARM64) {
        need_plt_entry = relocate_type == R_AARCH64_JUMP_SLOT;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        need_plt_entry = relocate_type == R_RISCV_JUMP_SLOT;
    }


    sym_attr_t *attr = elf_get_sym_attr(ctx, sym_index, true);
    uint64_t offset = need_plt_entry ? attr->plt_offset : attr->got_offset;
    if (offset) {
        return attr;
    }

    uint64_t got_offset = ctx->got->data_count;
    section_ptr_add(ctx->got, POINTER_SIZE);

    Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
    char *sym_name = (char *) ctx->symtab_section->link->data + sym->st_name;
    elf_put_relocate(ctx, ctx->symtab_section, ctx->got, got_offset, relocate_type, (int) sym_index, 0);

    if (need_plt_entry) {
        // 基于 amd64 的 plt entry
        attr->plt_offset = elf_create_plt_entry(ctx, got_offset, attr);

        size_t len = strlen(sym_name);
        if (len > sizeof(plt_name) - 5) {
            len = sizeof(plt_name) - 5;
        }

        memcpy(plt_name, sym_name, len);
        strcpy(plt_name + len, "@plt");

        Elf64_Sym *new_sym = NEW(Elf64_Sym);
        new_sym->st_value = attr->plt_offset;
        new_sym->st_size = 0;
        new_sym->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
        new_sym->st_other = 0;
        new_sym->st_shndx = ctx->plt->sh_index;
        attr->plt_sym = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, new_sym, plt_name);
    } else {
        attr->got_offset = got_offset;
    }

    return attr;
}

void load_object_file(elf_context_t *ctx, int fd, uint64_t file_offset) {
    assert(fd >= 0);

    lseek(fd, file_offset, SEEK_SET);
    Elf64_Ehdr ehdr; // 清 0
    Elf64_Shdr *shdr;
    ssize_t read_size = full_read(fd, &ehdr, sizeof(ehdr));
    assert(read_size == sizeof(ehdr) && "read elf header failed");
    assert(ehdr.e_type == ET_REL && "not a relocatable file");

    // read section header table
    Elf64_Shdr *shdr_list = elf_file_load_data(fd, file_offset + ehdr.e_shoff, sizeof(*shdr) * ehdr.e_shnum);
    // 当前 obj file 中的符号中的 sh_index 都是当前 obj 文件中的 index，需要这样一个 local_sections
    // 充当当前 sh_index -> global sh_index 的映射
    local_section_t *local_sections = mallocz(sizeof(local_section_t) * ehdr.e_shnum);

    // 加载段表字符串表 e_shstrndx = Section header string table index
    shdr = &shdr_list[ehdr.e_shstrndx]; // 必须使用引用，否则无法修改 shdr 的值
    char *shstrtab = elf_file_load_data(fd, file_offset + shdr->sh_offset, shdr->sh_size);

    // 加载符号表,单个可中定位文件中只允许存在一个符号表
    Elf64_Sym *symtab = NULL;
    Elf64_Sym *sym;
    uint64_t sym_count = 0;
    char *strtab = NULL;
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        shdr = &shdr_list[sh_index];
        if (shdr->sh_type != SHT_SYMTAB) {
            continue;
        }
        if (symtab) {
            error_exit("[load_object_file] object must contain only on symtab");
        }

        sym_count = shdr->sh_size / sizeof(*sym);
        // 加载符号数据
        symtab = elf_file_load_data(fd, file_offset + shdr->sh_offset, shdr->sh_size);
        local_sections[sh_index].section = ctx->symtab_section; // 通过 index 进行全局关联

        // 加载符号字符串表
        shdr = &shdr_list[shdr->sh_link];
        strtab = elf_file_load_data(fd, file_offset + shdr->sh_offset, shdr->sh_size);
    }

    // 遍历所有的段进行段合并(合并到 ctx 中)(按段名称合并，而不是段的类型, 其中不包含符号表，主要是对 data、text 段的合并)
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        // 跳过段表字符串表
        if (sh_index == ehdr.e_shstrndx) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        if (shdr->sh_type == SHT_RELA) {
            shdr = &shdr_list[shdr->sh_info];
        }

        if (shdr->sh_type != SHT_PROGBITS && shdr->sh_type != SHT_NOTE && shdr->sh_type != SHT_NOBITS &&
            shdr->sh_type != SHT_PREINIT_ARRAY && shdr->sh_type != SHT_INIT_ARRAY && shdr->sh_type != SHT_FINI_ARRAY &&
            !str_equal(shstrtab + shdr->sh_name, ".stabstr")) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        char *shdr_name = shstrtab + shdr->sh_name;
        // 检查是否为 .tbss 段
        bool is_tbss = str_equal(shdr_name, ".tbss");

        // 基于前缀的合并策略：将 .text.xxx 合并到 .text, .data.xxx 合并到 .data 等
        char *merge_name = shdr_name;
        if (strncmp(shdr_name, ".text.", 6) == 0) {
            merge_name = ".text";
        } else if (strncmp(shdr_name, ".data.rel.ro.local.", 19) == 0) {
            merge_name = ".data.rel.ro.local";
        } else if (strncmp(shdr_name, ".data.rel.local.", 16) == 0) {
            merge_name = ".data.rel.local";
        } else if (strncmp(shdr_name, ".data.rel.ro.", 13) == 0) {
            merge_name = ".data.rel.ro";
        } else if (strncmp(shdr_name, ".data.rel.", 10) == 0) {
            merge_name = ".data.rel";
        } else if (strncmp(shdr_name, ".data.", 6) == 0) {
            merge_name = ".data";
        } else if (strncmp(shdr_name, ".rodata.", 8) == 0) {
            merge_name = ".rodata";
        } else if (strncmp(shdr_name, ".bss.", 5) == 0) {
            merge_name = ".bss";
        } else if (strncmp(shdr_name, ".init_array.", 12) == 0) {
            merge_name = ".init_array";
        } else if (strncmp(shdr_name, ".fini_array.", 12) == 0) {
            merge_name = ".fini_array";
        }

        section_t *global_section;
        if (is_tbss) {
            global_section = ctx->tdata_section;
            assert(global_section);
        } else {
            // n * n 的遍历查找，使用 merge_name 进行匹配
            for (int i = 1; i < ctx->sections->count; ++i) {
                global_section = SEC_TACK(i);
                if (str_equal(global_section->name, merge_name)) {
                    // 在全局 sections 中找到了同名 section
                    goto FOUND;
                }
            }

            // not found in ctx->sections, will create new section (使用 merge_name)
            global_section = elf_section_new(ctx, merge_name, shdr->sh_type, shdr->sh_flags & ~SHF_GROUP);
            global_section->sh_addralign = shdr->sh_addralign;
            global_section->sh_entsize = shdr->sh_entsize;
            local_sections[sh_index].is_new = true;
        }


    FOUND:
        if (!is_tbss) {
            // 在ctx 中找到了同名 section 但是段类型不相同
            assertf(shdr->sh_type == global_section->sh_type, "[load_object_file] sh %s code invalid", shdr_name);
        }
        Elf64_Word sh_type = global_section->sh_type;


        // align
        global_section->data_count += -global_section->data_count & (shdr->sh_addralign - 1);
        // local shdr > global section
        if (shdr->sh_addralign > global_section->sh_addralign) {
            global_section->sh_addralign = shdr->sh_addralign;
        }
        local_sections[sh_index].offset = global_section->data_count;
        local_sections[sh_index].section = global_section;

        // 将 local section 数据写入到全局 section 中
        uint64_t sh_size = shdr->sh_size;
        if (shdr->sh_type == SHT_NOBITS) { // 预留空间单没数据
            if (is_tbss) { // 由于需要合并到 tdata 中，所以进行特殊处理
                elf_section_data_forward(global_section, sh_size, 1);
            } else {
                global_section->data_count += sh_size;
            }
        } else {
            unsigned char *ptr;
            lseek(fd, file_offset + shdr->sh_offset, SEEK_SET); // 移动 fd 指向的文件的偏移点
            ptr = section_ptr_add(global_section, sh_size);
            full_read(fd, ptr, sh_size);
        };
    }

    // section_t *debug_section = SEC_TACK(6);

    // 完善 section 中的关联关系，比如 section.link
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *section = local_sections[sh_index].section;
        if (!section || !local_sections[sh_index].is_new) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        if (shdr->sh_link > 0) {
            section->link = local_sections[shdr->sh_link].section; // 这里的 section 为全局 section
        }

        // 重定位段表的 sh_link 保存对应的符号表
        // sh_info 保存重定位表的目标表，如果 .rel.text -> .text
        if (shdr->sh_type == SHT_RELA) {
            // 修正为 global section 中的目标
            section_t *target_section = local_sections[shdr->sh_info].section;
            section->sh_info = target_section->sh_index;
            // 添加反向 link, section 已经是经过合并操作的全局唯一 section 了
            target_section->relocate = section;
        }
    }

    // 符号重定位
    uint64_t *symtab_index_map = mallocz(sizeof(uint64_t) * sym_count);
    // 遍历所有符号(符号表的第一个符号为 NULL)
    sym = symtab + 1;
    for (int i = 1; i < sym_count; ++i, sym++) {
        // add symbol
        char *sym_name = strtab + sym->st_name;

        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            local_section_t *local = &local_sections[sym->st_shndx]; // st_shndx 定义符号的段
            if (!local->section) {
                continue;
            }

            // convert section index
            sym->st_shndx = local->section->sh_index;
            // sm->slot 是 section' data 合并到全局 section 时的起始地址，所以 st_value 自然要加上这个起始地址
            sym->st_value += local->offset;
        }

        // 当前目标文件中定义的符号
        uint64_t sym_index = elf_set_sym(ctx, sym, sym_name);
        // i 表示的是当前可中定位文件中的符号索引，而 sym_index 则是全局符号表中的索引
        symtab_index_map[i] = sym_index;
    }

    // rela patch
    // 当然并不是所有的符号都已经被 patch 了，不过没关系，先指向 undef 就行，一旦符号被加载进来就会自动修复的，而不是重新建
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *rel_section = local_sections[sh_index].section;
        if (!rel_section) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        uint64_t offset = local_sections[sh_index].offset;
        uint64_t sh_size = shdr->sh_size;

        if (rel_section->sh_type != SHT_RELA) {
            continue;
        }

        // 找到当前 rel_section,当前 rela 中的 sym_index 和 apply offset 还在还是 local 的，需要改成 global

        // 应用 rel 的 section
        uint64_t apply_offset = local_sections[shdr->sh_info].offset;
        Elf64_Rela *rel;
        for (rel = (Elf64_Rela *) rel_section->data + (offset / sizeof(Elf64_Rela));
             rel < (Elf64_Rela *) rel_section->data + ((offset + sh_size) / sizeof(Elf64_Rela)); rel++) {
            uint64_t rel_type = ELF64_R_TYPE(rel->r_info); // 重定位类型
            uint64_t sym_index = ELF64_R_SYM(rel->r_info); // 重定位符号在符号表中的索引

            assertf(sym_index < sym_count, "rel sym index exception");

            uint64_t new_sym_index = symtab_index_map[sym_index];

            if (!new_sym_index && rel_type != R_RISCV_ALIGN && rel_type != R_RISCV_RELAX) {
                assertf(false, "patch new sym index == 0 zero, old sym index is %d", sym_index);
            }

            rel->r_info = ELF64_R_INFO(new_sym_index, rel_type);
            rel->r_offset += apply_offset;
        }
    }
}

size_t elf_section_data_forward(section_t *section, uint64_t size, uint64_t align) {
    size_t offset, offset_end;
    offset = (section->data_count + align - 1) & -align;
    offset_end = offset + size;
    if (section->sh_type != SHT_NOBITS && offset_end > section->data_capacity) {
        elf_section_realloc(section, offset_end);
    }
    section->data_count = offset_end; // forward
    if (align > section->sh_addralign) {
        section->sh_addralign = align;
    }

    return offset;
}

void *section_ptr_add(section_t *section, uint64_t size) {
    size_t offset = elf_section_data_forward(section, size, 1);
    return section->data + offset;
}

uint64_t elf_set_sym(elf_context_t *ctx, Elf64_Sym *sym, char *name) {
    section_t *s = ctx->symtab_section;
    table_t *symbol_table = ctx->symtab_hash;
    uint64_t sym_bind = ELF64_ST_BIND(sym->st_info);
    uint64_t sym_type = ELF64_ST_TYPE(sym->st_info);
    uint8_t sym_visible = ELF64_ST_VISIBILITY(sym->st_other);
    Elf64_Sym *exist_sym = NULL;
    uint64_t sym_index = 0;

    if (sym_bind != STB_LOCAL) {
        // 全局搜索符号查看是否已经被定义
        sym_index = (uint64_t) table_get(symbol_table, name);
        if (sym_index == 0) {
            goto DEF;
        }
        exist_sym = &((Elf64_Sym *) s->data)[sym_index];

        if (exist_sym->st_value == sym->st_value && exist_sym->st_size == sym->st_size &&
            exist_sym->st_info == sym->st_info &&
            exist_sym->st_other == sym->st_other && exist_sym->st_shndx == sym->st_shndx) {
            return sym_index;
        }

        if (exist_sym->st_shndx == SHN_UNDEF) {
            exist_sym->st_other = sym->st_other;
            goto PATCH;
        } else {
            // 当前段中的符号与全局符号同名

            // 计算可见性
            uint64_t exist_sym_bind = ELF64_ST_BIND(exist_sym->st_info);
            uint8_t exist_sym_visible = ELF64_ST_VISIBILITY(exist_sym->st_other);
            /* propagate the most constraining visibility */ // 使用约束力最强的可见性等级,越往右约束性越强
            /* STV_DEFAULT(0)<STV_PROTECTED(3)<STV_HIDDEN(2)<STV_INTERNAL(1) */
            uint8_t new_visible;
            if (sym_visible == STV_DEFAULT) {
                new_visible = exist_sym_visible;
            } else {
                // 排除 default 的情况下，值越小约束性越强
                new_visible = (exist_sym_visible < sym_visible) ? exist_sym_visible : sym_visible;
            }
            exist_sym->st_other = (exist_sym->st_other & ~ELF64_ST_VISIBILITY(-1)) | new_visible;

            // 同名覆盖关系
            if (sym->st_shndx == SHN_UNDEF) {
                // 保留原有符号，什么都不做
                return sym_index;
            } else if (sym_bind == STB_GLOBAL && exist_sym_bind == STB_WEAK) {
                // 强符号覆盖弱符号
                goto PATCH;
            } else if (sym_bind == STB_WEAK && exist_sym_bind == STB_GLOBAL) {
                return sym_index;
            } else if (sym_bind == STB_WEAK && exist_sym_bind == STB_WEAK) {
                return sym_index;
            } else if (sym_visible == STV_HIDDEN || sym_visible == STV_INTERNAL) {
                return sym_index;
            } else if ((exist_sym->st_shndx == SHN_COMMON || exist_sym->st_shndx == ctx->bss_section->sh_index) &&
                       (sym->st_shndx < SHN_LORESERVE && sym->st_shndx != ctx->bss_section->sh_index)) {
                goto PATCH;
            } else if (sym->st_shndx == SHN_COMMON || sym->st_shndx == ctx->bss_section->sh_index) {
                return sym_index;
            } else if (s->sh_flags & SHF_DYNSYM) {
                return sym_index;
            } else if (exist_sym->st_other & ST_ASM_SET) {
                goto PATCH;
            } else {
                assertf(false, "symbol '%s' repeat defined", name);
            }
        }
    } else {
        // local 符号
        goto DEF;
    }

PATCH:
    exist_sym->st_info = ELF64_ST_INFO(sym_bind, sym_type);
    exist_sym->st_shndx = sym->st_shndx;
    exist_sym->st_value = sym->st_value;
    exist_sym->st_size = sym->st_size;
    return sym_index;

DEF:
    sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, sym, name);
    return sym_index;
}

uint64_t elf_put_sym(section_t *symtab_section, table_t *symtab_hash, Elf64_Sym *sym, char *name) {
    Elf64_Sym *new_sym = section_ptr_add(symtab_section, sizeof(Elf64_Sym));
    uint64_t name_offset = 0;
    if (name && name[0]) {
        name_offset = elf_put_str(symtab_section->link, name);
    }

    new_sym->st_name = name_offset;
    new_sym->st_value = sym->st_value;
    new_sym->st_size = sym->st_size;
    new_sym->st_info = sym->st_info;
    new_sym->st_other = sym->st_other;
    new_sym->st_shndx = sym->st_shndx;
    uint64_t sym_index = new_sym - (Elf64_Sym *) symtab_section->data; // 终点 - 起点 = 数量

    if (name && strlen(name) > 0) {
        table_set(symtab_hash, name, (void *) sym_index);
    }

    return sym_index;
}

uint64_t elf_put_str(section_t *s, char *str) {
    size_t len = strlen(str) + 1; // 预留一个 \0
    uint64_t offset = s->data_count;

    char *ptr = section_ptr_add(s, len);
    memmove(ptr, str, len);
    return offset;
}

uint64_t elf_put_data(section_t *s, uint8_t *data, uint64_t size) {
    char *ptr = section_ptr_add(s, size);
    // 如果 data 为 null, 则填入 0
    if (data) {
        memmove(ptr, data, size);
    } else {
        memset(ptr, 0, size);
    }
    return (uint64_t) ptr - (uint64_t) s->data;
}

/**
 * - resolve common sym
 * - build got entries
 * - recalculate segment size
 * - pre_handle custom links
 * - alloc section name
 * - layout sections
 * - elf_relocate global_symbols
 * - elf_relocate sections
 * - fill got
 * - output execute
 */
void elf_exe_file_format(elf_context_t *ctx) {
    elf_resolve_common_symbols(ctx);

    elf_build_got_entries(ctx, 0);

    set_section_sizes(ctx);

    alloc_section_names(ctx, 0);

    layout_sections(ctx);

    elf_relocate_symbols(ctx, ctx->symtab_section);

    elf_relocate_sections(ctx);

    elf_fill_got(ctx);
}

void elf_resolve_common_symbols(elf_context_t *ctx) {
    Elf64_Sym *sym;
    for (sym = SEC_START(Elf64_Sym, ctx->symtab_section) + 1; sym < SEC_END(Elf64_Sym, ctx->symtab_section); sym++) {
        if (sym->st_shndx != SHN_COMMON) {
            continue;
        }
        char *name = (char *) ctx->symtab_section->link->data + sym->st_name;

        // 数据段的符号值一般是 .data 段的起始位置 st_value 前进
        sym->st_value = elf_section_data_forward(ctx->bss_section, sym->st_size, sym->st_value);
        // 修正符号定义段为 bss
        sym->st_shndx = ctx->bss_section->sh_index;
    }
}

/* Returns an enumerator to describe whether and when the relocation needs a
   GOT and/or PLT entry to be created. See tcc.h for a description of the
   different values. */

/**
 * plt/got 是一种重定位的方式，所以这里都是围绕着重定位段进行的
 * 基于重定位的类型
 * @param ctx
 * @param got_sym_index
 */
void elf_build_got_entries(elf_context_t *ctx, uint64_t got_sym_index) {
    Elf64_Rela *rel;

    // 一次遍历(基于 R_JMP_SLOT 构建 plt 段)
    int pass = 0;
REDO:
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = ctx->sections->take[sh_index];
        // 仅需要符号表重定位表
        if (s->sh_type != SHT_RELA) {
            continue;
        }
        if (s->link != ctx->symtab_section) {
            continue;
        }

        for (rel = (Elf64_Rela *) s->data; rel < (Elf64_Rela *) (s->data + s->data_count); rel++) {
            uint64_t type = ELF64_R_TYPE(rel->r_info); // 重定位类型
            uint64_t sym_index = ELF64_R_SYM(rel->r_info);
            Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
            char *name = (char *) ctx->symtab_section->link->data + sym->st_name;

            int gotplt_type = elf_gotplt_entry_type(type);
            if (gotplt_type == -1) {
                assertf(false, "[elf_build_got_entries] unknown relocation code for got: %d", type);
            }

            if (gotplt_type == NO_GOTPLT_ENTRY) {
                continue;
            }

            if (gotplt_type == AUTO_GOTPLT_ENTRY) {
                if (sym->st_shndx == SHN_ABS && sym->st_value == 0) {
                    continue;
                }
                if (sym->st_shndx != SHN_UNDEF) {
                    continue;
                }
            }

            // 架构相关代码 if arch == amd64
            if (BUILD_ARCH == ARCH_AMD64) {
                // 重定位类型改写， amd64 下 这两种的类型的重定位不参与 PLT 和 GOT 的构建
                if (type == R_X86_64_PLT32 || type == R_X86_64_PC32 && sym->st_shndx != SHN_UNDEF) {
                    rel->r_info = ELF64_R_INFO(sym_index, R_X86_64_PC32);
                    continue;
                }
            }

            // 代码段 PLT 构建
            int8_t is_code_rel = elf_is_code_relocate(type);
            if (is_code_rel == -1) {
                assertf(false, "[elf_build_got_entries] unknown relocation code for got: %d", type);
            }

            bool is_code_pass = pass == 0;
            bool is_data_pass = pass == 1;
            bool is_data_rel = !is_code_rel;

            // 代码段重定位仅在第一轮进行
            if (is_code_rel && !is_code_pass) {
                continue;
            }

            // 数据段重定位仅在第二轮进行
            if (is_data_rel && !is_data_pass) {
                continue;
            }

            if (is_code_rel && !ctx->plt) {
                // new plt
                ctx->plt = elf_section_new(ctx, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
                ctx->plt->sh_entsize = 4;
            }

            if (!ctx->got) {
                // build got
                got_sym_index = build_got(ctx);
            }

            int rel_type = elf_got_rel_type((bool) is_code_rel);
            sym_attr_t *attr = put_got_entry(ctx, rel_type, sym_index);

            if (is_code_rel) {
                rel->r_info = ELF64_R_INFO(attr->plt_sym, type);
            }
        }
    }
    if (++pass < 2) {
        goto REDO;
    }

    if (ctx->plt && ctx->plt->relocate) {
        ctx->plt->relocate->sh_info = ctx->got->sh_index; // 重定位表的 sh_info 存储的应用重定位的段
    }
    if (got_sym_index) {
        ((Elf64_Sym *) ctx->symtab_section->data)[got_sym_index].st_size = ctx->got->data_count;
    }
}

Elf64_Rela *
elf_put_rel_data(elf_context_t *ctx, section_t *apply_section, uint64_t rel_offset, char *name, uint64_t symbol_type) {
    Elf64_Sym sym = {
            .st_shndx = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, symbol_type),
            .st_other = 0,
            .st_value = 0,
    };
    // 如果遍历没有找到符号则会添加一条  UND 符号信息到符号表中
    //  10: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND string_new
    uint64_t sym_index = elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, name);


    int reloc_type;
    if (BUILD_ARCH == ARCH_AMD64) {
        reloc_type = R_X86_64_64;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        reloc_type = R_AARCH64_ABS64; // 使用适合 ARM64 的重定位类型
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        reloc_type = R_RISCV_64; // 使用适合 RISC-V 64 的重定位类型
    } else {
        assert(false && "Unsupported architecture");
    }

    return elf_put_relocate(ctx, ctx->symtab_section, apply_section, rel_offset, reloc_type, (int) sym_index, 0);
}

/**
 * rela section 不存在时会自动创建
 * @param ctx
 * @param sym_section
 * @param apply_section
 * @param offset
 * @param type
 * @param sym_index
 * @param addend
 * @return
 */
Elf64_Rela *
elf_put_relocate(elf_context_t *ctx, section_t *sym_section, section_t *apply_section, uint64_t offset, int type,
                 int sym_index,
                 int64_t addend) {
    char buf[1024];
    section_t *rel_section = apply_section->relocate;
    if (!rel_section) {
        char *temp_name = strdup(apply_section->name);
        assert(strlen(temp_name) < sizeof(buf) - 24); // 预留出 real. 空间
        snprintf(buf, sizeof(buf), REL_SECTION_FMT, temp_name);

        rel_section = elf_section_new(ctx, buf, SHT_RELA, SHF_INFO_LINK);
        rel_section->sh_entsize = sizeof(Elf64_Rela);
        rel_section->link = sym_section;
        rel_section->sh_info = apply_section->sh_index;
        apply_section->relocate = rel_section;
    }

    Elf64_Rela *rel = section_ptr_add(rel_section, sizeof(Elf64_Rela));
    rel->r_offset = offset;
    rel->r_info = ELF64_R_INFO(sym_index, type);
    rel->r_addend = addend;
    return rel;
}

void elf_relocate_symbols(elf_context_t *ctx, section_t *sym_section) {
    Elf64_Sym *sym;

    for (sym = SEC_START(Elf64_Sym, sym_section) + 1; sym < SEC_END(Elf64_Sym, sym_section); sym++) {
        int sh_index = sym->st_shndx; // 定义符号的 section, 如 data 或者 tls data

        char *name = (char *) ctx->symtab_section->link->data + sym->st_name;

        if (sh_index == SHN_UNDEF) {
            // libc 中 "_DYNAMIC" 唯一未定义的符号就是这个，还是个 STB_WEAK 类型的符号
            //            char *name = (char *) ctx->symtab_section->link->data + sym->st_name;
            /* XXX: _fp_hw seems to be part of the ABI, so we ignore
               it */
            if (!strcmp(name, "_fp_hw")) goto FOUND;
            /* only weak global_symbols are accepted to be undefined. Their
               value is zero */
            // 静态链接中只有弱符号允许未定义，他们的值固定位 0
            uint64_t sym_bind = ELF64_ST_BIND(sym->st_info);
            if (sym_bind == STB_WEAK) {
                sym->st_value = 0;
            } else {
                assertf(false, "undefined symbol '%s'", name);
            }
        } else if (sh_index < SHN_LORESERVE) {
            // 对于可重定位文件， st_value 保存了基于 section 的偏移
            // 对于可执行文件， st_value 则需要保存实际的线性地址了，也就是 section 的绝对地址 + st_value
            /* add section base */
            section_t *s = SEC_TACK(sh_index);

            // 对于 TLS 变量，需要计算其相对于 TLS 段起始的偏移
            sym->st_value += s->sh_addr;
        }
    FOUND:;
    }
}

void elf_relocate_sections(elf_context_t *ctx) {
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *rel_section = SEC_TACK(sh_index);
        if (rel_section->sh_type != SHT_RELA) {
            continue;
        }

        section_t *apply_section = SEC_TACK(rel_section->sh_info);

        elf_relocate_section(ctx, apply_section, rel_section);
    }
}

/**
 * 根据 rel code 进行重定位,和 cpu 相关
 * @param ctx
 * @param apply_section
 * @param rel_section
 */
void elf_relocate_section(elf_context_t *ctx, section_t *apply_section, section_t *rel_section) {
    Elf64_Sym *sym = NULL;
    Elf64_Rela *rel = NULL;
    uint8_t *ptr = NULL;
    for (rel = SEC_START(Elf64_Rela, rel_section); rel < SEC_END(Elf64_Rela, rel_section); rel++) {
        // 异常的重定向项，暂时不处理
        if (rel->r_offset > apply_section->data_capacity) {
            // TODO 为什么会出现这种情况
            // elf_section_realloc(apply_section, rel->r_offset);
            // assert(apply_section->data_capacity >= rel->r_offset);
            continue;
        }

        // s 为目标端, s->data 为段起始指针，其实就是 0, rel->r_offset 是基于目标段的偏移
        // 但是总的来说 ptr 都是待修正的符号的位置
        ptr = apply_section->data + rel->r_offset;

        // rel 引用的符号在符号表的索引，在没有修正之前就是 UNDEF, 不过 load elf 的时候已经进行了不停的修正，所以这里已经不会是 undef 了
        int sym_index = ELF64_R_SYM(rel->r_info);
        sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index]; // 符号

        char *name = (char *) ctx->symtab_section->link->data + sym->st_name;

        int type = ELF64_R_TYPE(rel->r_info); // code 就是重定位的类型
        addr_t target = sym->st_value; // 符号定义的位置
        target += rel->r_addend; // 从当前指令的结尾开始算起，比如 target - 4, 相当于 addr + 4

        // s->sh_addr 应该就是目标段的地址，加上 r_offset 就是绝对的地址修正了？
        // 没看出来 ptr 和 adr 的区别
        uint64_t addr = apply_section->sh_addr + rel->r_offset;


        elf_relocate(ctx, rel, type, ptr, addr, target);
    }
}

void elf_fill_got(elf_context_t *ctx) {
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        if (s->sh_type != SHT_RELA) {
            continue;
        }

        if (s->link != ctx->symtab_section) {
            continue;
        }

        Elf64_Rela *rel;
        for (rel = SEC_START(Elf64_Rela, s); rel < SEC_END(Elf64_Rela, s); rel++) {
            if (BUILD_ARCH == ARCH_AMD64) {
                switch (ELF64_R_TYPE(rel->r_info)) {
                    case R_X86_64_GOT32:
                    case R_X86_64_GOTPCREL:
                    case R_X86_64_GOTPCRELX:
                    case R_X86_64_REX_GOTPCRELX:
                    case R_X86_64_PLT32:
                        elf_fill_got_entry(ctx, rel);
                        break;
                }
            } else if (BUILD_ARCH == ARCH_ARM64) {
                switch (ELF64_R_TYPE(rel->r_info)) {
                    case R_AARCH64_ADR_GOT_PAGE:
                    case R_AARCH64_LD64_GOT_LO12_NC:
                    case R_AARCH64_ADR_PREL_PG_HI21:
                        elf_fill_got_entry(ctx, rel);
                        break;
                }
            } else if (BUILD_ARCH == ARCH_RISCV64) {
                switch (ELF64_R_TYPE(rel->r_info)) {
                    case R_RISCV_GOT_HI20:
                    case R_RISCV_PCREL_HI20:
                    case R_RISCV_PCREL_LO12_I:
                    case R_RISCV_PCREL_LO12_S:
                    case R_RISCV_CALL_PLT:
                        elf_fill_got_entry(ctx, rel);
                        break;
                }
            }
        }
    }
}

void elf_fill_got_entry(elf_context_t *ctx, Elf64_Rela *rel) {
    int sym_index = ELF64_R_SYM(rel->r_info);
    Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
    char *sym_name = (char *) ctx->symtab_section->link->data + sym->st_name;
    sym_attr_t *attr = elf_get_sym_attr(ctx, sym_index, 0);
    unsigned offset = attr->got_offset;

    if (offset == 0) {
        return;
    }
    if (offset + POINTER_SIZE > ctx->got->data_capacity) {
        elf_section_realloc(ctx->got, offset + POINTER_SIZE);
    }
    if (offset + POINTER_SIZE > ctx->got->data_count) {
        ctx->got->data_count = offset + POINTER_SIZE;
    }

    if (POINTER_SIZE == 8) {
        write64le(ctx->got->data + offset, sym->st_value);
    } else {
        write32le(ctx->got->data + offset, sym->st_value);
    };
}

int tidy_section_headers(elf_context_t *ctx) {
    int *back_map = malloc(ctx->sections->count * sizeof(int));
    section_t **new_section_tack = malloc(sizeof(void *) * ctx->sections->capacity);
    int new_section_count = 0;
    int tail = ctx->sections->count;
    for (int sh_index = 0; sh_index < ctx->sections->count; ++sh_index) {
        // actual sh index 表示应该在当前位置的 section
        int actual_sh_index = 0;
        section_t *s = NULL;
        if (sh_index > 0) {
            actual_sh_index = SEC_TACK(sh_index)->actual_sh_index;
            s = SEC_TACK(actual_sh_index);
        }
        if (sh_index == 0 || s->sh_name) {
            back_map[actual_sh_index] = new_section_count; // count 就是 new sh_index
            new_section_tack[new_section_count++] = s;
        } else {
            back_map[actual_sh_index] = 0; //
            new_section_tack[--tail] = s; // 不需要输出到可执行文件的 section 放在尾部
        }
    }

    for (int sh_index = 0; sh_index < new_section_count; ++sh_index) {
        section_t *s = new_section_tack[sh_index];
        if (!s) {
            continue;
        }

        s->sh_index = sh_index;
        if (s->sh_type == SHT_RELA) {
            s->sh_info = back_map[s->sh_info];
        }
    }

    Elf64_Sym *sym;
    section_t *sym_section = ctx->symtab_section;
    for (sym = SEC_START(Elf64_Sym, sym_section) + 1; sym < SEC_END(Elf64_Sym, sym_section); sym++) {
        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            sym->st_shndx = back_map[sym->st_shndx];
        }
    }
    ctx->sections->take = (void **) new_section_tack;

    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        s->actual_sh_index = sh_index;
    }

    return new_section_count;
}

section_t *elf_section_new(elf_context_t *ctx, char *name, uint64_t sh_type, uint64_t sh_flags) {
    section_t *s = NEW(section_t);
    strncpy(s->name, name, 1024);

    s->sh_type = sh_type;
    s->sh_flags = sh_flags;
    // log_debug("[elf_new_section] name: %s, sh_type: %lu, sh_flags: %lu", name, sh_type, sh_flags);

    switch (sh_type) {
        case SHT_GNU_versym:
            s->sh_addralign = 2;
            break;
        case SHT_HASH:
        case SHT_REL:
        case SHT_RELA:
        case SHT_DYNSYM:
        case SHT_SYMTAB:
        case SHT_DYNAMIC:
        case SHT_GNU_verneed:
        case SHT_GNU_verdef:
            s->sh_addralign = POINTER_SIZE;
            break;
        case SHT_STRTAB:
            s->sh_addralign = 1;
            break;
        default:
            s->sh_addralign = POINTER_SIZE; /* gcc/pcc default alignment */
            break;
    }
    if (sh_flags & SHF_PRIVATE) {
        slice_push(ctx->private_sections, s);
    } else {
        s->sh_index = ctx->sections->count;
        slice_push(ctx->sections, s);
    }

    return s;
}

static char *pstrcpy(char *buf, size_t buf_size, const char *s) {
    char *q, *q_end;
    int c;

    if (buf_size > 0) {
        q = buf;
        q_end = buf + buf_size - 1;
        while (q < q_end) {
            c = *s++;
            if (c == '\0')
                break;
            *q++ = c;
        }
        *q = '\0';
    }
    return buf;
}

/**
 * 符号可能是标签中的符号，也可能是
 * @param ctx
 * @param name
 * @return 返回 symbol 的虚拟地址
 */
uint64_t elf_get_sym_addr(elf_context_t *ctx, char *name, bool force) {
    // 部分平台需要 leading_underscore
    char buf[256];
    if (force) {
        buf[0] = '_';
        pstrcpy(buf + 1, sizeof(buf) - 1, name);
        name = buf;
    }

    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, name);
    if (sym_index == 0) {
        error_exit("[elf_get_sym_addr] undefined symbol %s", name);
    }

    Elf64_Sym *sym = &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
    if (sym->st_shndx == SHN_UNDEF) {
        error_exit("[elf_get_sym_addr] undefined symbol %s", name);
    }
    return sym->st_value;
}

/**
 * 符号表中 local 符号排在前面， global 符号排在后面
 * @param ctx
 * @param s
 */
void sort_symbols(elf_context_t *ctx, section_t *s) {
    uint64_t sym_count = s->data_count / sizeof(Elf64_Sym);
    Elf64_Sym *new_symtab = malloc(sizeof(Elf64_Sym) * sym_count);
    uint64_t *symtab_index_map = malloc(sizeof(uint64_t) * sym_count);

    Elf64_Sym *new_sym = new_symtab;

    // first pass - local symbols
    Elf64_Sym *sym = (Elf64_Sym *) s->data;
    for (int i = 0; i < sym_count; ++i) {
        if (ELF64_ST_BIND(sym->st_info) == STB_LOCAL) {
            // 检查是否为 . 开头的本地标签
            char *name = (char *) s->link->data + sym->st_name;
            if (name[0] != '.' || sym->st_info == ELF64_ST_INFO(STB_LOCAL, STT_FILE)) {
                symtab_index_map[i] = new_sym - new_symtab;
                *new_sym++ = *sym;
            } else {
                symtab_index_map[i] = 0; // 标记为已移除
            }
        }
        sym++;
    }

    if (s->sh_size) {
        s->sh_info = new_sym - new_symtab;
    }

    // second pass - global symbols
    sym = (Elf64_Sym *) s->data;
    for (int i = 0; i < sym_count; ++i) {
        if (ELF64_ST_BIND(sym->st_info) != STB_LOCAL) {
            symtab_index_map[i] = new_sym - new_symtab;
            *new_sym++ = *sym;
        }
        sym++;
    }

    // 更新符号表大小
    uint64_t new_sym_count = new_sym - new_symtab;
    s->data_count = new_sym_count * sizeof(Elf64_Sym);
    s->sh_size = s->data_count;

    // 将新符号复制到原符号表中
    memcpy(s->data, new_symtab, s->data_count);

    // 调整重定位表中的符号索引
    for (int sh_index = 1; sh_index < ctx->sections->count; ++sh_index) {
        section_t *rel_section = SEC_TACK(sh_index);
        if (rel_section->sh_type != SHT_RELA || rel_section->link != s) {
            continue;
        }
        Elf64_Rela *rel;
        for (rel = SEC_START(Elf64_Rela, rel_section); rel < SEC_END(Elf64_Rela, rel_section); rel++) {
            uint64_t sym_index = ELF64_R_SYM(rel->r_info);
            int type = ELF64_R_TYPE(rel->r_info);
            uint64_t new_sym_index = sym_index = symtab_index_map[sym_index];
            if (sym_index > 0) {
                assert(new_sym_index > 0);
            }

            rel->r_info = ELF64_R_INFO(new_sym_index, type);
        }
    }

    free(new_symtab);
    free(symtab_index_map);
}

static uint64_t get_be(const uint8_t *b, int n) {
    uint64_t ret = 0;
    while (n) ret = (ret << 8) | *b++, --n;
    return ret;
}

static ssize_t read_ar_header(int fd, uint64_t offset, archive_header_t *arhdr) {
    char *p, *e;
    lseek(fd, offset, SEEK_SET);
    ssize_t len = full_read(fd, arhdr, sizeof(archive_header_t));
    if (len != sizeof(archive_header_t)) return len ? -1 : 0;
    p = arhdr->ar_name;
    for (e = p + sizeof arhdr->ar_name; e > p && e[-1] == ' ';) --e;
    *e = '\0';
    arhdr->ar_size[sizeof arhdr->ar_size - 1] = 0;
    return len;
}

void load_archive(elf_context_t *ctx, int fd) {
    assertf(fd != -1, "invalid fd");

    archive_header_t arhdr;
    uint64_t file_offset = sizeof(ARMAG) - 1;
    ssize_t len = read_ar_header(fd, file_offset, &arhdr);
    if (len < 0) {
        assert(false && "invalid archive");
    }
    file_offset += len;
    int32_t size = strtol(arhdr.ar_size, NULL, 0);
    size = (size + 1) & ~1;
    int entrysize = 4;
    uint8_t *data = malloc(size);
    ssize_t read_size = full_read(fd, data, size);
    if (read_size != size) {
        assert(false && "invalid archive size");
    }

    uint64_t symbol_count = get_be(data, entrysize);
    uint8_t *ar_index = data + entrysize;
    char *ar_names = (char *) ar_index + symbol_count * entrysize;
    int bound;
    int i;
    uint64_t offset;
    do {
        bound = 0;
        char *sym_name = ar_names;
        int i = 0;
        while (i < symbol_count) {
            section_t *s = ctx->symtab_section;
            uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, sym_name);
            if (!sym_index) { // 如果内存符号表中不依赖该 sym, 则进行跳过 (i++, sym_name += strlen(sym_name) + 1)
                goto CONTINUE;
            }

            Elf64_Sym *sym = &((Elf64_Sym *) s->data)[sym_index];
            if (sym->st_shndx != SHN_UNDEF) { // 依赖当前符号，但是已经定义过了，则不重复加载
                goto CONTINUE;
            }

            // 加载符号在所在的 object header 偏移, 进行定向的 object 文件加载
            // get_be 从符号表中读取大端格式的整数，该整数表示 object 文件的 header 偏移
            offset = get_be(ar_index + (i * entrysize), entrysize);
            len = read_ar_header(fd, offset, &arhdr);
            int cmp = memcmp(arhdr.ar_fmag, ARFMAG, 2);
            if (len <= 0 || cmp != 0) {
                assertf(false, "invalid archive ar_fmag");
            }

            offset += len;
            // log_debug("   -> %s\n", arhdr.ar_name);
            load_object_file(ctx, fd, offset);
            ++bound;

        CONTINUE:
            i++;
            sym_name += strlen(sym_name) + 1;
        }
    } while (bound);
}

elf_context_t *elf_context_new(char *output, uint8_t type) {
    elf_context_t *ctx = NEW(elf_context_t);

    ctx->output = output;
    ctx->output_type = type;
    ctx->sections = slice_new();
    slice_push(ctx->sections, NULL);

    ctx->private_sections = slice_new();

    ctx->symtab_hash = table_new();
    /* create standard sections */
    ctx->text_section = elf_section_new(ctx, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    ctx->data_section = elf_section_new(ctx, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    // pe 需要特殊处理
    ctx->rodata_section = elf_section_new(ctx, ".data.ro", SHT_PROGBITS, ctx->shf_RELRO);

    // 添加 TLS 段
    if (type == OUTPUT_EXE) {
        ctx->tdata_section = elf_section_new(ctx, ".tdata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
        //        ctx->tbss_section = elf_section_new(ctx, ".tbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
    }

    /* create ro data section (make ro after relocation done with GNU_RELRO) */
    ctx->bss_section = elf_section_new(ctx, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);

    /* global_symbols are always generated for linking stage */
    ctx->symtab_section = elf_section_new(ctx, ".symtab", SHT_SYMTAB, 0);
    ctx->symtab_section->sh_entsize = sizeof(Elf64_Sym);
    section_t *strtab = elf_section_new(ctx, ".strtab", SHT_STRTAB, 0);
    elf_put_str(strtab, "");
    ctx->symtab_section->link = strtab;
    // 添加空符号
    Elf64_Sym empty_sym = {0};
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &empty_sym, NULL);
    // 添加文件符号
    Elf64_Sym file_sym = {
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_FILE),
            .st_other = 0,
            .st_shndx = SHN_ABS,
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &file_sym, output);

    elf_get_sym_attr(ctx, 0, 1);
    return ctx;
}

void *elf_file_load_data(int fd, uint64_t offset, uint64_t size) {
    void *data = malloc(size);
    lseek(fd, offset, SEEK_SET);
    full_read(fd, data, size);
    return data;
}

void elf_section_realloc(section_t *section, uint64_t new_size) {
    uint64_t size = section->data_capacity;
    if (size == 0) {
        size = 1;
    }
    while (size < new_size) {
        size = size * 2;
    }
    uint8_t *data = realloc(section->data, size);
    memset(data + section->data_capacity, 0, size - section->data_capacity);
    section->data = data;
    section->data_capacity = size;
}

Elf64_Sym *elf_find_sym(elf_context_t *ctx, char *name) {
    uint64_t sym_index = (uint64_t) table_get(ctx->symtab_hash, name);
    if (sym_index == 0) {
        return NULL;
    }
    return &((Elf64_Sym *) ctx->symtab_section->data)[sym_index];
}

/**
 * 返回了 data 存放数据的内存地址，可以基于该地址做数据覆盖
 * @param ctx
 * @param name
 * @param value
 * @param value_size
 * @return
 */
uint64_t elf_put_global_symbol(elf_context_t *ctx, char *name, void *value, uint8_t value_size) {
    // 值写入到数据段
    uint64_t offset = elf_put_data(ctx->data_section, value, value_size);

    // 写入符号表
    Elf64_Sym sym = {
            .st_size = value_size,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_other = 0,
            .st_shndx = ctx->data_section->sh_index, // 定义符号的段
            .st_value = offset, // 定义符号的位置，基于段的偏移
    };
    elf_put_sym(ctx->symtab_section, ctx->symtab_hash, &sym, name);
    return offset;
}

static inline uint32_t elf_ehdr_flags() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return 0;
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return 0;
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        // 设置为64位硬件双精度浮点ABI
        return 0x5; // rvc + double-float ABI
    }

    assert(false && "not support arch");
    return 0;
}

void elf_output(elf_context_t *ctx) {
    FILE *f;
    unlink(ctx->output);
    int fd = open(ctx->output, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0777);
    if (fd < 0 || (f = fdopen(fd, "wb")) == NULL) {
        assertf(false, "[elf_output] could not write '%s: %s'", ctx->output);
        return;
    }
    Elf64_Ehdr ehdr = {0};

    int shdr_count = ctx->sections->count;

    // 可重定位文件不包含程序头
    if (ctx->phdr_count > 0) {
        ehdr.e_phentsize = sizeof(Elf64_Phdr);
        ehdr.e_phnum = ctx->phdr_count;
        ehdr.e_phoff = sizeof(Elf64_Ehdr);
        shdr_count = tidy_section_headers(ctx);
    }

    ctx->file_offset = (ctx->file_offset + 3) & -4;

    // fill header
    ehdr.e_ident[0] = ELFMAG0;
    ehdr.e_ident[1] = ELFMAG1;
    ehdr.e_ident[2] = ELFMAG2;
    ehdr.e_ident[3] = ELFMAG3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;

    if (ctx->output_type == OUTPUT_OBJ) {
        ehdr.e_type = ET_REL;
    } else {
        // 仅可执行文件需要入口
        ehdr.e_type = ET_EXEC;
        // 这里是符号的虚拟地址(虚拟内存中的地址)
        ehdr.e_entry = elf_get_sym_addr(ctx, START_LABEL, 0); // 目前位于 crt1.o 中
    }

    ehdr.e_machine = elf_ehdr_machine();
    ehdr.e_version = EV_CURRENT;
    ehdr.e_flags = elf_ehdr_flags();
    ehdr.e_shoff = ctx->file_offset;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = shdr_count;
    ehdr.e_shstrndx = shdr_count - 1;
    fwrite(&ehdr, 1, sizeof(Elf64_Ehdr), f);
    if (ctx->phdr_list) {
        fwrite(ctx->phdr_list, 1, ctx->phdr_count * sizeof(Elf64_Phdr), f);
    }
    uint64_t offset = sizeof(Elf64_Ehdr) + ctx->phdr_count * sizeof(Elf64_Phdr);
    // 符号排序
    sort_symbols(ctx, ctx->symtab_section);

    // 将段数据写入文件
    for (int sh_index = 1; sh_index < shdr_count; ++sh_index) {
        int order_index = sh_index;
        if (ctx->output_type == OUTPUT_EXE) {
            order_index = SEC_TACK(sh_index)->actual_sh_index;
        }
        section_t *s = SEC_TACK(order_index);
        if (s->sh_type != SHT_NOBITS) {
            while (offset < s->sh_offset) {
                fputc(0, f);
                offset++;
            }
            if (s->sh_size) {
                fwrite(s->data, 1, s->sh_size, f);
            }
            offset += s->sh_size;
        }
    }

    // output section headers
    while (offset < ehdr.e_shoff) {
        fputc(0, f);
        offset++;
    }

    // 写入段表
    for (int sh_index = 0; sh_index < shdr_count; ++sh_index) {
        Elf64_Shdr shdr = {0};
        section_t *s = SEC_TACK(sh_index);
        if (s) {
            //            log_debug("[elf_output] shr: %s write file", s->name);
            shdr.sh_name = s->sh_name;
            shdr.sh_type = s->sh_type;
            shdr.sh_flags = s->sh_flags;
            shdr.sh_entsize = s->sh_entsize;
            shdr.sh_info = s->sh_info;
            if (s->link) {
                shdr.sh_link = s->link->sh_index;
            }
            shdr.sh_addralign = s->sh_addralign;
            shdr.sh_addr = s->sh_addr;
            shdr.sh_offset = s->sh_offset;
            shdr.sh_size = s->sh_size;
        }
        fwrite(&shdr, 1, sizeof(Elf64_Shdr), f);
    }

    fclose(f);
}
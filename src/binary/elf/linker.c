#include "linker.h"
#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "x86_64.h"
#include "arch.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>

static int sort_sections(linker_t *l) {
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        section_t *s = SECTION_TACK(sh_index);
        int base_weight;
        int sub_weight;
        {
            if (s->sh_flags & SHF_ALLOC) {
                base_weight = 0x100;
                if (s->sh_flags & SHF_WRITE) {
                    base_weight = 0x200;
                }
                if (s->sh_flags & SHF_TLS) {
                    base_weight += 0x200;
                }
            } else if (s->sh_name) {
                base_weight = 0x700;
            } else {
                base_weight = 0x900;
            }

            if (s->sh_type == SHT_SYMTAB || s->sh_type == SHT_DYNSYM) {
                sub_weight = 0x10;
            } else if (s->sh_type == SHT_STRTAB && !str_equal(s->name, ".stabstr")) {
                sub_weight = 0x11;
                if (sh_index == l->sections->count - 1) {
                    sub_weight = 0xff;
                }
            } else if (s->sh_type == SHT_HASH) {
                sub_weight = 0x12;
            } else if (s->sh_type == SHT_RELA) {
                sub_weight = 0x20;
                if (l->plt && s == l->plt->relocate) {
                    sub_weight = 0x21;
                }
            } else if (s == l->rodata_section || str_equal(s->name, ".data.rel.ro")) {
                sub_weight = 0x45;
            } else if (s->sh_type == SHT_DYNAMIC) {
                sub_weight = 0x46;
            } else if (s == l->got) {
                sub_weight = 0x47;
            } else {
                sub_weight = 0x50;
                if (s->sh_type == SHT_NOTE) {
                    sub_weight = 0x60;
                }
                if (s->sh_flags & SHF_EXECINSTR) {
                    sub_weight = 0x70;
                }
                if (s->sh_type == SHT_NOBITS) {
                    sub_weight = 0x80;
                }
            }
        }

        int weight = base_weight + sub_weight;
        int n = sh_index;
        int temp = SECTION_TACK(n - 1)->order_weight;
        // 倒序遍历(大-> 小) 选择一个刚好能卡住 k 的位置
        // 对于每一个比 k 大的值，都将其位置后移一位
        while (n > 1 && weight < temp) {
            SECTION_TACK(n)->order_weight = temp;  // 前移一位，留出空位，直到遇到一个 f 小于 k 的部分，就退出循环
            SECTION_TACK(n)->order_index = SECTION_TACK(n - 1)->order_index; // 位置后移一位， order_index 中存储了 sh_index
            --n;
        }

        // weight 为 sh_index 的 weight, 而不是 phdr_count 的 weight
        SECTION_TACK(sh_index)->order_weight = weight;
        SECTION_TACK(n)->order_index = sh_index;
    }
    SECTION_TACK(0)->order_index = 0;

    // 写入 order flags
    int phdr_count = 0;
    uint prev_flags = 0;
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        // 读取应该存在 sh_index 位置的 section
        int order_index = SECTION_TACK(sh_index)->order_index;
        section_t *s = SECTION_TACK(order_index);
        // 此处的 weight 存储的是排序后权重点的 weight?
        int weight = s->order_weight;
        uint flags = 0;
        if (weight < 0x700) {
            flags = s->sh_flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR | SHF_TLS);
            // 忽略掉前4位，只看后面几位，
            // 只要第 (sh_info link) 和 9 (Section is member of a group.) 位有值
            // 就表名该 section 为 RELRO sections
            // 0xfff0 = 1111 1111 1111 0000
            // 0x240 =  0000 0010 0100 0000
            if ((weight & 0xfff0) == 0x240) {
                flags |= 1 << 4; // 开启标志位 SHF_MERGE,Might be merged
            }/* RELRO (Relocation Read-Only) sections */

            // 添加标识位
            if (flags != prev_flags) {
                prev_flags = flags;
                ++phdr_count; // 开启一个新的段, phdr_count 表示程序表的段的数量
                //  把 00000001 -8-> 10000000  第 8 位是预留位，可以随便定义
                // 此处用来标志开启了新的 segment
                flags |= 1 << 8;
            } /* start new header when flags changed or relro */
        }
        s->order_flags = flags;
    }

    return phdr_count;
}

static void alloc_section_names(linker_t *l, bool is_obj) {
    section_t *shstr_section;
    shstr_section = elf_new_section(l, ".shstrtab", SHT_STRTAB, 0);
    elf_put_str(shstr_section, "");
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        section_t *s = l->sections->take[sh_index];
        if (is_obj) {
            s->sh_size = s->data_count;
        }
        if (s == shstr_section || s->sh_size || (s->sh_flags & SHF_ALLOC)) {
            s->sh_name = elf_put_str(shstr_section, s->name);
        }
    }

    shstr_section->sh_size = shstr_section->data_count;
}

static int set_section_sizes(linker_t *l) {
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        section_t *s = l->sections->take[sh_index];
        if (s->sh_flags & SHF_ALLOC) {
            s->sh_size = s->data_count;
        }
    }
    return 0;
}

static uint64_t build_got(linker_t *l) {
    l->got = elf_new_section(l, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    l->got->sh_entsize = 4;
    elf_section_data_add_ptr(l->got, 3 * PTR_SIZE);

    Elf64_Sym sym = {
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_other = 0,
            .st_shndx = l->got->sh_index,
    };
    return elf_set_sym(l, &sym, "_GLOBAL_OFFSET_TABLE_");
}

static int8_t is_code_relocate(uint relocate_type) {
    if (arch == ARCH_X86_64) {
        return x86_64_is_code_relocate(relocate_type);
    }
    return -1;
}


static int got_rel_type(bool is_code_rel) {
    if (arch == ARCH_X86_64) {
        if (is_code_rel) {
            return R_X86_64_JUMP_SLOT;
        } else {
            return R_X86_64_GLOB_DAT;
        }
    }
    return -1;
}

static int gotplt_entry_type(uint relocate_type) {
    if (arch == ARCH_X86_64) {
        return x86_64_gotplt_entry_type(relocate_type);
    }
    return -1;
}


static sym_attr_t *get_sym_attr(linker_t *l, uint sym_index, bool alloc) {
    if (sym_index >= l->sym_attrs_count) {
        if (!alloc) {
            return l->sym_attrs;
        }

        uint n = 1;
        while (sym_index >= n) {
            n *= 2;
        }
        sym_attr_t *ptr = realloc(l->sym_attrs, n * sizeof(*l->sym_attrs));
        l->sym_attrs = ptr;
        // 初始化刚刚申请的空间
        memset(l->sym_attrs + l->sym_attrs_count, 0, (n - l->sym_attrs_count) * sizeof(*l->sym_attrs));
        l->sym_attrs_count = n;
    }

    return &l->sym_attrs[sym_index]; // 符号始终在 l 中
}

/**
 * 符号在符号表未定义，或者重定位的类型为 PLT/GOT
 * @param l
 * @param relocate_type
 * @param sym_index
 * @return
 */
static sym_attr_t *put_got_entry(linker_t *l, int relocate_type, uint64_t sym_index) {
    char plt_name[200];
    uint need_plt_entry;
    need_plt_entry = relocate_type == R_X86_64_JUMP_SLOT;
    sym_attr_t *attr = get_sym_attr(l, sym_index, true);
    if (need_plt_entry ? attr->plt_offset : attr->got_offset) {
        return attr;
    }

    uint64_t got_offset = l->got->data_count;
    elf_section_data_add_ptr(l->got, PTR_SIZE);

    Elf64_Sym sym = ((Elf64_Sym *) l->symtab_section->data)[sym_index];
    char *sym_name = (char *) l->symtab_section->link->data + sym.st_name;
    elf_put_relocate(l, l->symtab_section, l->got,
                     got_offset, relocate_type, sym_index, 0);

    if (need_plt_entry) {
        // 基于 x86_64 的 plt entry
        if (arch == ARCH_X86_64) {
            attr->plt_offset = x86_64_create_plt_entry(l, got_offset, attr);
        }

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
        new_sym->st_shndx = l->plt->sh_index;
        attr->plt_sym = elf_put_sym(l, new_sym, plt_name);
    } else {
        attr->got_offset = got_offset;
    }

    return attr;
}

void elf_load_object_file(linker_t *l, int fd, uint64_t file_offset) {
    lseek(fd, file_offset, SEEK_SET);
    Elf64_Ehdr ehdr; // 清 0
    Elf64_Shdr shdr;
    ssize_t read_size = full_read(fd, &ehdr, sizeof(ehdr));
    if (read_size != sizeof(ehdr)) {
        error_exit("[elf_load_object_file] size: %d not equal sizeof ehdr", read_size);
    }
    if (ehdr.e_type != ET_REL) {
        error_exit("[elf_load_object_file] ehdr.e_type not rel file");
    }

    // read section header table
    Elf64_Shdr *shdr_list = elf_file_load_data(fd, file_offset + ehdr.e_shoff, sizeof(shdr) * ehdr.e_shnum);
    object_section_t *local_sections = malloc(sizeof(object_section_t) * ehdr.e_shnum);

    // 加载段表字符串表 e_shstrndx = Section header string table index
    shdr = shdr_list[ehdr.e_shstrndx];
    char *shstrtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);

    // 加载符号表,单个可中定位文件中只允许存在一个符号表
    Elf64_Sym *symtab = NULL;
    Elf64_Sym *sym;
    uint64_t sym_count = 0;
    char *strtab = NULL;
    for (int sh_index = 0; sh_index < ehdr.e_shnum; ++sh_index) {
        shdr = shdr_list[sh_index];
        if (shdr.sh_type != SHT_SYMTAB) {
            continue;
        }
        if (symtab) {
            error_exit("[elf_load_object_file] object must contain only on symtab");
        }

        sym_count = shdr.sh_size / sizeof(*sym);
        // 加载符号数据
        symtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);
        local_sections[sh_index].section = l->symtab_section; // 通过 index 进行全局关联

        // 加载符号字符串表
        shdr = shdr_list[shdr.sh_link];
        strtab = elf_file_load_data(fd, file_offset + shdr.sh_offset, shdr.sh_size);
    }

    // 段合并(按段名称合并，而不是段的类型, 其中不包含符号表，主要是对 data、text 段的合并)
    for (int sh_index; sh_index < ehdr.e_shnum; ++sh_index) {
        // 跳过段表字符串表
        if (sh_index == ehdr.e_shstrndx) {
            continue;
        }

        shdr = shdr_list[sh_index];
        if (shdr.sh_type == SHT_RELA) {
            shdr = shdr_list[shdr.sh_info];
        }

        if (shdr.sh_type != SHT_PROGBITS &&
            shdr.sh_type != SHT_NOTE &&
            shdr.sh_type != SHT_NOBITS &&
            shdr.sh_type != SHT_PREINIT_ARRAY &&
            shdr.sh_type != SHT_INIT_ARRAY &&
            shdr.sh_type != SHT_FINI_ARRAY &&
            !str_equal(shstrtab + shdr.sh_name, ".stabstr")) {
            continue;
        }

        shdr = shdr_list[sh_index];
        char *shdr_name = shstrtab + shdr.sh_name;
        section_t *section;
        // n * n 的遍历查找
        for (int i = 1; i < l->sections->count; ++i) {
            section = l->sections->take[i];
            if (str_equal(section->name, shdr_name)) {
                // 在全局 sections 中找到了同名 section
                goto FOUND;
            }
        }
        // not found
        section = elf_new_section(l, shdr_name, shdr.sh_type, shdr.sh_flags & ~SHF_GROUP);
        section->sh_addralign = shdr.sh_addralign;
        section->sh_entsize = shdr.sh_entsize;
        local_sections[sh_index].is_new = true;
        FOUND:
        // 同名但是类型不相同
        if (shdr.sh_type != section->sh_type) {
            error_exit("[elf_load_object_file] sh %s type invalid", shdr_name);
        }
        // align
        section->data_count += -section->data_count & (shdr.sh_addralign - 1);
        // local shdr > global section
        if (shdr.sh_addralign > section->sh_addralign) {
            section->sh_addralign = shdr.sh_addralign;
        }
        local_sections[sh_index].offset = section->data_count;
        local_sections[sh_index].section = section;

        // 将 local section 数据写入到全局 section 中
        uint64_t sh_size = shdr.sh_size;
        if (shdr.sh_type == SHT_NOBITS) { // 预留空间单没数据
            section->data_count += sh_size;
        } else {
            unsigned char *ptr;
            lseek(fd, file_offset + shdr.sh_offset, SEEK_SET); // 移动 fd 指向的文件的偏移点
            ptr = elf_section_data_add_ptr(section, sh_size);
            full_read(fd, ptr, sh_size);
        };
    }

    // 完善 section 中的关联关系，比如 section.link
    for (int sh_index; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *section = local_sections[sh_index].section;
        if (!section || !local_sections[sh_index].is_new) {
            continue;
        }

        shdr = shdr_list[sh_index];
        if (shdr.sh_link > 0) {
            section->link = local_sections[shdr.sh_link].section; // 这里的 section 为全局 section
        }

        // 重定位段表的 sh_link 保存对应的符号表
        // sh_info 保存重定位表的目标表，如果 .rel.text -> .text
        if (shdr.sh_type == SHT_RELA) {
            // 修正为 global section 中的目标
            section_t *target_section = local_sections[shdr.sh_info].section;
            section->sh_info = target_section->sh_index;
            // 添加反向 link, section 已经是经过合并操作的全局唯一 section 了
            target_section->relocate = section;
        }
    }

    // 符号重定位
    uint64_t *symtab_index_map = malloc(sizeof(uint64_t) * sym_count);
    // 遍历所有符号(符号表的第一个符号为 NULL)
    sym = symtab + 1;
    for (int i = 0; i < sym_count; ++i, sym++) {
        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            object_section_t local = local_sections[sym->st_shndx]; // st_shndx 定义符号的段
            if (!local.section) {
                continue;
            }

            // convert section index
            sym->st_shndx = local.section->sh_index;
            // sm->offset 是 section' data 合并到全局 section 时的起始地址，所以 st_value 自然要加上这个起始地址
            sym->st_value += local.offset;
        }
        // add symbol
        char *sym_name = strtab + sym->st_name;
        // 当前目标文件中定义的符号
        uint64_t sym_index = elf_set_sym(l, sym, sym_name);
        symtab_index_map[i] = sym_index;
    }

    // rela patch
    // 当然并不是所有的符号都已经被 patch 了，不过没关系，先指向 undef 就行，一旦符号被加载进来就会自动修复的，而不是重新建
    for (int sh_index = 0; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *s = local_sections[sh_index].section;
        if (!s) {
            continue;
        }
        if (s->sh_type != SHT_RELA) {
            continue;
        }

        shdr = shdr_list[sh_index];
        uint64_t offset = local_sections[sh_index].offset;
        uint64_t sh_size = shdr.sh_size;

        // 应用 rel 的 section
        uint64_t apply_offset = local_sections[shdr.sh_info].offset;
        Elf64_Rel *rel = (Elf64_Rel *) s->data + offset;
        Elf64_Rel *rel_end = (Elf64_Rel *) s->data + offset + sh_size;
        while (rel < rel_end) {
            uint64_t rel_type = ELF64_R_TYPE(rel->r_info); // 重定位类型
            uint64_t sym_index = ELF64_R_SYM(rel->r_info); // 重定位符号在符号表中的索引

            if (sym_index >= sym_count) {
                error_exit("[elf_load_object_file] rel sym index exception");
            }

            sym_index = symtab_index_map[sym_index];
            if (!sym_index) {
                error_exit("[elf_load_object_file] sym index not found");
            }

            rel->r_info = ELF64_R_INFO(sym_index, rel_type);
            rel->r_offset = apply_offset;
            rel++;
        }
    }
}

size_t elf_section_data_forward(section_t *section, uint64_t size, uint align) {
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

void *elf_section_data_add_ptr(section_t *section, uint64_t size) {
    size_t offset = elf_section_data_forward(section, size, 1);
    return section->data + offset;
}

uint64_t elf_set_sym(linker_t *l, Elf64_Sym *sym, char *name) {

    section_t *s = l->symtab_section;
    table *symbol_table = l->symbol_table;
    uint sym_bind = ELF64_ST_BIND(sym->st_info);
    uint sym_type = ELF64_ST_TYPE(sym->st_info);
    uint8_t sym_visible = ELF64_ST_VISIBILITY(sym->st_other);
    Elf64_Sym exist_sym;
    uint64_t sym_index;

    if (sym_bind != STB_LOCAL) {
        // 全局搜索符号查看是否已经被定义
        sym_index = (uint64_t) table_get(symbol_table, name);
        if (sym_index == 0) {
            goto DEF;
        }
        exist_sym = ((Elf64_Sym *) s->data)[sym_index];

        if (exist_sym.st_value == sym->st_value &&
            exist_sym.st_size == sym->st_size &&
            exist_sym.st_info == sym->st_info &&
            exist_sym.st_other == sym->st_other &&
            exist_sym.st_shndx == sym->st_shndx) {
            return sym_index;
        }

        if (exist_sym.st_shndx == SHN_UNDEF) {
            exist_sym.st_other = sym->st_other;
            goto PATCH;
        } else {
            // 当前段中的符号与全局符号同名

            // 计算可见性
            uint exist_sym_bind = ELF64_ST_BIND(exist_sym.st_info);
            uint8_t exist_sym_visible = ELF64_ST_VISIBILITY(exist_sym.st_other);
            /* propagate the most constraining visibility */ // 使用约束力最强的可见性等级,越往右约束性越强
            /* STV_DEFAULT(0)<STV_PROTECTED(3)<STV_HIDDEN(2)<STV_INTERNAL(1) */
            uint8_t new_visible;
            if (sym_visible == STV_DEFAULT) {
                new_visible = exist_sym_visible;
            } else {
                // 排除 default 的情况下，值越小约束性越强
                new_visible = (exist_sym_visible < sym_visible) ? exist_sym_visible : sym_visible;
            }
            exist_sym.st_other = (exist_sym.st_other & ~ELF64_ST_VISIBILITY(-1)) | new_visible;

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
            } else if ((exist_sym.st_shndx == SHN_COMMON || exist_sym.st_shndx == l->bss_section->sh_index)
                       && (sym->st_shndx < SHN_LORESERVE
                           && sym->st_shndx != l->bss_section->sh_index)) {
                goto PATCH;
            } else if (sym->st_shndx == SHN_COMMON || sym->st_shndx == l->bss_section->sh_index) {
                return sym_index;
            } else if (s->sh_flags & SHF_DYNSYM) {
                return sym_index;
            } else if (exist_sym.st_other & ST_ASM_SET) {
                goto PATCH;
            } else {
                error_exit("symbol %s repeat defined", name);
            }
        }
    }

    PATCH:
    exist_sym.st_info = ELF64_ST_INFO(sym_bind, sym_type);
    exist_sym.st_shndx = sym->st_shndx;
    exist_sym.st_value = sym->st_value;
    exist_sym.st_size = sym->st_size;
    return sym_index;

    DEF:
    sym_index = elf_put_sym(l, sym, name);
    return sym_index;
}

uint64_t elf_put_sym(linker_t *l, Elf64_Sym *sym, char *name) {
    section_t *s = l->symtab_section;
    Elf64_Sym *new_sym = elf_section_data_add_ptr(s, sizeof(Elf64_Sym));
    uint64_t name_offset = 0;
    if (name && name[0]) {
        name_offset = elf_put_str(s->link, name);
    }

    new_sym->st_name = name_offset;
    new_sym->st_value = sym->st_value;
    new_sym->st_size = sym->st_size;
    new_sym->st_info = sym->st_info;
    new_sym->st_other = sym->st_other;
    new_sym->st_shndx = sym->st_shndx;
    uint64_t sym_index = new_sym - (Elf64_Sym *) s->data; // 终点 - 起点 = 数量

    table_set(l->symbol_table, name, (void *) sym_index);

    return sym_index;
}

uint64_t elf_put_str(section_t *s, char *str) {
    size_t len = strlen(str) + 1; // 预留一个 \0
    uint64_t offset = s->data_count;

    char *ptr = elf_section_data_add_ptr(s, len);
    memmove(ptr, str, len);
    return offset;
}

/**
 * - resolve common sym
 * - build got entries
 * - recalculate segment size
 * - alloc section name
 * - layout sections
 * - relocate sym
 * - relocate sections
 * - fill got
 * - output execute
 */
void execute_file_format() {

}

void elf_resolve_common_symbols(linker_t *l) {
    Elf64_Sym *sym = ((Elf64_Sym *) l->symtab_section->data) + 1; // 跳过第一个符号
    Elf64_Sym *end = (Elf64_Sym *) l->symtab_section->data + l->symtab_section->data_count; // 指向结尾位置
    while (sym < end) {
        if (sym->st_shndx != SHN_COMMON) {
            continue;
        }
        // 数据段的符号值一般是 .data 段的起始位置 st_value 前进
        sym->st_value = elf_section_data_forward(l->bss_section, sym->st_size, sym->st_value);
        // 修正符号定义段为 bss
        sym->st_shndx = l->bss_section->sh_index;

        sym++;
    }
}

/* Returns an enumerator to describe whether and when the relocation needs a
   GOT and/or PLT entry to be created. See tcc.h for a description of the
   different values. */

/**
 * plt/got 是一种重定位的方式，所以这里都是围绕着重定位段进行的
 * 基于重定位的类型
 * @param l
 * @param got_sym_index
 */
void elf_build_got_entries(linker_t *l, uint got_sym_index) {
    Elf64_Rela *rel;

    // 一次遍历(基于 R_JMP_SLOT 构建 plt 段)
    int pass = 0;
    REDO:
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        section_t *s = l->sections->take[sh_index];
        // 仅需要符号表重定位表
        if (s->sh_type != SHT_RELA) {
            continue;
        }
        if (s->link != l->symtab_section) {
            continue;
        }

        for (rel = (Elf64_Rela *) s->data; rel < (Elf64_Rela *) (s->data + s->data_count); rel++) {
            uint type = ELF64_R_TYPE(rel->r_info); // 重定位类型
            uint64_t sym_index = ELF64_R_SYM(rel->r_info);
            Elf64_Sym sym = ((Elf64_Sym *) l->symtab_section->data)[sym_index];

            int gotplt_type = gotplt_entry_type(type);
            if (gotplt_type == -1) {
                error_exit("[elf_build_got_entries] unknown relocation type for got: %d", type);
            }

            if (gotplt_type == NO_GOTPLT_ENTRY) {
                continue;
            }

            if (gotplt_type == AUTO_GOTPLT_ENTRY) {
                if (sym.st_shndx == SHN_ABS && sym.st_value == 0) {
                    continue;
                }
                if (sym.st_shndx != SHN_UNDEF) {
                    continue;
                }
            }

            // 架构相关代码 if arch == x86_64
            if (arch == ARCH_X86_64) {
                // 重定位类型改写， 这两种的类型的重定位不参与 PLT 和 GOT 的构建
                if (type == R_X86_64_PLT32 || type == R_X86_64_PC32 &&
                                              sym.st_shndx != SHN_UNDEF) {
                    rel->r_info = ELF64_R_INFO(sym_index, R_X86_64_PC32);
                    continue;
                }
            }

            // 代码段 PLT 构建
            int8_t is_code_rel = is_code_relocate(type);
            if (is_code_rel == -1) {
                error_exit("[elf_build_got_entries] unknown relocation type for got: %d", type);
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

            if (is_code_rel && !l->plt) {
                // new plt
                l->plt = elf_new_section(l, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
                l->plt->sh_entsize = 4;
            }

            if (!l->got) {
                // build got
                got_sym_index = build_got(l);
            }

            int rel_type = got_rel_type((bool) is_code_rel);
            sym_attr_t *attr = put_got_entry(l, rel_type, sym_index);

            if (is_code_rel) {
                rel->r_info = ELF64_R_INFO(attr->plt_sym, type);
            }
        }
    }
    if (++pass < 2) {
        goto REDO;
    }

    if (l->plt && l->plt->relocate) {
        l->plt->relocate->sh_info = l->got->sh_index; // 重定位表的 sh_info 存储的应用重定位的段
    }
    if (got_sym_index) {
        ((Elf64_Sym *) l->symtab_section->data)[got_sym_index].st_size = l->got->data_count;
    }
}

void elf_put_relocate(linker_t *l, section_t *sym_section, section_t *apply_section, uint64_t offset, int type,
                      int sym_index, int64_t addend) {

    char buf[256];
    section_t *rel_section = apply_section->relocate;
    if (!rel_section) {
        snprintf(buf, sizeof(buf), REL_SECTION_FMT, apply_section->name);

        rel_section = elf_new_section(l, buf, SHT_RELA, sym_section->sh_index);
        rel_section->sh_entsize = sizeof(Elf64_Rela);
        rel_section->link = sym_section;
        rel_section->sh_info = apply_section->sh_index;
        apply_section->relocate = rel_section;
    }

    Elf64_Rela *rel = elf_section_data_add_ptr(rel_section, sizeof(Elf64_Rela));
    rel->r_offset = offset;
    rel->r_info = ELF64_R_INFO(sym_index, type);
    rel->r_addend = addend;
}

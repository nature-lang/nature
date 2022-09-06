#include "linker.h"
#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "x86_64.h"
#include "arch.h"
#include "output.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>

static uint64_t get_be(const uint8_t *b, int n) {
    uint64_t ret = 0;
    while (n)
        ret = (ret << 8) | *b++, --n;
    return ret;
}

static ssize_t read_ar_header(int fd, uint64_t offset, archive_header_t *arhdr) {
    char *p, *e;
    lseek(fd, offset, SEEK_SET);
    ssize_t len = full_read(fd, arhdr, sizeof(archive_header_t));
    if (len != sizeof(archive_header_t))
        return len ? -1 : 0;
    p = arhdr->ar_name;
    for (e = p + sizeof arhdr->ar_name; e > p && e[-1] == ' ';)
        --e;
    *e = '\0';
    arhdr->ar_size[sizeof arhdr->ar_size - 1] = 0;
    return len;
}

static int sort_sections(linker_t *l) {
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
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
            } else if (str_equal(s->name, ".data.rel.ro")) {
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
        int n;
        int temp;
        // 倒序遍历(大-> 小) 选择一个刚好能卡住 k 的位置
        // 对于每一个比 k 大的值，都将其位置后移一位
        for (n = sh_index; n > 1 && weight < (temp = SEC_TACK(n - 1)->actual_sh_weight); --n) {
            SEC_TACK(n)->actual_sh_weight = temp;  // 前移一位，留出空位，直到遇到一个 f 小于 k 的部分，就退出循环
            SEC_TACK(n)->actual_sh_index = SEC_TACK(n - 1)->actual_sh_index; // 位置后移一位， actual_sh_index 中存储了 sh_index
        }

        SEC_TACK(n)->actual_sh_weight = weight;
        SEC_TACK(n)->actual_sh_index = sh_index;
    }
//    SEC_TACK(0)->actual_sh_index = 0;

    // 写入 order flags
    int phdr_count = 0;
    uint prev_flags = 0;
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        // 读取应该存在 sh_index 位置的 section
        int weight = SEC_TACK(sh_index)->actual_sh_weight;
        int actual_sh_index = SEC_TACK(sh_index)->actual_sh_index;
        section_t *s = SEC_TACK(actual_sh_index);
//        printf("sh_index: %d, weight:%d, actual_sh_index: %d\n", sh_index, weight, actual_sh_index);
        uint flags = 0;
        if (weight >= 0x700) {
            s->phdr_flags = flags;
            continue;
        }
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
        s->phdr_flags = flags;
    }

    return phdr_count;
}

static void layout_sections(linker_t *l) {
    int phdr_count = sort_sections(l);
    l->phdr_count = phdr_count;
    l->phdr_list = mallocz(phdr_count * sizeof(Elf64_Phdr));

    uint64_t file_offset = sizeof(Elf64_Ehdr) + phdr_count * sizeof(Elf64_Phdr);
    addr_t s_align = elf_page_size();
    addr_t addr = elf_start_addr();
    addr_t base = addr;
    addr = addr + (file_offset & (s_align - 1));
    int n = 0;
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        int order_index = SEC_TACK(sh_index)->actual_sh_index;
        section_t *s = SEC_TACK(order_index);
        Elf64_Phdr *phdr = &l->phdr_list[n]; // ph fill 看起来像是预留的段空间
        uint flags = s->phdr_flags;
        uint align = s->sh_addralign - 1;
        if (flags == 0) { // no alloc in memory
            file_offset = (file_offset + align) & ~align; // ~align 按位取反
            s->sh_offset = file_offset;
            if (s->sh_type != SHT_NOBITS) {
                file_offset += s->sh_size;
            }
            continue;
        }

        // 右数第 8 位如果为 1，标识可装载(can alloc)
        if (flags & 1 << 8 && n) {
            if ((addr & (s_align - 1)) != 0) {
                addr += s_align;
            }
        }

        uint64_t tmp = addr;
        addr = (addr + align) & ~align;
        file_offset += (int) (addr - tmp); // 对齐差值，多个 section 共用一个 page 从而可以节省内存
        s->sh_offset = file_offset;
        s->sh_addr = addr;
        // 位移优先级高于按位与
        // x = 1 << 8 =  00000000 00000000 00000000 100000000
        // f &  x  = 最终判断第 8 位的值是否为 1, 为 1 表示需要创建一个新的 program header
        // pt load 段写入(可装载段)
        if (flags & 1 << 8) {
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
            if (flags & SHF_TLS) {
                phdr->p_type = PT_TLS;
                phdr->p_align = 4;
            }
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
        if (s->sh_type != SHT_NOBITS) {
            file_offset += s->sh_size;
        }

        phdr->p_filesz = file_offset - phdr->p_offset;
        phdr->p_memsz = addr - phdr->p_vaddr;
    }

    l->file_offset = file_offset;
}

static void alloc_section_names(linker_t *l, bool is_obj) {
    section_t *shstr_section;
    shstr_section = elf_new_section(l, ".shstrtab", SHT_STRTAB, 0);
    elf_put_str(shstr_section, "");
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
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
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
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
    elf_section_data_add_ptr(l->got, 3 * ptr_size());

    Elf64_Sym sym = {
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT),
            .st_other = 0,
            .st_shndx = l->got->sh_index,
    };
    return elf_set_sym(l, &sym, "_GLOBAL_OFFSET_TABLE_");
}

sym_attr_t *elf_get_sym_attr(linker_t *l, uint sym_index, bool alloc) {
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
    bool need_plt_entry = relocate_type == R_X86_64_JUMP_SLOT;
    sym_attr_t *attr = elf_get_sym_attr(l, sym_index, true);
    uint offset = need_plt_entry ? attr->plt_offset : attr->got_offset;
    if (offset) {
        return attr;
    }

    uint64_t got_offset = l->got->data_count;
    elf_section_data_add_ptr(l->got, ptr_size());

    Elf64_Sym *sym = &((Elf64_Sym *) l->symtab_section->data)[sym_index];
    char *sym_name = (char *) l->symtab_section->link->data + sym->st_name;
    elf_put_relocate(l, l->symtab_section, l->got,
                     got_offset, relocate_type, (int) sym_index, 0);

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
    Elf64_Shdr *shdr;
    ssize_t read_size = full_read(fd, &ehdr, sizeof(ehdr));
    if (read_size != sizeof(ehdr)) {
        error_exit("[elf_load_object_file] size: %d not equal sizeof ehdr", read_size);
    }
    if (ehdr.e_type != ET_REL) {
        error_exit("[elf_load_object_file] ehdr.e_type not rel file");
    }

    // read section header table
    Elf64_Shdr *shdr_list = elf_file_load_data(fd, file_offset + ehdr.e_shoff, sizeof(*shdr) * ehdr.e_shnum);
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
            error_exit("[elf_load_object_file] object must contain only on symtab");
        }

        sym_count = shdr->sh_size / sizeof(*sym);
        // 加载符号数据
        symtab = elf_file_load_data(fd, file_offset + shdr->sh_offset, shdr->sh_size);
        local_sections[sh_index].section = l->symtab_section; // 通过 index 进行全局关联

        // 加载符号字符串表
        shdr = &shdr_list[shdr->sh_link];
        strtab = elf_file_load_data(fd, file_offset + shdr->sh_offset, shdr->sh_size);
    }

    // 段合并(按段名称合并，而不是段的类型, 其中不包含符号表，主要是对 data、text 段的合并)
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        // 跳过段表字符串表
        if (sh_index == ehdr.e_shstrndx) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        if (shdr->sh_type == SHT_RELA) {
            shdr = &shdr_list[shdr->sh_info];
        }

        if (shdr->sh_type != SHT_PROGBITS &&
            shdr->sh_type != SHT_NOTE &&
            shdr->sh_type != SHT_NOBITS &&
            shdr->sh_type != SHT_PREINIT_ARRAY &&
            shdr->sh_type != SHT_INIT_ARRAY &&
            shdr->sh_type != SHT_FINI_ARRAY &&
            !str_equal(shstrtab + shdr->sh_name, ".stabstr")) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        char *shdr_name = shstrtab + shdr->sh_name;
        section_t *section;
        // n * n 的遍历查找
        for (int i = 1; i < l->sections->count; ++i) {
            section = SEC_TACK(i);
            if (str_equal(section->name, shdr_name)) {
                // 在全局 sections 中找到了同名 section
                goto FOUND;
            }
        }
        // not found
        section = elf_new_section(l, shdr_name, shdr->sh_type, shdr->sh_flags & ~SHF_GROUP);
        section->sh_addralign = shdr->sh_addralign;
        section->sh_entsize = shdr->sh_entsize;
        local_sections[sh_index].is_new = true;
        FOUND:
        // 同名但是类型不相同
        if (shdr->sh_type != section->sh_type) {
            error_exit("[elf_load_object_file] sh %s type invalid", shdr_name);
        }
        // align
        section->data_count += -section->data_count & (shdr->sh_addralign - 1);
        // local shdr > global section
        if (shdr->sh_addralign > section->sh_addralign) {
            section->sh_addralign = shdr->sh_addralign;
        }
        local_sections[sh_index].offset = section->data_count;
        local_sections[sh_index].section = section;

        // 将 local section 数据写入到全局 section 中
        uint64_t sh_size = shdr->sh_size;
        if (shdr->sh_type == SHT_NOBITS) { // 预留空间单没数据
            section->data_count += sh_size;
        } else {
            unsigned char *ptr;
            lseek(fd, file_offset + shdr->sh_offset, SEEK_SET); // 移动 fd 指向的文件的偏移点
            ptr = elf_section_data_add_ptr(section, sh_size);
            full_read(fd, ptr, sh_size);
        };
    }

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
//        printf("sym_name: %s\n", sym_name);
        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            local_section_t *local = &local_sections[sym->st_shndx]; // st_shndx 定义符号的段
            if (!local->section) {
                continue;
            }

            // convert section index
            sym->st_shndx = local->section->sh_index;
            // sm->offset 是 section' data 合并到全局 section 时的起始地址，所以 st_value 自然要加上这个起始地址
            sym->st_value += local->offset;
        }

        // 当前目标文件中定义的符号
        uint64_t sym_index = elf_set_sym(l, sym, sym_name);
        symtab_index_map[i] = sym_index;
    }

    // rela patch
    // 当然并不是所有的符号都已经被 patch 了，不过没关系，先指向 undef 就行，一旦符号被加载进来就会自动修复的，而不是重新建
    for (int sh_index = 1; sh_index < ehdr.e_shnum; ++sh_index) {
        section_t *s = local_sections[sh_index].section;
        if (!s) {
            continue;
        }
        if (s->sh_type != SHT_RELA) {
            continue;
        }

        shdr = &shdr_list[sh_index];
        uint64_t offset = local_sections[sh_index].offset;
        uint64_t sh_size = shdr->sh_size;

        // 应用 rel 的 section
        uint64_t apply_offset = local_sections[shdr->sh_info].offset;
        Elf64_Rela *rel = (Elf64_Rela *) s->data + (offset / sizeof(Elf64_Rela));
        Elf64_Rela *rel_end = (Elf64_Rela *) s->data + ((offset + sh_size) / sizeof(Elf64_Rela)); // 指针移动操作
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
    Elf64_Sym *exist_sym = NULL;
    uint64_t sym_index = 0;

    if (sym_bind != STB_LOCAL) {
        // 全局搜索符号查看是否已经被定义
        sym_index = (uint64_t) table_get(symbol_table, name);
        if (sym_index == 0) {
            goto DEF;
        }
        exist_sym = &((Elf64_Sym *) s->data)[sym_index];

        if (exist_sym->st_value == sym->st_value &&
            exist_sym->st_size == sym->st_size &&
            exist_sym->st_info == sym->st_info &&
            exist_sym->st_other == sym->st_other &&
            exist_sym->st_shndx == sym->st_shndx) {
            return sym_index;
        }

        if (exist_sym->st_shndx == SHN_UNDEF) {
            exist_sym->st_other = sym->st_other;
            goto PATCH;
        } else {
            // 当前段中的符号与全局符号同名

            // 计算可见性
            uint exist_sym_bind = ELF64_ST_BIND(exist_sym->st_info);
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
            } else if ((exist_sym->st_shndx == SHN_COMMON || exist_sym->st_shndx == l->bss_section->sh_index)
                       && (sym->st_shndx < SHN_LORESERVE
                           && sym->st_shndx != l->bss_section->sh_index)) {
                goto PATCH;
            } else if (sym->st_shndx == SHN_COMMON || sym->st_shndx == l->bss_section->sh_index) {
                return sym_index;
            } else if (s->sh_flags & SHF_DYNSYM) {
                return sym_index;
            } else if (exist_sym->st_other & ST_ASM_SET) {
                goto PATCH;
            } else {
                error_exit("symbol %s repeat defined", name);
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
 * - relocate symbols
 * - relocate sections
 * - fill got
 * - output execute
 */
void executable_file_format(linker_t *l) {
    elf_resolve_common_symbols(l);

    elf_build_got_entries(l, 0);

    set_section_sizes(l);

    alloc_section_names(l, 0);

    layout_sections(l);

    elf_relocate_symbols(l, l->symtab_section);

    elf_relocate_sections(l);

    elf_fill_got(l);
}

void elf_resolve_common_symbols(linker_t *l) {
    Elf64_Sym *sym;
    for (sym = SEC_START(Elf64_Sym, l->symtab_section) + 1; sym < SEC_END(Elf64_Sym, l->symtab_section); sym++) {
        if (sym->st_shndx != SHN_COMMON) {
            continue;
        }
        // 数据段的符号值一般是 .data 段的起始位置 st_value 前进
        sym->st_value = elf_section_data_forward(l->bss_section, sym->st_size, sym->st_value);
        // 修正符号定义段为 bss
        sym->st_shndx = l->bss_section->sh_index;
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
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
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
            Elf64_Sym *sym = &((Elf64_Sym *) l->symtab_section->data)[sym_index];

            int gotplt_type = gotplt_entry_type(type);
            if (gotplt_type == -1) {
                error_exit("[elf_build_got_entries] unknown relocation type for got: %d", type);
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

            // 架构相关代码 if arch == x86_64
            if (arch == ARCH_X86_64) {
                // 重定位类型改写， 这两种的类型的重定位不参与 PLT 和 GOT 的构建
                if (type == R_X86_64_PLT32 || type == R_X86_64_PC32 &&
                                              sym->st_shndx != SHN_UNDEF) {
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

void elf_relocate_symbols(linker_t *l, section_t *sym_section) {
    Elf64_Sym *sym;
    for (sym = SEC_START(Elf64_Sym, sym_section) + 1; sym < SEC_END(Elf64_Sym, sym_section); sym++) {
        int sh_index = sym->st_shndx;
        if (sh_index == SHN_UNDEF) {
            // libc 中 "_DYNAMIC" 唯一未定义的符号就是这个，还是个 STB_WEAK 类型的符号
            char *name = (char *) l->symtab_section->link->data + sym->st_name;
            /* XXX: _fp_hw seems to be part of the ABI, so we ignore
               it */
            if (!strcmp(name, "_fp_hw"))
                goto FOUND;
            /* only weak symbols are accepted to be undefined. Their
               value is zero */
            // 静态链接中只有弱符号允许未定义，他们的值固定位 0
            uint sym_bind = ELF64_ST_BIND(sym->st_info);
            if (sym_bind == STB_WEAK) {
                sym->st_value = 0;
            } else {
                error_exit("[elf_relocate_symbols] undefined symbol '%s'", name);
            }
        } else if (sh_index < SHN_LORESERVE) {
            // 对于可重定位文件， st_value 保存了基于 section 的偏移
            // 对于可执行文件， st_value 则需要保存实际的线性地址了，也就是 section 的绝对地址 + st_value
            /* add section base */
            sym->st_value += SEC_TACK(sh_index)->sh_addr;
        }
        FOUND:;
    }

}

void elf_relocate_sections(linker_t *l) {
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        section_t *rel_section = SEC_TACK(sh_index);
        if (rel_section->sh_type != SHT_RELA) {
            continue;
        }
        section_t *apply_section = SEC_TACK(rel_section->sh_info);
        elf_relocate_section(l, apply_section, rel_section);
    }
}

/**
 * 根据 rel type 进行重定位,和 cpu 相关
 * @param l
 * @param apply_section
 * @param rel_section
 */
void elf_relocate_section(linker_t *l, section_t *apply_section, section_t *rel_section) {
    Elf64_Sym *sym = NULL;
    Elf64_Rela *rel = NULL;
    uint8_t *ptr = NULL;
    for (rel = SEC_START(Elf64_Rela, rel_section); rel < SEC_END(Elf64_Rela, rel_section); rel++) {
        // s 为目标端, s->data 为段起始指针，其实就是 0, rel->r_offset 是基于目标段的偏移
        // 但是总的来说 ptr 都是待修正的符号的位置
        ptr = apply_section->data + rel->r_offset;

        // rel 引用的符号在符号表的索引，在没有修正之前就是 UNDEF, 不过 load elf 的时候已经进行了不停的修正，所以这里已经不会是 undef 了
        int sym_index = ELF64_R_SYM(rel->r_info);
        sym = &((Elf64_Sym *) l->symtab_section->data)[sym_index]; // 符号
        int type = ELF64_R_TYPE(rel->r_info); // type 就是重定位的类型
        addr_t target = sym->st_value; // 符号定义的位置
        target += rel->r_addend; // 为啥定义符号的位置要加上 rel->addend? 定位到结束位置？

        // s->sh_addr 应该就是目标段的地址，加上 r_offset 就是绝对的地址修正了？
        // 没看出来 ptr 和 adr 的区别
        uint64_t addr = apply_section->sh_addr + rel->r_offset;
        relocate(l, rel, type, ptr, addr, target);
    }
}

void elf_fill_got(linker_t *l) {
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        if (s->sh_type != SHT_RELA) {
            continue;
        }

        if (s->link != l->symtab_section) {
            continue;
        }

        Elf64_Rela *rel;
        for (rel = SEC_START(Elf64_Rela, s); rel < SEC_END(Elf64_Rela, s); rel++) {
            switch (ELF64_R_TYPE(rel->r_info)) {
                case R_X86_64_GOT32:
                case R_X86_64_GOTPCREL:
                case R_X86_64_GOTPCRELX:
                case R_X86_64_REX_GOTPCRELX:
                case R_X86_64_PLT32:
                    elf_fill_got_entry(l, rel);
                    break;
            }
        }
    }
}

void elf_fill_got_entry(linker_t *l, Elf64_Rela *rel) {
    int sym_index = ELF64_R_SYM(rel->r_info);
    Elf64_Sym *sym = &((Elf64_Sym *) l->symtab_section->data)[sym_index];
    sym_attr_t *attr = elf_get_sym_attr(l, sym_index, 0);
    unsigned offset = attr->got_offset;

    if (offset == 0) {
        return;
    }
    if (offset + ptr_size() > l->got->data_capacity) {
        elf_section_realloc(l->got, offset + ptr_size());
    }
    if (offset + ptr_size() > l->got->data_count) {
        l->got->data_count = offset + ptr_size();
    }

    if (ptr_size() == 8) {
        write64le(l->got->data + offset, sym->st_value);
    } else {
        write32le(l->got->data + offset, sym->st_value);
    };
}

int tidy_section_headers(linker_t *l) {
    int *back_map = malloc(l->sections->count * sizeof(int));
    section_t **new_section_tack = malloc(sizeof(void *) * l->sections->capacity);
    int new_section_count = 0;
    int tail = l->sections->count;
    for (int sh_index = 0; sh_index < l->sections->count; ++sh_index) {
        // order index 表示应该在当前位置的 section
        int order_index = 0;
        section_t *s = NULL;
        if (sh_index > 0) {
            order_index = SEC_TACK(sh_index)->actual_sh_index;
            s = SEC_TACK(order_index);
        }
        if (sh_index == 0 || s->sh_name) {
            back_map[order_index] = new_section_count; // count 就是 new sh_index
            new_section_tack[new_section_count++] = s;
        } else {
            back_map[order_index] = 0; //
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
    section_t *sym_section = l->symtab_section;
    for (sym = SEC_START(Elf64_Sym, sym_section) + 1; sym < SEC_END(Elf64_Sym, sym_section); sym++) {
        if (sym->st_shndx != SHN_UNDEF && sym->st_shndx < SHN_LORESERVE) {
            sym->st_shndx = back_map[sym->st_shndx];
        }
    }
    l->sections->take = (void **) new_section_tack;

    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        section_t *s = SEC_TACK(sh_index);
        s->actual_sh_index = sh_index;
    }

    return new_section_count;
}

section_t *elf_new_section(linker_t *l, char *name, uint sh_type, uint sh_flags) {
    section_t *s = mallocz(sizeof(section_t));
    strcpy(s->name, name);
    s->sh_type = sh_type;
    s->sh_flags = sh_flags;

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
            s->sh_addralign = ptr_size();
            break;
        case SHT_STRTAB:
            s->sh_addralign = 1;
            break;
        default:
            s->sh_addralign = ptr_size(); /* gcc/pcc default alignment */
            break;
    }
    if (sh_flags & SHF_PRIVATE) {
        slice_push(l->private_sections, s);
    } else {
        s->sh_index = l->sections->count;
        slice_push(l->sections, s);
    }

    return s;
}

addr_t elf_get_sym_addr(linker_t *l, char *name) {
    uint64_t sym_index = (uint64_t) table_get(l->symbol_table, name);
    if (sym_index == 0) {
        error_exit("[elf_get_sym_addr] undefined symbol %s", name);
    }
    Elf64_Sym *sym = &((Elf64_Sym *) l->symtab_section->data)[sym_index];
    if (sym->st_shndx == SHN_UNDEF) {
        error_exit("[elf_get_sym_addr] undefined symbol %s", name);
    }
    return sym->st_value;
}

void sort_symbols(linker_t *l, section_t *s) {
    int sym_count = s->data_count / sizeof(Elf64_Sym);
    Elf64_Sym *new_symtab = malloc(sizeof(Elf64_Sym) * sym_count);
    uint64_t *symtab_index_map = malloc(sizeof(uint64_t) * sym_count);

    Elf64_Sym *new_sym = new_symtab;

    // first pass
    Elf64_Sym *sym = (Elf64_Sym *) s->data;
    for (int i = 0; i < sym_count; ++i) {
        if (ELF64_ST_BIND(sym->st_info) == STB_LOCAL) {
            // nwe_symtab 相当于起始点, 第一次就是索引为 0
            symtab_index_map[i] = new_sym - new_symtab;
            *new_sym++ = *sym;
        }
        sym++;
    }

    if (s->sh_size) {
        s->sh_info = new_sym - new_symtab;
    }

    // second pass
    sym = (Elf64_Sym *) s->data;
    for (int i = 0; i < sym_count; ++i) {
        if (ELF64_ST_BIND(sym->st_info) != STB_LOCAL) {
            // nwe_symtab 相当于起始点, 第一次就是索引为 0
            symtab_index_map[i] = new_sym - new_symtab;
            *new_sym++ = *sym;
        }
        sym++;
    }

    // 将新符号复制到就符号中
    memcpy(s->data, new_symtab, sym_count * sizeof(Elf64_Sym));

    // 调整符号索引
    for (int sh_index = 1; sh_index < l->sections->count; ++sh_index) {
        section_t *rel_section = SEC_TACK(sh_index);
        if (rel_section->sh_type != SHT_RELA || rel_section->link != s) {
            continue;
        }
        Elf64_Rela *rel;
        for (rel = SEC_START(Elf64_Rela, rel_section); rel < SEC_END(Elf64_Rela, rel_section); rel++) {
            uint64_t sym_index = ELF64_R_SYM(rel->r_info);
            int type = ELF64_R_TYPE(rel->r_info);
            sym_index = symtab_index_map[sym_index];
            rel->r_info = ELF64_R_INFO(sym_index, type);
        }
    }
}

void elf_load_archive(linker_t *l, int fd) {
    archive_header_t arhdr;
    uint64_t file_offset = sizeof(ARMAG) - 1;
    ssize_t len = read_ar_header(fd, file_offset, &arhdr);
    if (len < 0) {
        error_exit("invalid archive");
    }
    file_offset += len;
    int32_t size = strtol(arhdr.ar_size, NULL, 0);
    size = (size + 1) & ~1;
    int entrysize = 4;
    uint8_t *data = malloc(size);
    ssize_t read_size = full_read(fd, data, size);
    if (read_size != size) {
        error_exit("invalid archive size");
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
            section_t *s = l->symtab_section;
            uint64_t sym_index = (uint64_t) table_get(l->symbol_table, sym_name);
            if (!sym_index) {
                goto CONTINUE;
            }
            Elf64_Sym *sym = &((Elf64_Sym *) s->data)[sym_index];
            if (sym->st_shndx != SHN_UNDEF) {
                goto CONTINUE;
            }

            // 已经加载到内存中的符号存在对该符号的依赖
            offset = get_be(ar_index + i * entrysize, entrysize);
            len = read_ar_header(fd, offset, &arhdr);
            int cmp = memcmp(arhdr.ar_fmag, ARFMAG, 2);
            if (len <= 0 || cmp != 0) {
                error_exit("invalid archive ar_fmag");
            }

            offset += len;
            printf("   -> %s\n", arhdr.ar_name);
            elf_load_object_file(l, fd, offset);
            ++bound;

            CONTINUE:
            i++;
            sym_name += strlen(sym_name) + 1;
        }
    } while (bound);
}

linker_t *linker_new(char *output) {
    linker_t *l = mallocz(sizeof(linker_t));


    l->output = output;
    l->sections = slice_new();
    slice_push(l->sections, NULL);

    l->symbol_table = table_new();
    /* create standard sections */
    l->text_section = elf_new_section(l, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    l->data_section = elf_new_section(l, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    /* create ro data section (make ro after relocation done with GNU_RELRO) */
    l->bss_section = elf_new_section(l, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);

    /* symbols are always generated for linking stage */
    l->symtab_section = elf_new_section(l, ".symtab", SHT_SYMTAB, 0);
    l->symtab_section->sh_entsize = sizeof(Elf64_Sym);
    section_t *strtab = elf_new_section(l, ".strtab", SHT_STRTAB, 0);
    elf_put_str(strtab, "");
    l->symtab_section->link = strtab;
    Elf64_Sym empty_sym = {0};
    elf_put_sym(l, &empty_sym, NULL);

    elf_get_sym_attr(l, 0, 1);
    return l;
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
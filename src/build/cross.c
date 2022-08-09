#include "cross.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "src/assembler/linux_elf/elf.h"
#include "src/assembler/linux_elf/amd64_inst.h"
#include "src/lower/amd64/amd64.h"

char *os = "linux";  // 默认编译系统-影响 elf 文件构建
char *arch = "amd64";

/**
 * @param m
 * @return  m->asm_insts
 */
list *cross_lower(module_t *m) {
    m->asm_insts = list_new();
    for (int i = 0; i < m->compiler_closures->count; ++i) {
        closure *c = m->compiler_closures->take[i];
        if (str_equal(arch, "amd64")) {
            list_append(m->asm_insts, amd64_lower_closure(c));
        } else {
            goto ERROR;
        };
    }

    return m->asm_insts;
    ERROR:
    error_exit("[cross_assembler] unsupported OS/ARCH pair %s/%s", os, arch);
}

void cross_assembler(module_t *m) {
    if (str_equal(os, "linux")) {
        list *text_inst_list;
        if (str_equal(arch, "amd64")) {
            text_inst_list = linux_elf_amd64_insts_build(m->asm_insts);
        } else {
            goto ERROR;
        }

        char *_filename = str_connect(m->module_unique_name, ".n");
        linux_elf_t elf = linux_elf_init(_filename, m->var_decl_list, text_inst_list);
        m->elf_binary = linux_elf_encoding(elf, &m->elf_count);
        m->linker_file_name = str_connect(filename, ".o");
        str_replace(m->linker_file_name, '/', '.');
    }

    ERROR:
    error_exit("[cross_assembler] unsupported OS/ARCH pair %s/%s", os, arch);
}

/**
 * modules list
 * @param list
 */
void cross_linker(slice_t *list) {
}

// 默认 cpu 架构，影响 lower 和 elf


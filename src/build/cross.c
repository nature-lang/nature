#include "cross.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/linux_elf/elf.h"
#include "src/assembler/linux_elf/amd64_inst.h"
#include "src/lower/amd64/amd64.h"
#include "src/lower/lower.h"
#include "config.h"

/**
 * @param m
 * @return  m->asm_insts
 */
void cross_lower(module_t *m) {
    lower_var_decl_list = list_new();
    m->asm_insts = list_new();

    // pre
    if (str_equal(BUILD_ARCH, "x86_64")) {
        amd64_register_init();
        amd64_opcode_init();
    } else {
        goto ERROR;
    };

    // lower
    for (int i = 0; i < m->compiler_closures->count; ++i) {
        closure *c = m->compiler_closures->take[i];
        if (str_equal(BUILD_ARCH, "x86_64")) {
            list_append(m->asm_insts, amd64_lower_closure(c));
        } else {
            goto ERROR;
        }
    }

    // post
    if (str_equal(BUILD_ARCH, "x86_64")) {
        list_append(m->var_decl_list, lower_var_decl_list);
    } else {
        goto ERROR;
    }

    return;
    ERROR:
    error_exit("[cross_lower] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);

}

/**
 * 汇编器目前只支持 linux elf x86_64
 * @param m
 */
void cross_assembler(module_t *m) {
    if (str_equal(BUILD_OS, "linux")) {
        // 符号表初始化
        char *_filename = str_connect(m->module_unique_name, ".n");
        linux_elf_init(_filename, m->var_decl_list);

        if (str_equal(BUILD_ARCH, "x86_64")) {
            // 依赖 elf_init 中的符号表，重定位表
            linux_elf_text_inst_list = linux_elf_amd64_insts_build(m->asm_insts);
        } else {
            goto ERROR;
        }

        // 创建 elf 实例
        linux_elf_t elf = linux_elf_new();
        m->elf_binary = linux_elf_encoding(elf, &m->elf_count);
        m->linker_file_name = str_connect(filename, ".o");
        str_replace(m->linker_file_name, '/', '.');
    } else {
        goto ERROR;
    }


    return;
    ERROR:
    error_exit("[cross_assembler] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
}

/**
 * base on ${NATURE_ROOT}/lib/${BUILD_OS}_${BUILD_ARCH} + file
 * @return
 */
static char *lib_file_path(char *file) {
    char *_os_arch = str_connect(BUILD_OS, "_");
    _os_arch = str_connect(_os_arch, BUILD_ARCH);

    char *dir = file_join(NATURE_ROOT, "lib");
    dir = file_join(dir, _os_arch);
    return file_join(dir, file);
}

/**
 * modules module_list
 * @param module_list
 */
void cross_linker(slice_t *module_list) {
    char *tmp_dir = cross_tmp_dir();
    if (tmp_dir == NULL) {
        error_exit("[cross_linker] create tmp dir failed");
    }

    slice_t *ld_args = slice_new();
    for (int i = 0; i < module_list->count; ++i) {
        module_t *m = module_list->take[i];

        // 写入到 tmp 目录
        char *file = file_join(tmp_dir, m->linker_file_name);
        linux_elf_to_file(m->elf_binary, m->elf_count, file);

        // 工作目录再 tmp_dir 中,所以之类使用相对路径即可
        slice_push(ld_args, m->linker_file_name);
    }

    slice_push(ld_args, "-Bstatic"); // 静态链接
    slice_push(ld_args, "-nostdinc"); // 忽略标准库头文件
    slice_push(ld_args, "-nostdlib"); // 忽略标准库
    slice_push(ld_args, lib_file_path(LIB_START_FILE));
    slice_push(ld_args, lib_file_path(LIB_RUNTIME_FILE));
    slice_push(ld_args, lib_file_path(LIB_C_FILE));

    slice_push(ld_args, "-o"); // 输出名称
    slice_push(ld_args, LINKER_OUTPUT); // 输出名称

    exec(tmp_dir, LINKER_ELF_NAME, ld_args);

    // 检测是否生成
    char *src_path = file_join(tmp_dir, LINKER_OUTPUT);
    char *dst_path = file_join(WORK_DIR, BUILD_OUTPUT);
    if (!file_exists(src_path)) {
        error_exit("[cross_linker] linker failed");
    }
    copy(dst_path, src_path, 0755);
}

char *cross_tmp_dir() {
    char *tmp_dir;
    if (str_equal(BUILD_OS, "linux")) {
        char temp[] = LINUX_BUILD_TMP_DIR;
        mkdtemp(temp);

        size_t size = strlen(LINUX_BUILD_TMP_DIR) + 1;
        tmp_dir = malloc(size);
        memset(tmp_dir, '\0', size);
        strcpy(tmp_dir, temp);
    } else {
        goto ERROR;
    }

    return tmp_dir;
    ERROR:
    error_exit("[cross_tmp_dir] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
}

// 默认 cpu 架构，影响 lower 和 elf


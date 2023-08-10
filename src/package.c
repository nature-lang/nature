#include "package.h"
#include "utils/slice.h"
#include "build/config.h"

static char *std_packages[] = {
        "std",
        "syscall",
        "strings",
        "libc"
        // 添加其他 std package
};

bool is_std_package(char *package) {
    // 遍历 std package 数组
    for (size_t i = 0; i < sizeof(std_packages) / sizeof(std_packages[0]); i++) {
        // 使用 strcmp 函数比较字符串是否相等
        if (strcmp(package, std_packages[i]) == 0) {
            return true; // 找到匹配的 std package
        }
    }

    return false; // 没有找到匹配的 std package
}

/**
 *
 * import foo.bar => foo is package.name, so import workdir/bar.n
 * import foo => import foo.foo => import workdir/foo.n
 * @param package_dir
 * @param ast_import_package  foo.bar.car 每一段都是一个字符串,放在了 import packages 中
 * @return
 */
char *package_import_fullpath(toml_table_t *package_conf, char *package_dir, slice_t *ast_import_package) {
    assert(package_dir);
    assert(ast_import_package);
    assert(ast_import_package->count > 0);

    char *entry = "main";
    toml_datum_t datum = toml_string_in(package_conf, "entry");
    if (datum.ok) {
        entry = datum.u.s;
    }
    assertf(!ends_with(entry, ".n"), "entry cannot end with .n, entry '%s'", entry);

    // 判断 temp 是否是一个文件夹
    char *temp = package_dir;
    for (int i = 1; i < ast_import_package->count; ++i) {
        temp = path_join(temp, ast_import_package->take[i]);
    }

    if (dir_exists(temp)) {
        // 拼接文件后缀
        temp = path_join(temp, entry);
    }

    // os + arch 文件
    char *os = os_to_string(BUILD_OS);
    char *arch = arch_to_string(BUILD_ARCH);
    char *os_arch = str_connect_by(os, arch, "_");
    char *os_arch_full_path = str_connect_by(temp, os_arch, ".");
    os_arch_full_path = str_connect(os_arch_full_path, ".n");
    if (file_exists(os_arch_full_path)) {
        return os_arch_full_path;
    }

    // os 文件
    char *os_full_path = str_connect_by(temp, os, ".");
    os_full_path = str_connect(os_full_path, ".n");
    if (file_exists(os_full_path)) {
        return os_full_path;
    }

    // 不带其他后缀
    char *full_path = str_connect(temp, ".n");
    assertf(file_exists(full_path), "cannot find file %s", full_path);

    return full_path;
}

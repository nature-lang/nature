#include "package.h"
#include "utils/slice.h"

static char *std_packages[] = {
        "os",
        "syscall",
        "string",
        // 添加其他 std package
};

static char *cross_suffix[] = {
        "linux_amd64.n",
        "linux_riscv64.n",
        "darwin_amd64.n",
        "linux.n",
        "darwin.n",
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
 * @param workdir
 * @param import_package  foo.bar.car 每一段都是一个字符串,放在了 import packages 中
 * @return
 */
char *package_import_fullpath(toml_table_t *package_conf, char *workdir, slice_t *import_package) {
    assert(workdir);
    assert(import_package);
    assert(import_package->count > 0);

    char *temp = workdir;
    if (import_package->count == 1) {
        // 默认 entry 为 main, 可以通过 entry 手动指定
        char *entry = "main";

        toml_datum_t datum = toml_string_in(package_conf, "entry");
        if (datum.ok) {
            entry = datum.u.s;
        }

        temp = path_join(temp, entry);

        // entry 不能 .n 结尾, 会导致无法跨平台使用
        assertf(!ends_with(temp, ".n"), "cannot end with .n, entry=%s", entry);
    } else {
        for (int i = 1; i < import_package->count; ++i) {
            temp = path_join(temp, import_package->take[i]);
        }
    }


    // cross 文件确认
    for (int i = 0; i < sizeof(cross_suffix) / sizeof(cross_suffix[0]); ++i) {
        char *suffix = cross_suffix[i];
        char *full_path = str_connect_by(temp, suffix, ".");
        if (file_exists(full_path)) {
            return full_path;
        }
    }

    char *full_path = str_connect(temp, ".n");
    assertf(file_exists(full_path), "cannot find file %s", full_path);

    return full_path;
}

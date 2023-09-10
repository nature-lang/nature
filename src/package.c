#include "package.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "build/config.h"
#include <dirent.h>
#include "utils/helper.h"

static table_t *std_package_table;
static table_t *std_temp_package_table;

bool is_std_package(char *package) {
    // 扫描 nature root 下的所有 文件，并注册到全局变量 std_packages 中
    if (std_package_table) {
        return table_exist(std_package_table, package);
    }

    std_package_table = table_new();

    // 遍历 NATURE_ROOT 下的 std 目录下的所有文件夹
    char *std_dir = path_join(NATURE_ROOT, "std");
    DIR *dir = opendir(std_dir);
    assertf(dir, "cannot found std dir %s", std_dir);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            if (!str_equal(entry->d_name, ".") &&
                !str_equal(entry->d_name, "..") &&
                !str_equal(entry->d_name, "temps")) {
                char *dirname = strdup(entry->d_name);
                table_set(std_package_table, dirname, (void *) 1);
            }
        }
    }

    closedir(dir);

    return table_exist(std_package_table, package);
}

bool is_std_temp_package(char *package) {
    // 扫描 nature root 下的所有 文件，并注册到全局变量 std_packages 中
    if (std_temp_package_table) {
        return table_exist(std_temp_package_table, package);
    }

    std_temp_package_table = table_new();

    // 遍历 NATURE_ROOT 下的 std 目录下的所有文件夹
    char *std_dir = path_join(NATURE_ROOT, "std");
    char *temp_dir = path_join(std_dir, "temps");

    DIR *dir = opendir(temp_dir);
    assertf(dir, "cannot found temp dir %s", temp_dir);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *filename = strdup(entry->d_name);

            filename = rtrim(filename, ".n");

            table_set(std_temp_package_table, filename, (void *) 1);
        }
    }

    closedir(dir);


    return table_exist(std_temp_package_table, package);
}

char *package_import_temp_fullpath(toml_table_t *package_conf, char *package_dir, slice_t *ast_import_package) {
    assert(package_dir);
    assert(ast_import_package);
    assert(ast_import_package->count == 2);

    // import package.test_temp
    char *temp_name = ast_import_package->take[1];
    assertf(temp_name, "import temp package exception");

    // package_conf 中查找 temp_name
    toml_table_t *temp_table = toml_table_in(package_conf, "templates");
    if (!temp_table) {
        return NULL;
    }

    toml_table_t *temp_entry_table = toml_table_in(temp_table, temp_name);
    if (!temp_entry_table) {
        return NULL;
    }

    // entry path  string
    toml_datum_t datum = toml_string_in(temp_entry_table, "path");
    assertf(datum.ok, "%s entry 'path' not found", temp_name);

    char *path = datum.u.s;
    assertf(path[0] != '.', "cannot use package %s temps path=%s begin with '.'", package_dir, path);
    assertf(path[0] != '/', "cannot use package %s temps absolute path=%s", package_dir, path);
    assertf(ends_with(path, ".n"), "cannot use package %s temps path=%s not end with .n", package_dir, path);

    path = path_join(package_dir, path);

    return path;
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
//    assertf(file_exists(full_path), "cannot find file %s", full_path);

    return full_path;
}

slice_t *package_links(char *package_dir, toml_table_t *package_conf) {
    if (!package_conf) {
        return NULL;
    }
    toml_table_t *link_table = toml_table_in(package_conf, "links");
    if (!link_table) {
        return NULL;
    }
    slice_t *result = slice_new();

    char *os_arch = dsprintf("%s_%s", os_to_string(BUILD_OS), arch_to_string(BUILD_ARCH));
    for (int i = 0; i < toml_table_ntab(link_table); ++i) {
        const char *key = toml_key_in(link_table, i);
        toml_table_t *table = toml_table_in(link_table, key);

        toml_datum_t datum = toml_string_in(table, os_arch);
        if (!datum.ok) {
            continue;
        }
        char *path = datum.u.s;

        // 只能使用相对路径
        assertf(path[0] != '.', "cannot use package %s temps path=%s begin with '.'", package_dir, path);
        assertf(path[0] != '/', "cannot use package %s temps absolute path=%s", package_dir, path);

        // 基于 package conf 所在目录生成绝对路劲
        path = path_join(package_dir, path);

        assertf(file_exists(path), "link path '%s' notfound", path);

        slice_push(result, path);
    }

    return result;
}


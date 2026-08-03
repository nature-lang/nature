#include "package.h"
#include "build/config.h"
#include "utils/helper.h"
#include "utils/slice.h"
#include "utils/table.h"
#include <dirent.h>

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
        if (str_equal(entry->d_name, ".") ||
            str_equal(entry->d_name, "..") ||
            str_equal(entry->d_name, "builtin") ||
            str_equal(entry->d_name, "temps")) {
            continue;
        }

        char *full_path = path_join(std_dir, entry->d_name);
        bool is_directory = dir_exists(full_path);
        free(full_path);
        if (is_directory) {
            char *dirname = strdup(entry->d_name);
            table_set(std_package_table, dirname, (void *) 1);
        }
    }

    closedir(dir);

    return table_exist(std_package_table, package);
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

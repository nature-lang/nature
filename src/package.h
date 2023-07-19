#ifndef NATURE_PACKAGE_H
#define NATURE_PACKAGE_H

#include "utils/toml.h"
#include "utils/assertf.h"
#include "utils/linked.h"

#define PACKAGE_TOML "package.toml"

linked_t *package_linked; // toml_table_t

typedef enum {
    DEP_TYPE_LOCAL = 1,
    DEP_TYPE_GIT = 2
} package_dep_type;

static inline toml_table_t *package_parser(char *file) {
    FILE *fp = fopen(file, "r");
    assertf(fp, "cannot open file %s", file);

    char errbuf[200];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    assertf(conf, "cannot parser - ", errbuf);

    return conf;
}

static inline void package_init() {
    char *package_file = path_join(WORK_DIR, PACKAGE_TOML);
    if (file_exists(package_file)) {
        // 启用了 package 模式
        DEBUGF("use package.toml")
        package_linked = linked_new();

        toml_table_t *main_package = package_parser(package_file);
        linked_push(package_linked, main_package);
    } else {
        DEBUGF("not package.toml")
        package_linked = NULL;
    }
}

static inline bool is_std_package(char *package) {
    // std package 数组
    const char *std_packages[] = {
            "os",
            "syscall",
            "string",
            // 添加其他 std package
    };

    // 遍历 std package 数组
    for (size_t i = 0; i < sizeof(std_packages) / sizeof(std_packages[0]); i++) {
        // 使用 strcmp 函数比较字符串是否相等
        if (strcmp(package, std_packages[i]) == 0) {
            return true; // 找到匹配的 std package
        }
    }

    return false; // 没有找到匹配的 std package
}

static inline bool is_current_package(toml_table_t *conf, char *package) {
    // 读取 conf 的 name 字段,判断是否与 package 一致
    toml_datum_t name = toml_string_in(conf, "name");

    return strcmp(name.u.s, package) == 0;
}

static inline bool is_dep_package(toml_table_t *conf, char *package) {
    toml_table_t *deps = toml_table_in(conf, "dependencies");
    if (!deps) {
        return false;
    }

    // deps key 如果是 package 则返回 true
    return toml_table_in(deps, package) != NULL;
}

/**
 *
 * import foo.bar => foo is package.name, so import workdir/bar.n
 * import foo => import foo.foo => import workdir/foo.n
 * @param workdir
 * @param import_package  foo.bar.car 每一段都是一个字符串,放在了 import packages 中
 * @return
 */
static inline char *module_full_path(char *workdir, slice_t *import_package) {
    assert(import_package);
    assert(import_package->count > 0);
    if (import_package->count == 1) {
        // import foo => import foo.foo => import workdir/foo.n
        return path_join(workdir, import_package->take[0]);

    }

    char *result = workdir;
    for (int i = 1; i < import_package->count; ++i) {
        result = path_join(result, import_package->take[i]);
    }

    return result;
}

static inline char *package_dep_str_in(toml_table_t *conf, char *package, char *key) {
    toml_table_t *deps = toml_table_in(conf, "dependencies");
    if (!deps) {
        return NULL;
    }

    toml_table_t *dep = toml_table_in(deps, package);
    if (!dep) {
        return NULL;
    }

    toml_datum_t datum = toml_string_in(dep, key);
    if (!datum.ok) {
        return NULL;
    }

    return datum.u.s;
}


#endif //NATURE_PACKAGE_H

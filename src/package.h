#ifndef NATURE_PACKAGE_H
#define NATURE_PACKAGE_H

#include "utils/toml.h"
#include "utils/assertf.h"
#include "utils/linked.h"
#include "utils/slice.h"

#define PACKAGE_TOML "package.toml"
#define TYPE_GIT "git"
#define TYPE_LOCAL "local"
#define PACKAGE_SOURCE_INFIX ".nature/package/sources"

typedef struct {
    char *path;
    char *impl;
} template_t;

bool is_std_package(char *package);

char *package_import_fullpath(toml_table_t *package_conf, char *package_dir, slice_t *ast_import_package);

static inline toml_table_t *package_parser(char *file) {
    FILE *fp = fopen(file, "r");
    assertf(fp, "cannot open file %s", file);

    char errbuf[200];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    assertf(conf, "cannot parser - ", errbuf);

    return conf;
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

static inline char *package_dep_git_dir(toml_table_t *conf, char *package) {
    char *home = homedir();
    assertf(home, "cannot find home dir");
    char *package_dir = path_join(home, PACKAGE_SOURCE_INFIX);


    char *url = package_dep_str_in(conf, package, "url");
    char *version = package_dep_str_in(conf, package, "version");

    url = str_replace(url, "/", ".");
    version = str_replace(version, "/", ".");
    url = str_connect_by(url, version, "@");

    return path_join(package_dir, url);
}


static inline char *package_dep_local_dir(toml_table_t *conf, char *package) {
    char *home = homedir();
    assertf(home, "cannot find home dir");
    char *package_dir = path_join(home, PACKAGE_SOURCE_INFIX);

    char *version = package_dep_str_in(conf, package, "version");
    char *path = str_replace(package, "/", ".");
    version = str_replace(version, "/", ".");
    path = str_connect_by(path, version, "@");

    return path_join(package_dir, path);
}

/**
 * name = "test"
 * templates = [
 *       { path = "temps/helper.temp.n", impl = "temps/test.a" }
 *       { path = "temps/builtin.temp.n", impl = "temps/test.o" }
 *       { path = "temps/builtin.temp.n", impl = "temps/test.llm" }
 *   ]
 * @param conf
 * @return
 */
static inline slice_t *package_templates(toml_table_t *conf) {
    if (!conf) {
        return NULL;
    }

    toml_array_t *temps = toml_array_in(conf, "templates");
    if (!temps) {
        return NULL;
    }
    slice_t *result = slice_new(); // template_t

    size_t len = toml_array_nelem(temps);
    for (int i = 0; i < len; ++i) {
        toml_table_t *temp = toml_table_at(temps, i);
        if (!temp) {
            continue;
        }
        toml_datum_t datum = toml_string_in(temp, "path");
        if (!datum.ok) {
            continue;
        }
        char *path = datum.u.s;
        assertf(ends_with(path, ".n"), "templates path must end with .n, index=%d, actual '%s'", i, path);
        assertf(file_exists(path), "templates path '%s' notfound", path);


        // 如果是 c 语言标准库中的函数已经实现了，就不需要专门搞进来了
        char *impl = NULL;
        datum = toml_string_in(temp, "impl");
        if (datum.ok) {
            impl = datum.u.s;
            assertf(ends_with(impl, ".a"), "templates impl only support .a, index=%d, actual '%s'", i, impl);
            assertf(file_exists(impl), "templates impl '%s' notfound", impl);
        }

        template_t *item = NEW(template_t);
        item->path = path;
        item->impl = impl;
        slice_push(result, item);
    }

    return result;
}

#endif //NATURE_PACKAGE_H

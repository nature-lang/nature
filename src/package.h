#ifndef NATURE_PACKAGE_H
#define NATURE_PACKAGE_H

#include "utils/toml.h"
#include "utils/assertf.h"
#include "utils/linked.h"
#include "utils/slice.h"
#include "build/config.h"


#define PACKAGE_TOML "package.toml"
#define TYPE_GIT "git"
#define TYPE_LOCAL "local"
#define PACKAGE_SOURCE_INFIX ".nature/package/sources"

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
 *       "temps/helper.temp.n",
 *       "temps/builtin.temp.n",
 *       "temps/builtin.temp.n"
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
        toml_datum_t datum = toml_string_at(temps, i);
        if (!datum.ok) {
            continue;
        }
        char *path = datum.u.s;
        assertf(ends_with(path, ".n"), "templates path must end with .n, index=%d, actual '%s'", i, path);
        assertf(file_exists(path), "templates path '%s' notfound", path);

        slice_push(result, path);
    }

    return result;
}

static slice_t *package_links(toml_table_t *package_conf) {
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
        slice_push(result, path);
    }

    return result;
}

#endif //NATURE_PACKAGE_H

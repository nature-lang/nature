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
    char* path = str_replace(package, "/", ".");
    version = str_replace(version, "/", ".");
    path = str_connect_by(path, version, "@");

    return path_join(package_dir, path);
}

#endif //NATURE_PACKAGE_H

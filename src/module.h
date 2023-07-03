#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "utils/linked.h"
#include "src/build/config.h"
#include "utils/helper.h"
#include "src/symbol/symbol.h"
#include "structs.h"

#define MODULE_SUFFIX ".n"

// module_path + path + ident
static inline char *ident_with_module(char *module_ident, char *ident) {
    if (!module_ident) {
        return ident;
    }

    char *temp = str_connect(module_ident, ".");
    temp = str_connect(temp, ident);
    return temp;
}

static inline char *make_unique_ident(module_t *m, char *ident) {
    char *result = malloc(strlen(ident) + sizeof(int) + 2);
    sprintf(result, "%s_%d", ident, m->var_unique_count++);
    return result;
}


static inline char *var_unique_ident(module_t *m, char *ident) {
    char *result = malloc(strlen(ident) + sizeof(int) + 2);
    sprintf(result, "%s_%d", ident, m->var_unique_count++);

    return ident_with_module(m->ident, result);
}


/**
 * @param importer_dir importer 所在的目录, 用来计算相对路径引入
 * @param import
 * @return
 */
void full_import(string importer_dir, ast_import_t *import);

module_t *module_build(string source_path, module_type_t type);

/**
 * 从 base_ns 开始，去掉结尾的 .n 部分
 * @param full_path
 * @return
 */
static inline char *module_unique_ident(char *full_path) {
    // 从 BASE_NS 开始，截止到目录部分
    char *result = str_replace(full_path, WORK_DIR, "");

    result = str_connect(BASE_NS, result);
    // 去掉结尾的 .n 部分
    result = rtrim(result, ".n");

    // replace dot
    result = str_replace(result, "/", ".");
    return result;
}

#endif //NATURE_MODULE_H

#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "utils/linked.h"
#include "utils/value.h"
#include "src/symbol/symbol.h"
#include "lir.h"
#include "structs.h"

#define MODULE_SUFFIX ".n"

// module_path + path + ident
static char *ident_with_module(char *module_ident, char *ident) {
    if (str_equal(module_ident, "")) {
        return ident;
    }

    char *temp = str_connect(module_ident, ".");
    temp = str_connect(temp, ident);
    return temp;
}

static char *unique_ident(module_t *m, char *ident) {
    char *unique_ident = malloc(strlen(ident) + sizeof(int) + 2);
    sprintf(unique_ident, "%s_%d", ident, m->var_unique_count++);
    return unique_ident;
}


static char *var_unique_ident(module_t *m, char *ident) {
    char *unique_ident = malloc(strlen(ident) + sizeof(int) + 2);
    sprintf(unique_ident, "%s_%d", ident, m->var_unique_count++);

    return ident_with_module(m->ident, unique_ident);
}


/**
 * @param importer_dir importer 所在的目录, 用来计算相对路径引入
 * @param import
 * @return
 */
void complete_import(string importer_dir, ast_import *import);

char *parser_base_ns(char *dir);

module_t *module_build(string source_path, module_type_t type);

/**
 * 从 base_ns 开始，去掉结尾的 .n 部分
 * @param full_path
 * @return
 */
static char *module_unique_ident(char *full_path) {
    char *result = str_replace(full_path, WORK_DIR, ""); // 从 BASE_NS 开始，截止到目录部分

    result = str_connect(BASE_NS, result);
    // 去掉结尾的 .n 部分
    result = rtrim(result, strlen(".n"));

    // replace dot
    result = str_replace(result, "/", ".");
    return result;
}

#endif //NATURE_MODULE_H

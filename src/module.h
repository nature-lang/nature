#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "src/build/config.h"
#include "src/symbol/symbol.h"
#include "types.h"
#include "utils/helper.h"
#include "utils/linked.h"

extern int var_unique_count;

// module_path + path + ident
static inline char *ident_with_module(char *module_ident, char *ident) {
    // template 是没有 module_ident 的
    if (!module_ident) {
        return ident;
    }

    char *temp = str_connect(module_ident, ".");
    temp = str_connect(temp, ident);
    return temp;
}

static inline char *make_unique_ident(module_t *m, char *ident) {
    char *result = malloc(strlen(ident) + sizeof(int) + 2);
    if (m->ident) {
        sprintf(result, "%s_%d", ident, m->var_unique_count++);
    } else {
        sprintf(result, "%s_%d", ident, var_unique_count++);
    }
    return result;
}

static inline char *label_unique_ident(module_t *m, char *ident) {
    char *result = malloc(strlen(ident) + sizeof(int) + 2);
    sprintf(result, "%s_%d", ident, m->var_unique_count++);
    return ident_with_module(m->label_prefix, result);
}

static inline char *var_unique_ident(module_t *m, char *ident) {
    char *result = malloc(strlen(ident) + sizeof(int) + 2);
    if (m->ident) {
        sprintf(result, "%s_%d", ident, m->var_unique_count++);
    } else {
        sprintf(result, "%s_%d", ident, var_unique_count++);
    }
    return ident_with_module(m->ident, result);
}

module_t *module_build(ast_import_t *import, char *source_path, module_type_t type);

/**
 * 从 base_ns 开始，去掉结尾的 .n 部分
 * @param full_path
 * @return
 */
static inline char *module_unique_ident(ast_import_t *import) {
    if (!import) {
        return "";
    }

    char *temp_dir = path_dir(import->package_dir);
    char *ident = str_replace(import->full_path, temp_dir, "");
    ident = ltrim(ident, "/");

    ident = rtrim(ident, ".n");
    ident = str_replace(ident, "/", ".");

    return ident;
}

#endif//NATURE_MODULE_H

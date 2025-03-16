#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "src/build/config.h"
#include "src/symbol/symbol.h"
#include "types.h"
#include "utils/helper.h"
#include "utils/linked.h"

extern int64_t global_var_unique_count;

// module_path + path + ident
static inline char *ident_with_prefix(char *module_ident, char *ident) {
    // template 是没有 module_ident 的
    if (!module_ident) {
        return ident;
    }

    char *temp = str_connect(module_ident, ".");
    temp = str_connect(temp, ident);
    return temp;
}

static inline char *label_ident_with_unique(module_t *m, char *ident) {
    int64_t size = strlen(ident) + 10 + 2;
    char *result = mallocz(size); // 预留 10 位数字
    snprintf(result, size, "%s_%ld", ident, global_var_unique_count++);
    return result;
}

static inline char *label_ident_with_prefix(module_t *m, char *ident) {
    int64_t size = strlen(ident) + 10 + 2;
    char *result = mallocz(size);
    snprintf(result, size, "%s_%d", ident, m->var_unique_count++);
    assert(m->label_prefix);
    return ident_with_prefix(m->label_prefix, result);
}

static inline char *var_ident_with_index(module_t *m, char *ident) {
    int64_t size = strlen(ident) + 10 + 2;
    char *result = mallocz(size);
    snprintf(result, size, "%s_%ld", ident, global_var_unique_count++);
    return result;
}

static inline char *var_unique_ident(module_t *m, char *ident) {
    int64_t size = strlen(ident) + 10 + 2;
    char *result = mallocz(size);

    if (m->ident) {
        snprintf(result, size, "%s_%d", ident, m->var_unique_count++);
    } else {
        snprintf(result, size, "%s_%ld", ident, global_var_unique_count++);
    }
    return ident_with_prefix(m->ident, result);
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

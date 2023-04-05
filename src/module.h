#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "utils/linked.h"
#include "utils/value.h"
#include "src/symbol/symbol.h"
#include "src/lir/lir.h"
#include "structs.h"

#define MODULE_SUFFIX ".n"

// module_path + path + ident
char *ident_with_module(char *module_ident, char *ident);

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
char *module_unique_ident(string full_path);

#endif //NATURE_MODULE_H

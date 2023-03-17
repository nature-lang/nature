#ifndef NATURE_BUILD_BUILD_H
#define NATURE_BUILD_BUILD_H

#include "src/module.h"

void build_init(char *build_entry);

void build_config_print();

slice_t *build_modules();

void build_compiler(slice_t *modules);

void build_assembler(slice_t *modules);

void build_linker(slice_t *modules);

void build(char *build_entry);

#endif //NATURE_BUILD_H

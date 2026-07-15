#ifndef NATURE_LD_TAPI_H
#define NATURE_LD_TAPI_H

#include "ld.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Semantic symbol metadata retained from a TAPI text-based stub.  The Mach-O
   input layer currently consumes regular and weak names; keeping the complete
   kind here lets the linker attach TLV/absolute import semantics without
   changing the YAML parser again. */
typedef enum {
    LD_TAPI_SYMBOL_REGULAR = 0,
    LD_TAPI_SYMBOL_ABSOLUTE,
    LD_TAPI_SYMBOL_TLV,
} ld_tapi_symbol_kind_t;

typedef struct {
    char *name;
    char *imported_name;
    ld_tapi_symbol_kind_t kind;
    bool weak;
    bool reexport;
} ld_tapi_symbol_t;

typedef struct {
    char *install_name;
    uint32_t current_version;
    uint32_t compatibility_version;
    ld_tapi_symbol_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    char **reexports;
    size_t reexport_count;
    size_t reexport_capacity;
} ld_tapi_stub_t;

typedef struct {
    size_t line;
    size_t column;
    char message[256];
} ld_tapi_error_t;

int ld_tapi_parse(const uint8_t *bytes, size_t size, ld_tapi_stub_t *stub,
                  ld_tapi_error_t *error);
void ld_tapi_stub_deinit(ld_tapi_stub_t *stub);

#endif

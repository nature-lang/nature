#include "ld_coff_capability.h"

#include "ld_coff_internal.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * This release-gate inventory follows the checked input classification and
 * ordinary-link capability model in lld/COFF/InputFiles.cpp, MinGW.cpp, and
 * llvm/Object/COFFObjectFile.cpp. It intentionally inventories, but never
 * invokes, the LTO implementation.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#define LD_COFF_CAPABILITY_SCHEMA \
    "nature.windows-sysroot-coff-capabilities.v2"
#define LD_COFF_CAPABILITY_LLVM_COMMIT \
    "c58ba1cf51d2886994da7e667a05c1bfe4f4396b"

static const uint8_t ld_coff_capability_clgl_magic[16] = {
        0x38,
        0xfe,
        0xb3,
        0x0c,
        0xa5,
        0xd9,
        0xab,
        0x4d,
        0xac,
        0x9b,
        0xd6,
        0xb6,
        0x22,
        0x26,
        0x53,
        0xc2,
};

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ld_coff_capability_strings_t;

typedef struct {
    uint16_t *items;
    size_t count;
    size_t capacity;
} ld_coff_capability_u16s_t;

typedef struct {
    uint8_t *items;
    size_t count;
    size_t capacity;
} ld_coff_capability_u8s_t;

typedef struct {
    char *path;
    const char *type;
} ld_coff_capability_input_t;

typedef struct {
    char *name;
    const char *container;
    const char *type;
    ld_coff_capability_strings_t definitions;
    ld_coff_capability_strings_t undefined_symbols;
    ld_coff_capability_strings_t weak_undefined_symbols;
    ld_coff_capability_strings_t weak_alias_sources;
    ld_coff_capability_strings_t weak_alias_targets;
    bool selected;
} ld_coff_capability_object_t;

typedef struct {
    char *object_name;
    char *dll;
    char *symbol;
    char *import_name;
    uint16_t version;
    uint16_t hint;
    uint8_t type;
    uint8_t name_type;
} ld_coff_capability_import_t;

typedef struct {
    ld_coff_capability_input_t *inputs;
    size_t input_count;
    size_t input_capacity;
    ld_coff_capability_object_t *objects;
    size_t object_count;
    size_t object_capacity;
    ld_coff_capability_import_t *imports;
    size_t import_count;
    size_t import_capacity;
    ld_coff_capability_u16s_t relocations;
    ld_coff_capability_u8s_t comdats;
    ld_coff_capability_strings_t directives;
    ld_coff_capability_strings_t undefined_symbols;
    ld_coff_capability_strings_t defined_symbols;
    ld_coff_capability_strings_t weak_undefined_symbols;
    ld_coff_capability_strings_t allowed_synthetic_symbols;
    ld_coff_capability_strings_t external_contract_symbols;
    ld_coff_capability_strings_t weak_unresolved_symbols;
    ld_coff_capability_strings_t hard_unresolved_symbols;
    ld_coff_capability_strings_t system_dlls;
    ld_coff_capability_strings_t refptr_symbols;
    bool pseudo_reloc_v2_marker;
    bool pseudo_reloc_runtime_symbols;
    char *error;
    size_t error_capacity;
} ld_coff_capability_state_t;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} ld_coff_capability_builder_t;

typedef enum {
    LD_COFF_CAPABILITY_LTO_NONE = 0,
    LD_COFF_CAPABILITY_LTO_LLVM_BITCODE,
    LD_COFF_CAPABILITY_LTO_MSVC_LTCG,
} ld_coff_capability_lto_t;

static void *ld_coff_capability_grow(void *items, size_t old_capacity,
                                     size_t new_capacity,
                                     size_t item_size) {
    if (item_size == 0U || new_capacity > SIZE_MAX / item_size) return NULL;
    void *grown = realloc(items, new_capacity * item_size);
    if (grown && new_capacity > old_capacity) {
        memset((uint8_t *) grown + old_capacity * item_size, 0,
               (new_capacity - old_capacity) * item_size);
    }
    return grown;
}

static char *ld_coff_capability_strdup(const char *value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

static unsigned char ld_coff_capability_ascii_lower(unsigned char value) {
    return value >= 'A' && value <= 'Z'
                   ? (unsigned char) (value + ('a' - 'A'))
                   : value;
}

static bool ld_coff_capability_ascii_space(unsigned char value) {
    return value == ' ' || (value >= '\t' && value <= '\r');
}

static bool ld_coff_capability_ascii_equal(const char *left,
                                           const char *right,
                                           size_t length) {
    for (size_t i = 0U; i < length; i++) {
        if (ld_coff_capability_ascii_lower((unsigned char) left[i]) !=
            ld_coff_capability_ascii_lower((unsigned char) right[i]))
            return false;
    }
    return true;
}

static int ld_coff_capability_ascii_compare(const char *left,
                                            const char *right) {
    while (*left && *right) {
        unsigned char a =
                ld_coff_capability_ascii_lower((unsigned char) *left++);
        unsigned char b =
                ld_coff_capability_ascii_lower((unsigned char) *right++);
        if (a != b) return a < b ? -1 : 1;
    }
    unsigned char a = (unsigned char) *left;
    unsigned char b = (unsigned char) *right;
    return a < b ? -1 : a > b;
}

static int ld_coff_capability_fail(ld_coff_capability_state_t *state,
                                   int status, const char *format, ...) {
    if (state && state->error && state->error_capacity != 0U) {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(state->error, state->error_capacity, format, arguments);
        va_end(arguments);
    }
    return status;
}

static void ld_coff_capability_diagnostic(void *context,
                                          ld_diag_level_t level,
                                          const char *message) {
    ld_coff_capability_state_t *state = context;
    if (!state || level != LD_DIAG_ERROR || !state->error ||
        state->error_capacity == 0U || state->error[0] != '\0')
        return;
    snprintf(state->error, state->error_capacity, "%s", message);
}

static bool ld_coff_capability_push_string(
        ld_coff_capability_strings_t *strings, const char *value) {
    for (size_t i = 0U; i < strings->count; i++) {
        if (strcmp(strings->items[i], value) == 0) return true;
    }
    if (strings->count == strings->capacity) {
        size_t next = strings->capacity ? strings->capacity * 2U : 16U;
        if (next < strings->capacity) return false;
        char **grown = ld_coff_capability_grow(
                strings->items, strings->capacity, next, sizeof(*grown));
        if (!grown) return false;
        strings->items = grown;
        strings->capacity = next;
    }
    char *copy = ld_coff_capability_strdup(value);
    if (!copy) return false;
    strings->items[strings->count++] = copy;
    return true;
}

static bool ld_coff_capability_push_string_n(
        ld_coff_capability_strings_t *strings, const char *value,
        size_t length) {
    if (length == SIZE_MAX) return false;
    char *copy = malloc(length + 1U);
    if (!copy) return false;
    memcpy(copy, value, length);
    copy[length] = '\0';
    bool pushed = ld_coff_capability_push_string(strings, copy);
    free(copy);
    return pushed;
}

static bool ld_coff_capability_append_string(
        ld_coff_capability_strings_t *strings, const char *value) {
    if (strings->count == strings->capacity) {
        size_t next = strings->capacity ? strings->capacity * 2U : 8U;
        if (next < strings->capacity) return false;
        char **grown = ld_coff_capability_grow(
                strings->items, strings->capacity, next, sizeof(*grown));
        if (!grown) return false;
        strings->items = grown;
        strings->capacity = next;
    }
    char *copy = ld_coff_capability_strdup(value);
    if (!copy) return false;
    strings->items[strings->count++] = copy;
    return true;
}

static bool ld_coff_capability_push_lower_string(
        ld_coff_capability_strings_t *strings, const char *value) {
    size_t length = strlen(value);
    if (length == SIZE_MAX) return false;
    char *lower = malloc(length + 1U);
    if (!lower) return false;
    for (size_t i = 0U; i < length; i++)
        lower[i] = (char) ld_coff_capability_ascii_lower(
                (unsigned char) value[i]);
    lower[length] = '\0';
    bool result = ld_coff_capability_push_string(strings, lower);
    free(lower);
    return result;
}

static bool ld_coff_capability_has_string(
        const ld_coff_capability_strings_t *strings, const char *value) {
    for (size_t i = 0U; i < strings->count; i++)
        if (strcmp(strings->items[i], value) == 0) return true;
    return false;
}

static bool ld_coff_capability_ascii_string_equal(const char *left,
                                                   const char *right) {
    size_t left_length = strlen(left);
    return left_length == strlen(right) &&
           ld_coff_capability_ascii_equal(left, right, left_length);
}

static bool ld_coff_capability_allowed_system_dll(const char *name) {
    static const char *allowed[] = {
            "advapi32.dll", "bcrypt.dll",  "crypt32.dll", "dbghelp.dll",
            "iphlpapi.dll", "kernel32.dll", "ntdll.dll",   "ole32.dll",
            "psapi.dll",    "secur32.dll",  "shell32.dll", "ucrtbase.dll",
            "user32.dll",   "userenv.dll",  "version.dll", "winmm.dll",
            "ws2_32.dll",
    };
    for (size_t i = 0U; i < sizeof(allowed) / sizeof(*allowed); i++)
        if (ld_coff_capability_ascii_string_equal(name, allowed[i]))
            return true;
    static const char api_set_prefix[] = "api-ms-win-crt-";
    size_t length = strlen(name);
    return length > sizeof(api_set_prefix) - 1U + 4U &&
           ld_coff_capability_ascii_equal(
                   name, api_set_prefix, sizeof(api_set_prefix) - 1U) &&
           ld_coff_capability_ascii_equal(name + length - 4U, ".dll", 4U);
}

static bool ld_coff_capability_linker_synthetic(const char *name) {
    static const char *synthetics[] = {
            "__ImageBase",
            "__image_base__",
            "__CTOR_LIST__",
            "__DTOR_LIST__",
            "__RUNTIME_PSEUDO_RELOC_LIST__",
            "__RUNTIME_PSEUDO_RELOC_LIST_END__",
            "__data_start__",
            "__data_end__",
            "__bss_start__",
            "__bss_end__",
    };
    for (size_t i = 0U; i < sizeof(synthetics) / sizeof(*synthetics); i++)
        if (strcmp(name, synthetics[i]) == 0) return true;
    return false;
}

static bool ld_coff_capability_external_contract(const char *name) {
    static const char *external[] = {
            "main",           "main.main",      "WinMain",
            "wWinMain",       "rt_data",
            "rt_rtype_data",  "rt_rtype_count", "rt_fndef_data",
            "rt_fndef_count", "rt_caller_data", "rt_caller_count",
            "rt_symdef_data", "rt_symdef_count", "rt_strtable_data",
    };
    for (size_t i = 0U; i < sizeof(external) / sizeof(*external); i++)
        if (strcmp(name, external[i]) == 0) return true;
    return false;
}

static bool ld_coff_capability_object_defines(
        const ld_coff_capability_object_t *object, const char *name) {
    return ld_coff_capability_has_string(&object->definitions, name);
}

static bool ld_coff_capability_object_provides(
        const ld_coff_capability_object_t *object, const char *name) {
    return ld_coff_capability_object_defines(object, name) ||
           ld_coff_capability_has_string(&object->weak_alias_sources, name);
}

static bool ld_coff_capability_symbol_resolved(
        const ld_coff_capability_state_t *state, const char *name,
        size_t depth) {
    if (depth > state->weak_undefined_symbols.count) return false;
    if (ld_coff_capability_linker_synthetic(name) ||
        ld_coff_capability_external_contract(name))
        return true;
    for (size_t i = 0U; i < state->object_count; i++) {
        const ld_coff_capability_object_t *object = &state->objects[i];
        if (object->selected &&
            ld_coff_capability_object_defines(object, name))
            return true;
    }
    for (size_t i = 0U; i < state->object_count; i++) {
        const ld_coff_capability_object_t *object = &state->objects[i];
        if (!object->selected) continue;
        for (size_t j = 0U; j < object->weak_alias_sources.count; j++) {
            if (strcmp(object->weak_alias_sources.items[j], name) != 0)
                continue;
            if (ld_coff_capability_symbol_resolved(
                        state, object->weak_alias_targets.items[j], depth + 1U))
                return true;
        }
    }
    return false;
}

static int ld_coff_capability_validate_symbol_closure(
        ld_coff_capability_state_t *state) {
    bool changed;
    do {
        changed = false;
        for (size_t i = 0U; i < state->object_count; i++) {
            ld_coff_capability_object_t *object = &state->objects[i];
            if (!object->selected) continue;
            for (size_t j = 0U; j < object->undefined_symbols.count; j++) {
                const char *name = object->undefined_symbols.items[j];
                if (ld_coff_capability_linker_synthetic(name)) {
                    if (!ld_coff_capability_push_string(
                                &state->allowed_synthetic_symbols, name))
                        return ld_coff_capability_fail(
                                state, LD_IO_ERROR, "out of memory");
                    continue;
                }
                if (ld_coff_capability_external_contract(name)) {
                    if (!ld_coff_capability_push_string(
                                &state->external_contract_symbols, name))
                        return ld_coff_capability_fail(
                                state, LD_IO_ERROR, "out of memory");
                    continue;
                }
                bool defined = false;
                for (size_t k = 0U; k < state->object_count; k++) {
                    ld_coff_capability_object_t *provider =
                            &state->objects[k];
                    if (provider->selected &&
                        ld_coff_capability_object_defines(provider, name)) {
                        defined = true;
                        break;
                    }
                }
                if (defined) continue;
                for (size_t k = 0U; k < state->object_count; k++) {
                    ld_coff_capability_object_t *provider =
                            &state->objects[k];
                    if (provider->selected ||
                        !ld_coff_capability_object_provides(provider, name))
                        continue;
                    provider->selected = true;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);

    const char *hard_source = NULL;
    for (size_t i = 0U; i < state->object_count; i++) {
        ld_coff_capability_object_t *object = &state->objects[i];
        if (!object->selected) continue;
        for (size_t j = 0U; j < object->undefined_symbols.count; j++) {
            const char *name = object->undefined_symbols.items[j];
            if (ld_coff_capability_linker_synthetic(name)) {
                if (!ld_coff_capability_push_string(
                            &state->allowed_synthetic_symbols, name))
                    return ld_coff_capability_fail(state, LD_IO_ERROR,
                                                   "out of memory");
                continue;
            }
            if (ld_coff_capability_external_contract(name)) {
                if (!ld_coff_capability_push_string(
                            &state->external_contract_symbols, name))
                    return ld_coff_capability_fail(state, LD_IO_ERROR,
                                                   "out of memory");
                continue;
            }
            if (ld_coff_capability_symbol_resolved(state, name, 0U)) continue;
            if (ld_coff_capability_has_string(
                        &object->weak_undefined_symbols, name)) {
                if (!ld_coff_capability_push_string(
                            &state->weak_unresolved_symbols, name))
                    return ld_coff_capability_fail(state, LD_IO_ERROR,
                                                   "out of memory");
                continue;
            }
            if (!ld_coff_capability_push_string(
                        &state->hard_unresolved_symbols, name))
                return ld_coff_capability_fail(state, LD_IO_ERROR,
                                               "out of memory");
            if (!hard_source) hard_source = object->name;
        }
    }
    if (state->hard_unresolved_symbols.count != 0U)
        return ld_coff_capability_fail(
                state, LD_SYMBOL_ERROR,
                "windows_amd64 sysroot has unresolved symbol '%s' referenced by '%s' in the default archive closure",
                state->hard_unresolved_symbols.items[0],
                hard_source ? hard_source : "<unknown>");
    return LD_OK;
}

static bool ld_coff_capability_push_u16(ld_coff_capability_u16s_t *values,
                                        uint16_t value) {
    for (size_t i = 0U; i < values->count; i++)
        if (values->items[i] == value) return true;
    if (values->count == values->capacity) {
        size_t next = values->capacity ? values->capacity * 2U : 16U;
        if (next < values->capacity) return false;
        uint16_t *grown = ld_coff_capability_grow(
                values->items, values->capacity, next, sizeof(*grown));
        if (!grown) return false;
        values->items = grown;
        values->capacity = next;
    }
    values->items[values->count++] = value;
    return true;
}

static bool ld_coff_capability_push_u8(ld_coff_capability_u8s_t *values,
                                       uint8_t value) {
    for (size_t i = 0U; i < values->count; i++)
        if (values->items[i] == value) return true;
    if (values->count == values->capacity) {
        size_t next = values->capacity ? values->capacity * 2U : 8U;
        if (next < values->capacity) return false;
        uint8_t *grown = ld_coff_capability_grow(
                values->items, values->capacity, next, sizeof(*grown));
        if (!grown) return false;
        values->items = grown;
        values->capacity = next;
    }
    values->items[values->count++] = value;
    return true;
}

static bool ld_coff_capability_push_input(
        ld_coff_capability_state_t *state, const char *path,
        const char *type) {
    if (state->input_count == state->input_capacity) {
        size_t next = state->input_capacity ? state->input_capacity * 2U : 8U;
        if (next < state->input_capacity) return false;
        ld_coff_capability_input_t *grown = ld_coff_capability_grow(
                state->inputs, state->input_capacity, next, sizeof(*grown));
        if (!grown) return false;
        state->inputs = grown;
        state->input_capacity = next;
    }
    char *copy = ld_coff_capability_strdup(path);
    if (!copy) return false;
    state->inputs[state->input_count++] =
            (ld_coff_capability_input_t) {.path = copy, .type = type};
    return true;
}

static ld_coff_capability_object_t *ld_coff_capability_push_object(
        ld_coff_capability_state_t *state, const char *name,
        const char *container, const char *type) {
    if (state->object_count == state->object_capacity) {
        size_t next =
                state->object_capacity ? state->object_capacity * 2U : 32U;
        if (next < state->object_capacity) return NULL;
        ld_coff_capability_object_t *grown = ld_coff_capability_grow(
                state->objects, state->object_capacity, next,
                sizeof(*grown));
        if (!grown) return NULL;
        state->objects = grown;
        state->object_capacity = next;
    }
    char *copy = ld_coff_capability_strdup(name);
    if (!copy) return NULL;
    ld_coff_capability_object_t *object =
            &state->objects[state->object_count++];
    *object =
            (ld_coff_capability_object_t) {.name = copy,
                                           .container = container,
                                           .type = type};
    return object;
}

static const char *ld_coff_capability_basename(const char *path) {
    const char *base = path ? path : "";
    for (const char *cursor = base; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1U;
    return base;
}

static char *ld_coff_capability_normalized_object_name(
        const ld_coff_object_t *object) {
    const char *container = object && object->file
                                    ? ld_coff_capability_basename(
                                              object->file->path)
                                    : "<input>";
    if (!object || !object->archive_member)
        return ld_coff_capability_strdup(container);
    const char *member = ld_coff_capability_basename(
            object->member_name ? object->member_name : "<member>");
    size_t container_length = strlen(container);
    size_t member_length = strlen(member);
    if (container_length > SIZE_MAX - member_length - 3U) return NULL;
    char *name = malloc(container_length + member_length + 3U);
    if (!name) return NULL;
    memcpy(name, container, container_length);
    name[container_length] = '(';
    memcpy(name + container_length + 1U, member, member_length);
    name[container_length + 1U + member_length] = ')';
    name[container_length + 2U + member_length] = '\0';
    return name;
}

static bool ld_coff_capability_push_import(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object,
        uint16_t version) {
    if (state->import_count == state->import_capacity) {
        size_t next =
                state->import_capacity ? state->import_capacity * 2U : 32U;
        if (next < state->import_capacity) return false;
        ld_coff_capability_import_t *grown = ld_coff_capability_grow(
                state->imports, state->import_capacity, next,
                sizeof(*grown));
        if (!grown) return false;
        state->imports = grown;
        state->import_capacity = next;
    }
    const ld_coff_import_t *source = object->import;
    ld_coff_capability_import_t value = {
            .object_name = ld_coff_capability_normalized_object_name(object),
            .dll = ld_coff_capability_strdup(source->dll_name),
            .symbol = ld_coff_capability_strdup(source->public_name),
            .import_name = ld_coff_capability_strdup(source->import_name),
            .version = version,
            .hint = source->ordinal_hint,
            .type = source->type,
            .name_type = source->name_type,
    };
    if (!value.object_name || !value.dll || !value.symbol ||
        !value.import_name) {
        free(value.object_name);
        free(value.dll);
        free(value.symbol);
        free(value.import_name);
        return false;
    }
    state->imports[state->import_count++] = value;
    return true;
}

static const char *ld_coff_capability_relocation_name(uint16_t type) {
    switch (type) {
        case LD_COFF_REL_AMD64_ABSOLUTE:
            return "ABSOLUTE";
        case LD_COFF_REL_AMD64_ADDR64:
            return "ADDR64";
        case LD_COFF_REL_AMD64_ADDR32:
            return "ADDR32";
        case LD_COFF_REL_AMD64_ADDR32NB:
            return "ADDR32NB";
        case LD_COFF_REL_AMD64_REL32:
            return "REL32";
        case LD_COFF_REL_AMD64_REL32_1:
            return "REL32_1";
        case LD_COFF_REL_AMD64_REL32_2:
            return "REL32_2";
        case LD_COFF_REL_AMD64_REL32_3:
            return "REL32_3";
        case LD_COFF_REL_AMD64_REL32_4:
            return "REL32_4";
        case LD_COFF_REL_AMD64_REL32_5:
            return "REL32_5";
        case LD_COFF_REL_AMD64_SECTION:
            return "SECTION";
        case LD_COFF_REL_AMD64_SECREL:
            return "SECREL";
        default:
            return NULL;
    }
}

static const char *ld_coff_capability_comdat_name(uint8_t selection) {
    switch (selection) {
        case LD_COFF_COMDAT_NODUPLICATES:
            return "NODUPLICATES";
        case LD_COFF_COMDAT_ANY:
            return "ANY";
        case LD_COFF_COMDAT_SAME_SIZE:
            return "SAME_SIZE";
        case LD_COFF_COMDAT_EXACT_MATCH:
            return "EXACT_MATCH";
        case LD_COFF_COMDAT_ASSOCIATIVE:
            return "ASSOCIATIVE";
        case LD_COFF_COMDAT_LARGEST:
            return "LARGEST";
        case LD_COFF_COMDAT_NEWEST:
            return "NEWEST";
        default:
            return NULL;
    }
}

static const char *ld_coff_capability_import_type_name(uint8_t type) {
    switch (type) {
        case LD_COFF_IMPORT_CODE:
            return "code";
        case LD_COFF_IMPORT_DATA:
            return "data";
        case LD_COFF_IMPORT_CONST:
            return "const";
        default:
            return NULL;
    }
}

static const char *ld_coff_capability_import_name_type_name(uint8_t type) {
    switch (type) {
        case LD_COFF_IMPORT_ORDINAL:
            return "ordinal";
        case LD_COFF_IMPORT_NAME:
            return "name";
        case LD_COFF_IMPORT_NAME_NOPREFIX:
            return "name_noprefix";
        case LD_COFF_IMPORT_NAME_UNDECORATE:
            return "name_undecorate";
        case LD_COFF_IMPORT_NAME_EXPORTAS:
            return "name_exportas";
        default:
            return NULL;
    }
}

static bool ld_coff_capability_value(const char *value, size_t length,
                                     const char **start,
                                     size_t *result_length) {
    if (!value || length == 0U) return false;
    if (length >= 2U && value[0] == '"' && value[length - 1U] == '"') {
        value++;
        length -= 2U;
    }
    if (length == 0U) return false;
    *start = value;
    *result_length = length;
    return true;
}

static int ld_coff_capability_validate_directive(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object,
        ld_coff_capability_object_t *capability_object, const char *token) {
    const char *body = token;
    if (*body == '/' || *body == '-') body++;
    const char *colon = strchr(body, ':');
    const char *value = colon ? colon + 1U : NULL;
    size_t key_length = colon ? (size_t) (colon - body) : strlen(body);
    size_t value_length = value ? strlen(value) : 0U;
    const char *unquoted = NULL;
    size_t unquoted_length = 0U;
    bool has_value = ld_coff_capability_value(
            value, value_length, &unquoted, &unquoted_length);
    bool supported = false;
    bool valid = false;
    if (key_length == 10U &&
        ld_coff_capability_ascii_equal(body, "DEFAULTLIB", 10U)) {
        supported = true;
        valid = has_value;
    } else if (key_length == 12U &&
               ld_coff_capability_ascii_equal(body, "NODEFAULTLIB", 12U)) {
        supported = true;
        /* No value, including a present-but-empty colon, means all libs. */
        valid = true;
    } else if (key_length == 13U &&
               ld_coff_capability_ascii_equal(body, "ALTERNATENAME", 13U)) {
        supported = true;
        const char *equals = has_value
                                     ? memchr(unquoted, '=', unquoted_length)
                                     : NULL;
        valid = equals && equals != unquoted &&
                (size_t) (equals - unquoted) + 1U < unquoted_length;
    } else if (key_length == 7U &&
               ld_coff_capability_ascii_equal(body, "INCLUDE", 7U)) {
        supported = true;
        valid = has_value;
    } else if (key_length == 14U &&
               ld_coff_capability_ascii_equal(body, "FAILIFMISMATCH", 14U)) {
        supported = true;
        valid = has_value && memchr(unquoted, '=', unquoted_length) != NULL;
    } else if (key_length == 15U &&
               ld_coff_capability_ascii_equal(body, "EXCLUDE-SYMBOLS", 15U)) {
        supported = true;
        valid = has_value;
    }
    if (!supported) {
        return ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: unsupported .drectve capability '%s'",
                object->display_name, token);
    }
    if (!valid) {
        return ld_coff_capability_fail(
                state, LD_INVALID_INPUT,
                "%s: malformed supported .drectve token '%s'",
                object->display_name, token);
    }
    if (!ld_coff_capability_push_string(&state->directives, token))
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    if (key_length == 7U &&
        ld_coff_capability_ascii_equal(body, "INCLUDE", 7U) &&
        (!ld_coff_capability_push_string_n(&state->undefined_symbols, unquoted,
                                           unquoted_length) ||
         !ld_coff_capability_push_string_n(
                 &capability_object->undefined_symbols, unquoted,
                 unquoted_length)))
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    return LD_OK;
}

static int ld_coff_capability_scan_directives(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object,
        ld_coff_capability_object_t *capability_object,
        const ld_coff_section_t *section) {
    size_t offset = 0U;
    while (offset < section->data_size) {
        while (offset < section->data_size &&
               ld_coff_capability_ascii_space(section->data[offset]))
            offset++;
        if (offset == section->data_size) break;
        size_t start = offset;
        bool quoted = false;
        while (offset < section->data_size) {
            unsigned char character = section->data[offset];
            if (character == 0U)
                return ld_coff_capability_fail(
                        state, LD_INVALID_INPUT,
                        "%s: embedded NUL in .drectve", object->display_name);
            if (character == '"') quoted = !quoted;
            if (!quoted && ld_coff_capability_ascii_space(character)) break;
            offset++;
        }
        if (quoted)
            return ld_coff_capability_fail(
                    state, LD_INVALID_INPUT,
                    "%s: unterminated quote in .drectve",
                    object->display_name);
        size_t length = offset - start;
        char *token = malloc(length + 1U);
        if (!token)
            return ld_coff_capability_fail(state, LD_IO_ERROR,
                                           "out of memory");
        memcpy(token, section->data + start, length);
        token[length] = '\0';
        int status = ld_coff_capability_validate_directive(
                state, object, capability_object, token);
        free(token);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static bool ld_coff_capability_decimal(const uint8_t *bytes, size_t length,
                                       uint64_t *value) {
    size_t first = 0U;
    while (first < length && bytes[first] == ' ') first++;
    while (length > first && bytes[length - 1U] == ' ') length--;
    if (first == length) return false;
    uint64_t result = 0U;
    for (size_t i = first; i < length; i++) {
        if (bytes[i] < '0' || bytes[i] > '9') return false;
        uint64_t digit = (uint64_t) (bytes[i] - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static ld_coff_capability_lto_t ld_coff_capability_payload_lto(
        const uint8_t *bytes, size_t size) {
    static const uint8_t raw[] = {'B', 'C', 0xc0, 0xde};
    static const uint8_t wrapper[] = {0xde, 0xc0, 0x17, 0x0b};
    if (size >= sizeof(raw) && memcmp(bytes, raw, sizeof(raw)) == 0)
        return LD_COFF_CAPABILITY_LTO_LLVM_BITCODE;
    if (size >= sizeof(wrapper) &&
        memcmp(bytes, wrapper, sizeof(wrapper)) == 0)
        return LD_COFF_CAPABILITY_LTO_LLVM_BITCODE;
    if (size >= LD_COFF_BIGOBJ_HEADER_SIZE && bytes[0] == 0U &&
        bytes[1] == 0U && bytes[2] == 0xffU && bytes[3] == 0xffU &&
        memcmp(bytes + 12U, ld_coff_capability_clgl_magic,
               sizeof(ld_coff_capability_clgl_magic)) == 0)
        return LD_COFF_CAPABILITY_LTO_MSVC_LTCG;
    return LD_COFF_CAPABILITY_LTO_NONE;
}

static bool ld_coff_capability_archive_special(const uint8_t *name,
                                               size_t length) {
    while (length && name[length - 1U] == ' ') length--;
    return (length == 1U && name[0] == '/') ||
           (length == 2U && name[0] == '/' && name[1] == '/') ||
           (length == 7U && memcmp(name, "/SYM64/", 7U) == 0) ||
           (length >= 9U && memcmp(name, "__.SYMDEF", 9U) == 0);
}

static ld_coff_capability_lto_t ld_coff_capability_file_lto(
        const ld_coff_file_t *file) {
    if (!file || !file->bytes) return LD_COFF_CAPABILITY_LTO_NONE;
    ld_coff_capability_lto_t direct =
            ld_coff_capability_payload_lto(file->bytes, file->size);
    if (direct != LD_COFF_CAPABILITY_LTO_NONE) return direct;
    if (file->size < LD_COFF_ARCHIVE_MAGIC_SIZE ||
        memcmp(file->bytes, LD_COFF_ARCHIVE_MAGIC,
               LD_COFF_ARCHIVE_MAGIC_SIZE) != 0)
        return LD_COFF_CAPABILITY_LTO_NONE;
    ld_coff_view_t view = {file->bytes, file->size};
    size_t offset = LD_COFF_ARCHIVE_MAGIC_SIZE;
    while (offset < file->size) {
        if (!ld_coff_range_ok(view, offset, LD_COFF_ARCHIVE_HEADER_SIZE))
            return LD_COFF_CAPABILITY_LTO_NONE;
        const uint8_t *header = file->bytes + offset;
        uint64_t size64 = 0U;
        if (!ld_coff_capability_decimal(header + 48U, 10U, &size64) ||
            size64 > SIZE_MAX)
            return LD_COFF_CAPABILITY_LTO_NONE;
        size_t payload_offset = offset + LD_COFF_ARCHIVE_HEADER_SIZE;
        size_t payload_size = (size_t) size64;
        if (!ld_coff_range_ok(view, payload_offset, payload_size))
            return LD_COFF_CAPABILITY_LTO_NONE;
        if (!ld_coff_capability_archive_special(header, 16U)) {
            size_t name_length = 16U;
            while (name_length && header[name_length - 1U] == ' ')
                name_length--;
            if (name_length >= 3U && header[0] == '#' && header[1] == '1' &&
                header[2] == '/') {
                uint64_t embedded = 0U;
                if (ld_coff_capability_decimal(header + 3U,
                                               name_length - 3U,
                                               &embedded) &&
                    embedded <= payload_size) {
                    payload_offset += (size_t) embedded;
                    payload_size -= (size_t) embedded;
                }
            }
            ld_coff_capability_lto_t member = ld_coff_capability_payload_lto(
                    file->bytes + payload_offset, payload_size);
            if (member != LD_COFF_CAPABILITY_LTO_NONE) return member;
        }
        uint64_t next = (uint64_t) offset + LD_COFF_ARCHIVE_HEADER_SIZE +
                        (uint64_t) size64;
        if (next & 1U) next++;
        if (next > file->size) return LD_COFF_CAPABILITY_LTO_NONE;
        offset = (size_t) next;
    }
    return LD_COFF_CAPABILITY_LTO_NONE;
}

static bool ld_coff_capability_is_pseudo_list(const char *name) {
    return strcmp(name, "__RUNTIME_PSEUDO_RELOC_LIST__") == 0 ||
           strcmp(name, "_RUNTIME_PSEUDO_RELOC_LIST__") == 0 ||
           strcmp(name, "___RUNTIME_PSEUDO_RELOC_LIST__") == 0;
}

static bool ld_coff_capability_is_pseudo_list_end(const char *name) {
    return strcmp(name, "__RUNTIME_PSEUDO_RELOC_LIST_END__") == 0 ||
           strcmp(name, "_RUNTIME_PSEUDO_RELOC_LIST_END__") == 0 ||
           strcmp(name, "___RUNTIME_PSEUDO_RELOC_LIST_END__") == 0;
}

static bool ld_coff_capability_is_pseudo_runtime_symbol(const char *name) {
    return ld_coff_capability_is_pseudo_list(name) ||
           ld_coff_capability_is_pseudo_list_end(name) ||
           strcmp(name, "_pei386_runtime_relocator") == 0;
}

static bool ld_coff_capability_has_pseudo_v2_marker(
        const ld_coff_symbol_t *symbol) {
    if (!ld_coff_capability_is_pseudo_list(symbol->name) || !symbol->section ||
        !symbol->section->data || symbol->value > symbol->section->data_size ||
        symbol->section->data_size - symbol->value < 12U)
        return false;
    ld_coff_view_t marker = {
            symbol->section->data + symbol->value,
            symbol->section->data_size - symbol->value,
    };
    uint32_t magic1 = 1U;
    uint32_t magic2 = 1U;
    uint32_t version = 0U;
    if (ld_coff_read_u32(marker, 0U, &magic1) &&
        ld_coff_read_u32(marker, 4U, &magic2) &&
        ld_coff_read_u32(marker, 8U, &version) && magic1 == 0U &&
        magic2 == 0U && version == 1U)
        return true;
    return false;
}

static int ld_coff_capability_validate_pseudo_relocations(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object) {
    for (size_t i = 0U; i < object->symbol_count; i++) {
        const ld_coff_symbol_t *start = &object->symbols[i];
        if (start->auxiliary || !start->name || !start->section ||
            !ld_coff_capability_is_pseudo_list(start->name) ||
            !start->section->data ||
            (start->section->characteristics &
             LD_COFF_SCN_CNT_INITIALIZED_DATA) == 0U)
            continue;
        for (size_t j = 0U; j < object->symbol_count; j++) {
            const ld_coff_symbol_t *end = &object->symbols[j];
            if (end->auxiliary || !end->name ||
                !ld_coff_capability_is_pseudo_list_end(end->name) ||
                end->section != start->section)
                continue;
            if (end->value < start->value ||
                end->value > start->section->data_size)
                return ld_coff_capability_fail(
                        state, LD_INVALID_INPUT,
                        "%s(%s): invalid MinGW pseudo-relocation table range "
                        "%u..%u",
                        object->display_name, start->section->name,
                        start->value, end->value);
            uint32_t size = end->value - start->value;
            if (size == 0U ||
                (size == 12U &&
                 ld_coff_capability_has_pseudo_v2_marker(start)))
                continue;
            return ld_coff_capability_fail(
                    state, LD_UNSUPPORTED,
                    "%s(%s): non-empty MinGW pseudo-relocation table (%u "
                    "bytes) is unsupported; rebuild the sysroot without "
                    "automatic data imports",
                    object->display_name, start->section->name, size);
        }
    }
    return LD_OK;
}

static int ld_coff_capability_scan_import(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object,
        ld_coff_capability_object_t *capability_object) {
    ld_coff_view_t view = {object->bytes, object->size};
    uint16_t version = 0U;
    uint16_t type_info = 0U;
    if (!ld_coff_read_u16(view, 4U, &version) ||
        !ld_coff_read_u16(view, 18U, &type_info))
        return ld_coff_capability_fail(
                state, LD_INVALID_INPUT,
                "%s: truncated short import capability header",
                object->display_name);
    if (version != 0U)
        return ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: unsupported short import header version %u",
                object->display_name, version);
    if ((type_info & UINT16_C(0xffe0)) != 0U)
        return ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: unsupported short import TypeInfo bits 0x%04x",
                object->display_name, type_info);
    if (!ld_coff_capability_import_type_name(object->import->type) ||
        !ld_coff_capability_import_name_type_name(object->import->name_type))
        return ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: unsupported short import type/name type %u/%u",
                object->display_name, object->import->type,
                object->import->name_type);
    if (!ld_coff_capability_allowed_system_dll(object->import->dll_name))
        return ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: import DLL '%s' is outside the windows_amd64 system DLL allowlist",
                object->display_name, object->import->dll_name);
    if (!ld_coff_capability_push_import(state, object, version) ||
        !ld_coff_capability_push_lower_string(&state->system_dlls,
                                              object->import->dll_name))
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    if (!ld_coff_capability_push_string(&state->defined_symbols,
                                        object->import->public_name) ||
        !ld_coff_capability_push_string(&capability_object->definitions,
                                        object->import->public_name))
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    size_t public_length = strlen(object->import->public_name);
    if (public_length > SIZE_MAX - 7U)
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    char *iat_name = malloc(public_length + 7U);
    if (!iat_name)
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    memcpy(iat_name, "__imp_", 6U);
    memcpy(iat_name + 6U, object->import->public_name, public_length + 1U);
    bool pushed = ld_coff_capability_push_string(&state->defined_symbols,
                                                 iat_name) &&
                  ld_coff_capability_push_string(
                          &capability_object->definitions, iat_name);
    free(iat_name);
    if (!pushed)
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    return LD_OK;
}

static int ld_coff_capability_scan_object(
        ld_coff_capability_state_t *state, const ld_coff_object_t *object,
        bool capability_root) {
    if (object->bigobj) {
        ld_coff_view_t view = {object->bytes, object->size};
        uint16_t version = 0U;
        if (!ld_coff_read_u16(view, 4U, &version))
            return ld_coff_capability_fail(
                    state, LD_INVALID_INPUT,
                    "%s: truncated BigObj capability header",
                    object->display_name);
        if (version != 2U)
            return ld_coff_capability_fail(
                    state, LD_UNSUPPORTED,
                    "%s: unsupported BigObj header version %u",
                    object->display_name, version);
    }
    const char *object_type =
            object->import_object ? "short_import"
                                  : (object->bigobj ? "bigobj" : "coff");
    const char *container = object->archive_member ? "archive" : "direct";
    char *normalized_name =
            ld_coff_capability_normalized_object_name(object);
    if (!normalized_name)
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    ld_coff_capability_object_t *capability_object =
            ld_coff_capability_push_object(state, normalized_name, container,
                                           object_type);
    free(normalized_name);
    if (!capability_object)
        return ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    capability_object->selected = !object->archive_member || capability_root;
    if (object->import_object)
        return ld_coff_capability_scan_import(state, object,
                                              capability_object);

    for (size_t i = 0U; i < object->section_count; i++) {
        const ld_coff_section_t *section = &object->sections[i];
        if (strcmp(section->name, ".llvm.lto") == 0)
            return ld_coff_capability_fail(
                    state, LD_UNSUPPORTED,
                    "%s: LLVM fat-LTO section '.llvm.lto' is unsupported; "
                    "rebuild the sysroot object with -fno-lto",
                    object->display_name);
        bool comdat = (section->characteristics & LD_COFF_SCN_LNK_COMDAT) != 0U;
        if (comdat) {
            if (!ld_coff_capability_comdat_name(section->comdat_selection))
                return ld_coff_capability_fail(
                        state, LD_UNSUPPORTED,
                        "%s(%s): unsupported or missing COMDAT selection %u",
                        object->display_name, section->name,
                        section->comdat_selection);
            if (section->comdat_selection == LD_COFF_COMDAT_ASSOCIATIVE &&
                (section->associative_section == 0U ||
                 section->associative_section > object->section_count))
                return ld_coff_capability_fail(
                        state, LD_INVALID_INPUT,
                        "%s(%s): associative COMDAT parent %u is invalid",
                        object->display_name, section->name,
                        section->associative_section);
            if (!ld_coff_capability_push_u8(&state->comdats,
                                            section->comdat_selection))
                return ld_coff_capability_fail(state, LD_IO_ERROR,
                                               "out of memory");
        } else if (section->comdat_selection != 0U) {
            return ld_coff_capability_fail(
                    state, LD_INVALID_INPUT,
                    "%s(%s): COMDAT selection %u appears without the COMDAT "
                    "section flag",
                    object->display_name, section->name,
                    section->comdat_selection);
        }
        for (size_t j = 0U; j < section->relocation_count; j++) {
            uint16_t type = section->relocations[j].type;
            if (!ld_coff_capability_relocation_name(type))
                return ld_coff_capability_fail(
                        state, LD_UNSUPPORTED,
                        "%s(%s): unsupported AMD64 relocation type 0x%04x",
                        object->display_name, section->name, type);
            if (!ld_coff_capability_push_u16(&state->relocations, type))
                return ld_coff_capability_fail(state, LD_IO_ERROR,
                                               "out of memory");
        }
        if (strcmp(section->name, ".drectve") == 0) {
            int status = ld_coff_capability_scan_directives(
                    state, object, capability_object, section);
            if (status != LD_OK) return status;
        }
    }

    for (size_t i = 0U; i < object->symbol_count; i++) {
        const ld_coff_symbol_t *symbol = &object->symbols[i];
        if (symbol->auxiliary || !symbol->name || !*symbol->name) continue;
        bool external = symbol->storage_class == LD_COFF_STORAGE_CLASS_EXTERNAL ||
                        symbol->storage_class ==
                                LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL;
        if (external && symbol->section_number == LD_COFF_SYM_UNDEFINED &&
            symbol->value == 0U) {
            if (!ld_coff_capability_push_string(&state->undefined_symbols,
                                                symbol->name) ||
                !ld_coff_capability_push_string(
                        &capability_object->undefined_symbols, symbol->name) ||
                (symbol->storage_class ==
                                 LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL &&
                 (!ld_coff_capability_push_string(
                          &state->weak_undefined_symbols, symbol->name) ||
                  !ld_coff_capability_push_string(
                          &capability_object->weak_undefined_symbols,
                          symbol->name))))
                return ld_coff_capability_fail(state, LD_IO_ERROR,
                                               "out of memory");
        } else if (external &&
                   (!ld_coff_capability_push_string(&state->defined_symbols,
                                                    symbol->name) ||
                    !ld_coff_capability_push_string(
                            &capability_object->definitions, symbol->name))) {
            return ld_coff_capability_fail(state, LD_IO_ERROR,
                                           "out of memory");
        }
        if (strncmp(symbol->name, ".refptr.", 8U) == 0 &&
            !ld_coff_capability_push_string(&state->refptr_symbols,
                                            symbol->name))
            return ld_coff_capability_fail(state, LD_IO_ERROR,
                                           "out of memory");
        if (ld_coff_capability_is_pseudo_runtime_symbol(symbol->name)) {
            state->pseudo_reloc_runtime_symbols = true;
            if (ld_coff_capability_has_pseudo_v2_marker(symbol))
                state->pseudo_reloc_v2_marker = true;
        }
    }
    for (size_t i = 0U; i < object->symbol_count; i++) {
        const ld_coff_symbol_t *symbol = &object->symbols[i];
        if (symbol->auxiliary ||
            symbol->storage_class != LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL)
            continue;
        if (symbol->weak_target_index >= object->symbol_count) {
            return ld_coff_capability_fail(
                    state, LD_INVALID_INPUT,
                    "%s: weak external '%s' has invalid fallback symbol index %u",
                    object->display_name, symbol->name,
                    symbol->weak_target_index);
        }
        const ld_coff_symbol_t *target =
                &object->symbols[symbol->weak_target_index];
        if (target->auxiliary || !target->name || !*target->name)
            return ld_coff_capability_fail(
                    state, LD_INVALID_INPUT,
                    "%s: weak external '%s' has an invalid fallback symbol",
                    object->display_name, symbol->name);
        if (!ld_coff_capability_append_string(
                    &capability_object->weak_alias_sources, symbol->name) ||
            !ld_coff_capability_append_string(
                    &capability_object->weak_alias_targets, target->name))
            return ld_coff_capability_fail(state, LD_IO_ERROR,
                                           "out of memory");
    }
    return ld_coff_capability_validate_pseudo_relocations(state, object);
}

static const char *ld_coff_capability_direct_type(
        const ld_coff_context_t *context) {
    if (context->object_count == 0U) return "unknown";
    const ld_coff_object_t *object = context->objects[0];
    if (object->import_object) return "short_import";
    return object->bigobj ? "bigobj" : "coff";
}

static bool ld_coff_capability_archive_is_root(const char *path) {
    const char *base = path;
    for (const char *cursor = path; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1U;
    static const char *roots[] = {
            "libruntime.a",    "libuv.a",       "compiler_rt.lib",
            "libmbedtls.a",    "libmbedx509.a", "libmbedcrypto.a",
    };
    for (size_t i = 0U; i < sizeof(roots) / sizeof(*roots); i++)
        if (strcmp(base, roots[i]) == 0) return true;
    return false;
}

static int ld_coff_capability_scan_path(ld_coff_capability_state_t *state,
                                        const char *path) {
    ld_options_t options;
    memset(&options, 0, sizeof(options));
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = ld_coff_capability_diagnostic;
    options.diagnostic_context = state;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);
    int status = ld_coff_load_input(&context, path);
    ld_coff_capability_lto_t lto = context.file_count
                                           ? ld_coff_capability_file_lto(
                                                     context.files[0])
                                           : LD_COFF_CAPABILITY_LTO_NONE;
    if (lto == LD_COFF_CAPABILITY_LTO_LLVM_BITCODE) {
        status = ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: LLVM bitcode/LTO is unsupported; rebuild the sysroot "
                "input with -fno-lto",
                path);
    } else if (lto == LD_COFF_CAPABILITY_LTO_MSVC_LTCG) {
        status = ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: MSVC LTCG object is unsupported; rebuild without /GL",
                path);
    }
    if (status != LD_OK) {
        if (state->error && state->error_capacity && !state->error[0])
            ld_coff_capability_fail(state, status,
                                    "%s: COFF capability parse failed", path);
        ld_coff_context_deinit(&context);
        return status;
    }
    if (context.file_count != 1U) {
        status = ld_coff_capability_fail(
                state, LD_INVALID_INPUT,
                "%s: checked COFF parser produced an unexpected file count",
                path);
        ld_coff_context_deinit(&context);
        return status;
    }
    const ld_coff_file_t *file = context.files[0];
    bool archive = file->size >= LD_COFF_ARCHIVE_MAGIC_SIZE &&
                   memcmp(file->bytes, LD_COFF_ARCHIVE_MAGIC,
                          LD_COFF_ARCHIVE_MAGIC_SIZE) == 0;
    const char *input_type =
            archive ? "archive" : ld_coff_capability_direct_type(&context);
    if (strcmp(input_type, "unknown") == 0) {
        status = ld_coff_capability_fail(
                state, LD_UNSUPPORTED,
                "%s: unknown COFF input/header capability", path);
    } else if (!ld_coff_capability_push_input(state, path, input_type)) {
        status = ld_coff_capability_fail(state, LD_IO_ERROR, "out of memory");
    }
    bool capability_root =
            archive && ld_coff_capability_archive_is_root(path);
    for (size_t i = 0U; status == LD_OK && i < context.object_count; i++)
        status = ld_coff_capability_scan_object(
                state, context.objects[i], capability_root);
    ld_coff_context_deinit(&context);
    return status;
}

static void ld_coff_capability_strings_deinit(
        ld_coff_capability_strings_t *strings) {
    for (size_t i = 0U; i < strings->count; i++) free(strings->items[i]);
    free(strings->items);
    memset(strings, 0, sizeof(*strings));
}

static void ld_coff_capability_state_deinit(
        ld_coff_capability_state_t *state) {
    for (size_t i = 0U; i < state->input_count; i++)
        free(state->inputs[i].path);
    for (size_t i = 0U; i < state->object_count; i++) {
        free(state->objects[i].name);
        ld_coff_capability_strings_deinit(&state->objects[i].definitions);
        ld_coff_capability_strings_deinit(
                &state->objects[i].undefined_symbols);
        ld_coff_capability_strings_deinit(
                &state->objects[i].weak_undefined_symbols);
        ld_coff_capability_strings_deinit(
                &state->objects[i].weak_alias_sources);
        ld_coff_capability_strings_deinit(
                &state->objects[i].weak_alias_targets);
    }
    for (size_t i = 0U; i < state->import_count; i++) {
        free(state->imports[i].object_name);
        free(state->imports[i].dll);
        free(state->imports[i].symbol);
        free(state->imports[i].import_name);
    }
    free(state->inputs);
    free(state->objects);
    free(state->imports);
    free(state->relocations.items);
    free(state->comdats.items);
    ld_coff_capability_strings_deinit(&state->directives);
    ld_coff_capability_strings_deinit(&state->undefined_symbols);
    ld_coff_capability_strings_deinit(&state->defined_symbols);
    ld_coff_capability_strings_deinit(&state->weak_undefined_symbols);
    ld_coff_capability_strings_deinit(&state->allowed_synthetic_symbols);
    ld_coff_capability_strings_deinit(&state->external_contract_symbols);
    ld_coff_capability_strings_deinit(&state->weak_unresolved_symbols);
    ld_coff_capability_strings_deinit(&state->hard_unresolved_symbols);
    ld_coff_capability_strings_deinit(&state->system_dlls);
    ld_coff_capability_strings_deinit(&state->refptr_symbols);
    memset(state, 0, sizeof(*state));
}

static int ld_coff_capability_compare_string(const void *left,
                                             const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static int ld_coff_capability_compare_u16(const void *left,
                                          const void *right) {
    uint16_t a = *(const uint16_t *) left;
    uint16_t b = *(const uint16_t *) right;
    return a < b ? -1 : a > b;
}

static int ld_coff_capability_compare_u8(const void *left,
                                         const void *right) {
    uint8_t a = *(const uint8_t *) left;
    uint8_t b = *(const uint8_t *) right;
    return a < b ? -1 : a > b;
}

static int ld_coff_capability_compare_input(const void *left,
                                            const void *right) {
    const ld_coff_capability_input_t *a = left;
    const ld_coff_capability_input_t *b = right;
    int order = strcmp(a->path, b->path);
    return order ? order : strcmp(a->type, b->type);
}

static int ld_coff_capability_compare_object(const void *left,
                                             const void *right) {
    const ld_coff_capability_object_t *a = left;
    const ld_coff_capability_object_t *b = right;
    int order = strcmp(a->name, b->name);
    if (order) return order;
    order = strcmp(a->container, b->container);
    return order ? order : strcmp(a->type, b->type);
}

static int ld_coff_capability_compare_import(const void *left,
                                             const void *right) {
    const ld_coff_capability_import_t *a = left;
    const ld_coff_capability_import_t *b = right;
    int order = ld_coff_capability_ascii_compare(a->dll, b->dll);
    if (order) return order;
    order = strcmp(a->symbol, b->symbol);
    if (order) return order;
    order = strcmp(a->import_name, b->import_name);
    if (order) return order;
    order = strcmp(a->object_name, b->object_name);
    if (order) return order;
    if (a->type != b->type) return a->type < b->type ? -1 : 1;
    if (a->name_type != b->name_type)
        return a->name_type < b->name_type ? -1 : 1;
    if (a->hint != b->hint) return a->hint < b->hint ? -1 : 1;
    return 0;
}

static void ld_coff_capability_sort(ld_coff_capability_state_t *state) {
    if (state->input_count > 1U)
        qsort(state->inputs, state->input_count, sizeof(*state->inputs),
              ld_coff_capability_compare_input);
    if (state->object_count > 1U)
        qsort(state->objects, state->object_count, sizeof(*state->objects),
              ld_coff_capability_compare_object);
    if (state->import_count > 1U)
        qsort(state->imports, state->import_count, sizeof(*state->imports),
              ld_coff_capability_compare_import);
    if (state->relocations.count > 1U)
        qsort(state->relocations.items, state->relocations.count,
              sizeof(*state->relocations.items),
              ld_coff_capability_compare_u16);
    if (state->comdats.count > 1U)
        qsort(state->comdats.items, state->comdats.count,
              sizeof(*state->comdats.items),
              ld_coff_capability_compare_u8);
    ld_coff_capability_strings_t *sets[] = {
            &state->directives,
            &state->undefined_symbols,
            &state->defined_symbols,
            &state->weak_undefined_symbols,
            &state->allowed_synthetic_symbols,
            &state->external_contract_symbols,
            &state->weak_unresolved_symbols,
            &state->hard_unresolved_symbols,
            &state->system_dlls,
            &state->refptr_symbols,
    };
    for (size_t i = 0U; i < sizeof(sets) / sizeof(*sets); i++)
        if (sets[i]->count > 1U)
            qsort(sets[i]->items, sets[i]->count,
                  sizeof(*sets[i]->items),
                  ld_coff_capability_compare_string);
}

static bool ld_coff_capability_builder_reserve(
        ld_coff_capability_builder_t *builder, size_t additional) {
    if (additional > SIZE_MAX - builder->size - 1U) return false;
    size_t required = builder->size + additional + 1U;
    if (required <= builder->capacity) return true;
    size_t next = builder->capacity ? builder->capacity : 1024U;
    while (next < required) {
        if (next > SIZE_MAX / 2U) {
            next = required;
            break;
        }
        next *= 2U;
    }
    char *grown = realloc(builder->data, next);
    if (!grown) return false;
    builder->data = grown;
    builder->capacity = next;
    return true;
}

static bool ld_coff_capability_builder_append(
        ld_coff_capability_builder_t *builder, const char *value,
        size_t length) {
    if (!ld_coff_capability_builder_reserve(builder, length)) return false;
    memcpy(builder->data + builder->size, value, length);
    builder->size += length;
    builder->data[builder->size] = '\0';
    return true;
}

static bool ld_coff_capability_builder_text(
        ld_coff_capability_builder_t *builder, const char *value) {
    return ld_coff_capability_builder_append(builder, value, strlen(value));
}

static bool ld_coff_capability_builder_format(
        ld_coff_capability_builder_t *builder, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int required = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (required < 0 ||
        !ld_coff_capability_builder_reserve(builder, (size_t) required)) {
        va_end(arguments);
        return false;
    }
    int written = vsnprintf(builder->data + builder->size,
                            builder->capacity - builder->size, format,
                            arguments);
    va_end(arguments);
    if (written != required) return false;
    builder->size += (size_t) written;
    return true;
}

static bool ld_coff_capability_builder_json_string(
        ld_coff_capability_builder_t *builder, const char *value) {
    static const char hex[] = "0123456789abcdef";
    if (!ld_coff_capability_builder_text(builder, "\"")) return false;
    for (const unsigned char *cursor = (const unsigned char *) value; *cursor;
         cursor++) {
        char escaped[6];
        const char *short_escape = NULL;
        switch (*cursor) {
            case '"':
                short_escape = "\\\"";
                break;
            case '\\':
                short_escape = "\\\\";
                break;
            case '\b':
                short_escape = "\\b";
                break;
            case '\f':
                short_escape = "\\f";
                break;
            case '\n':
                short_escape = "\\n";
                break;
            case '\r':
                short_escape = "\\r";
                break;
            case '\t':
                short_escape = "\\t";
                break;
            default:
                break;
        }
        if (short_escape) {
            if (!ld_coff_capability_builder_text(builder, short_escape))
                return false;
        } else if (*cursor < 0x20U || *cursor >= 0x7fU) {
            escaped[0] = '\\';
            escaped[1] = 'u';
            escaped[2] = '0';
            escaped[3] = '0';
            escaped[4] = hex[*cursor >> 4U];
            escaped[5] = hex[*cursor & 0x0fU];
            if (!ld_coff_capability_builder_append(builder, escaped,
                                                   sizeof(escaped)))
                return false;
        } else {
            char character = (char) *cursor;
            if (!ld_coff_capability_builder_append(builder, &character, 1U))
                return false;
        }
    }
    return ld_coff_capability_builder_text(builder, "\"");
}

static bool ld_coff_capability_render_strings(
        ld_coff_capability_builder_t *builder,
        const ld_coff_capability_strings_t *strings) {
    if (!ld_coff_capability_builder_text(builder, "[")) return false;
    for (size_t i = 0U; i < strings->count; i++) {
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        if (!ld_coff_capability_builder_json_string(builder,
                                                    strings->items[i]))
            return false;
    }
    return ld_coff_capability_builder_text(builder, "]");
}

static bool ld_coff_capability_render(
        const ld_coff_capability_state_t *state,
        ld_coff_capability_builder_t *builder) {
    if (!ld_coff_capability_builder_text(builder, "{\n  \"schema\":"))
        return false;
    if (!ld_coff_capability_builder_json_string(builder,
                                                LD_COFF_CAPABILITY_SCHEMA) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"target\":\"windows/amd64\",\n  "
                         "\"llvm_reference_commit\":"))
        return false;
    if (!ld_coff_capability_builder_json_string(
                builder, LD_COFF_CAPABILITY_LLVM_COMMIT) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"bitcode_lto\":\"absent\",\n  "
                         "\"input_headers\":["))
        return false;
    for (size_t i = 0U; i < state->input_count; i++) {
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        if (!ld_coff_capability_builder_text(builder, "{\"path\":"))
            return false;
        if (!ld_coff_capability_builder_json_string(builder,
                                                    state->inputs[i].path) ||
            !ld_coff_capability_builder_text(builder, ",\"type\":") ||
            !ld_coff_capability_builder_json_string(builder,
                                                    state->inputs[i].type) ||
            !ld_coff_capability_builder_text(builder, "}"))
            return false;
    }
    if (!ld_coff_capability_builder_text(builder, "],\n  \"objects\":["))
        return false;
    for (size_t i = 0U; i < state->object_count; i++) {
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        const ld_coff_capability_object_t *object = &state->objects[i];
        if (!ld_coff_capability_builder_text(builder, "{\"name\":") ||
            !ld_coff_capability_builder_json_string(builder, object->name) ||
            !ld_coff_capability_builder_text(builder, ",\"container\":") ||
            !ld_coff_capability_builder_json_string(builder,
                                                    object->container) ||
            !ld_coff_capability_builder_text(builder, ",\"type\":") ||
            !ld_coff_capability_builder_json_string(builder, object->type) ||
            !ld_coff_capability_builder_text(builder, "}"))
            return false;
    }
    if (!ld_coff_capability_builder_text(
                builder, "],\n  \"amd64_relocations\":["))
        return false;
    for (size_t i = 0U; i < state->relocations.count; i++) {
        uint16_t type = state->relocations.items[i];
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        if (!ld_coff_capability_builder_text(builder, "{\"name\":") ||
            !ld_coff_capability_builder_json_string(
                    builder, ld_coff_capability_relocation_name(type)) ||
            !ld_coff_capability_builder_format(builder, ",\"value\":%u}",
                                               type))
            return false;
    }
    if (!ld_coff_capability_builder_text(
                builder, "],\n  \"comdat_selections\":["))
        return false;
    for (size_t i = 0U; i < state->comdats.count; i++) {
        uint8_t selection = state->comdats.items[i];
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        if (!ld_coff_capability_builder_text(builder, "{\"name\":") ||
            !ld_coff_capability_builder_json_string(
                    builder, ld_coff_capability_comdat_name(selection)) ||
            !ld_coff_capability_builder_format(builder, ",\"value\":%u}",
                                               selection))
            return false;
    }
    if (!ld_coff_capability_builder_text(builder,
                                         "],\n  \"drectve_tokens\":"))
        return false;
    if (!ld_coff_capability_render_strings(builder, &state->directives) ||
        !ld_coff_capability_builder_text(builder, ",\n  \"imports\":["))
        return false;
    for (size_t i = 0U; i < state->import_count; i++) {
        const ld_coff_capability_import_t *import = &state->imports[i];
        if (i && !ld_coff_capability_builder_text(builder, ",")) return false;
        if (!ld_coff_capability_builder_text(builder, "{\"object\":") ||
            !ld_coff_capability_builder_json_string(builder,
                                                    import->object_name) ||
            !ld_coff_capability_builder_text(builder, ",\"dll\":") ||
            !ld_coff_capability_builder_json_string(builder, import->dll) ||
            !ld_coff_capability_builder_text(builder, ",\"symbol\":") ||
            !ld_coff_capability_builder_json_string(builder,
                                                    import->symbol) ||
            !ld_coff_capability_builder_text(builder, ",\"import_name\":") ||
            !ld_coff_capability_builder_json_string(builder,
                                                    import->import_name) ||
            !ld_coff_capability_builder_text(builder, ",\"type\":") ||
            !ld_coff_capability_builder_json_string(
                    builder,
                    ld_coff_capability_import_type_name(import->type)) ||
            !ld_coff_capability_builder_text(builder, ",\"name_type\":") ||
            !ld_coff_capability_builder_json_string(
                    builder, ld_coff_capability_import_name_type_name(
                                     import->name_type)) ||
            !ld_coff_capability_builder_format(
                    builder, ",\"version\":%u,\"hint\":%u}", import->version,
                    import->hint))
            return false;
    }
    if (!ld_coff_capability_builder_text(
                builder, "],\n  \"undefined_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(builder,
                                           &state->undefined_symbols) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"allowed_synthetic_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(
                builder, &state->allowed_synthetic_symbols) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"external_contract_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(
                builder, &state->external_contract_symbols) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"weak_unresolved_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(
                builder, &state->weak_unresolved_symbols) ||
        !ld_coff_capability_builder_text(
                builder, ",\n  \"hard_unresolved_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(
                builder, &state->hard_unresolved_symbols) ||
        !ld_coff_capability_builder_text(builder, ",\n  \"system_dlls\":"))
        return false;
    if (!ld_coff_capability_render_strings(builder, &state->system_dlls) ||
        !ld_coff_capability_builder_text(builder, ",\n  \"refptr_symbols\":"))
        return false;
    if (!ld_coff_capability_render_strings(builder, &state->refptr_symbols) ||
        !ld_coff_capability_builder_format(
                builder,
                ",\n  \"pseudo_reloc_support\":\"empty-only\",\n  "
                "\"pseudo_reloc_v2_marker\":%s,\n  "
                "\"pseudo_reloc_runtime_symbols\":%s\n}\n",
                state->pseudo_reloc_v2_marker ? "true" : "false",
                state->pseudo_reloc_runtime_symbols ? "true" : "false"))
        return false;
    return true;
}

int ld_coff_capability_scan_json(
        const char *const *input_paths, size_t input_count,
        ld_coff_capability_manifest_t *manifest, char *error,
        size_t error_capacity) {
    if (manifest) memset(manifest, 0, sizeof(*manifest));
    if (error && error_capacity != 0U) error[0] = '\0';
    if (!manifest || !input_paths || input_count == 0U)
        return LD_INVALID_ARGUMENT;
    ld_coff_capability_state_t state = {
            .error = error,
            .error_capacity = error_capacity,
    };
    int status = LD_OK;
    for (size_t i = 0U; i < input_count; i++) {
        if (!input_paths[i] || !*input_paths[i]) {
            status = ld_coff_capability_fail(
                    &state, LD_INVALID_ARGUMENT,
                    "COFF capability input path %zu is empty", i);
            break;
        }
        status = ld_coff_capability_scan_path(&state, input_paths[i]);
        if (status != LD_OK) break;
    }
    if (status == LD_OK)
        status = ld_coff_capability_validate_symbol_closure(&state);
    ld_coff_capability_builder_t builder = {0};
    if (status == LD_OK) {
        ld_coff_capability_sort(&state);
        if (!ld_coff_capability_render(&state, &builder))
            status = ld_coff_capability_fail(&state, LD_IO_ERROR,
                                             "out of memory rendering COFF "
                                             "capability manifest");
    }
    if (status == LD_OK) {
        manifest->json = builder.data;
        manifest->size = builder.size;
        builder.data = NULL;
    }
    free(builder.data);
    ld_coff_capability_state_deinit(&state);
    return status;
}

void ld_coff_capability_manifest_deinit(
        ld_coff_capability_manifest_t *manifest) {
    if (!manifest) return;
    free(manifest->json);
    memset(manifest, 0, sizeof(*manifest));
}

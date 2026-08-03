#include "ld_tapi.h"

#include "utils/uthash.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>

#define LD_TAPI_DEFAULT_DYLIB_VERSION 0x00010000U
#define LD_TAPI_MAX_SYMBOL_LENGTH 4096U
#define LD_TAPI_MAX_INSTALL_NAME_LENGTH 4096U

typedef struct ld_tapi_symbol_index {
    const char *name;
    size_t index;
    UT_hash_handle hh;
} ld_tapi_symbol_index_t;

typedef struct {
    ld_tapi_stub_t *stub;
    ld_tapi_error_t *error;
    ld_tapi_symbol_index_t *symbol_index;
} ld_tapi_parse_state_t;

typedef enum {
    LD_TAPI_OBJC_CLASS,
    LD_TAPI_OBJC_IVAR,
    LD_TAPI_OBJC_EH_TYPE,
} ld_tapi_objc_kind_t;

static int ld_tapi_error_at(ld_tapi_error_t *error, int code,
                            const yaml_node_t *node, const char *format, ...) {
    if (error && error->message[0] == '\0') {
        if (node) {
            error->line = node->start_mark.line + 1U;
            error->column = node->start_mark.column + 1U;
        }
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
    }
    return code;
}

static bool ld_tapi_scalar_equals(const yaml_node_t *node, const char *value) {
    if (!node || node->type != YAML_SCALAR_NODE) return false;
    size_t length = strlen(value);
    return node->data.scalar.length == length &&
           memcmp(node->data.scalar.value, value, length) == 0;
}

static int ld_tapi_require_scalar(ld_tapi_parse_state_t *state,
                                  const yaml_node_t *node, const char *field,
                                  const char **value, size_t *length) {
    if (!node || node->type != YAML_SCALAR_NODE ||
        node->data.scalar.length == 0U ||
        memchr(node->data.scalar.value, '\0', node->data.scalar.length)) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                "%s must be a non-empty scalar", field);
    }
    *value = (const char *) node->data.scalar.value;
    *length = node->data.scalar.length;
    return LD_OK;
}

static int ld_tapi_map_get(ld_tapi_parse_state_t *state,
                           yaml_document_t *document, yaml_node_t *mapping,
                           const char *key, bool required,
                           yaml_node_t **result) {
    *result = NULL;
    if (!mapping || mapping->type != YAML_MAPPING_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, mapping,
                                "TAPI object containing '%s' must be a mapping", key);
    }
    for (yaml_node_pair_t *pair = mapping->data.mapping.pairs.start;
         pair < mapping->data.mapping.pairs.top; pair++) {
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        yaml_node_t *value_node = yaml_document_get_node(document, pair->value);
        if (!key_node || key_node->type != YAML_SCALAR_NODE || !value_node) {
            return ld_tapi_error_at(state->error, LD_INVALID_INPUT, key_node,
                                    "TAPI mapping keys must be scalars");
        }
        if (!ld_tapi_scalar_equals(key_node, key)) continue;
        if (*result) {
            return ld_tapi_error_at(state->error, LD_INVALID_INPUT, key_node,
                                    "duplicate TAPI key '%s'", key);
        }
        *result = value_node;
    }
    if (required && !*result) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, mapping,
                                "TAPI mapping is missing required key '%s'", key);
    }
    return LD_OK;
}

static int ld_tapi_sequence_contains(ld_tapi_parse_state_t *state,
                                     yaml_document_t *document,
                                     yaml_node_t *sequence,
                                     const char *field,
                                     const char *const *accepted,
                                     size_t accepted_count, bool *matched) {
    *matched = false;
    if (!sequence || sequence->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, sequence,
                                "invalid %s list: expected a sequence", field);
    }
    for (yaml_node_item_t *item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        const char *value;
        size_t length;
        int result = ld_tapi_require_scalar(state, node, field, &value, &length);
        if (result != LD_OK) return result;
        for (size_t i = 0; i < accepted_count; i++) {
            size_t accepted_length = strlen(accepted[i]);
            if (length == accepted_length &&
                memcmp(value, accepted[i], length) == 0) {
                *matched = true;
            }
        }
    }
    return LD_OK;
}

static void *ld_tapi_grow_array(void *items, size_t *capacity,
                                size_t count, size_t element_size) {
    if (count < *capacity) return items;
    if (*capacity > SIZE_MAX / 2U) return NULL;
    size_t next = *capacity ? *capacity * 2U : 32U;
    if (next <= count) {
        if (count == SIZE_MAX) return NULL;
        next = count + 1U;
    }
    if (next > SIZE_MAX / element_size) return NULL;
    void *grown = realloc(items, next * element_size);
    if (!grown) return NULL;
    *capacity = next;
    return grown;
}

static char *ld_tapi_string_copy(const char *value, size_t length) {
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static int ld_tapi_symbol_push(ld_tapi_parse_state_t *state,
                               const yaml_node_t *node, const char *name,
                               size_t name_length, ld_tapi_symbol_kind_t kind,
                               bool weak, bool reexport,
                               const char *imported_name,
                               size_t imported_name_length) {
    if (name_length == 0U || name_length > LD_TAPI_MAX_SYMBOL_LENGTH ||
        memchr(name, '\0', name_length)) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                "TAPI export has an invalid symbol name");
    }
    ld_tapi_symbol_index_t *entry = NULL;
    HASH_FIND(hh, state->symbol_index, name, name_length, entry);
    if (entry) {
        ld_tapi_symbol_t *symbol = &state->stub->symbols[entry->index];
        if (symbol->kind != kind) {
            if (symbol->kind == LD_TAPI_SYMBOL_REGULAR) {
                symbol->kind = kind;
            } else if (kind != LD_TAPI_SYMBOL_REGULAR) {
                return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                        "TAPI symbol '%s' has conflicting kinds",
                                        symbol->name);
            }
        }
        /* A regular definition is stronger than a weak duplicate. */
        symbol->weak = symbol->weak && weak;
        symbol->reexport = symbol->reexport || reexport;
        if (imported_name) {
            if (symbol->imported_name &&
                (strlen(symbol->imported_name) != imported_name_length ||
                 memcmp(symbol->imported_name, imported_name,
                        imported_name_length) != 0)) {
                return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                        "TAPI symbol '%s' has conflicting imported names",
                                        symbol->name);
            }
            if (!symbol->imported_name) {
                symbol->imported_name =
                        ld_tapi_string_copy(imported_name, imported_name_length);
                if (!symbol->imported_name) return LD_IO_ERROR;
            }
        }
        return LD_OK;
    }

    ld_tapi_stub_t *stub = state->stub;
    ld_tapi_symbol_t *symbols = ld_tapi_grow_array(
            stub->symbols, &stub->symbol_capacity, stub->symbol_count,
            sizeof(*symbols));
    if (!symbols) return LD_IO_ERROR;
    stub->symbols = symbols;

    char *name_copy = ld_tapi_string_copy(name, name_length);
    char *imported_copy = NULL;
    if (imported_name) {
        imported_copy = ld_tapi_string_copy(imported_name, imported_name_length);
    }
    if (!name_copy || (imported_name && !imported_copy)) {
        free(name_copy);
        free(imported_copy);
        return LD_IO_ERROR;
    }

    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        free(name_copy);
        free(imported_copy);
        return LD_IO_ERROR;
    }
    size_t index = stub->symbol_count++;
    stub->symbols[index] = (ld_tapi_symbol_t) {
            .name = name_copy,
            .imported_name = imported_copy,
            .kind = kind,
            .weak = weak,
            .reexport = reexport,
    };
    entry->name = name_copy;
    entry->index = index;
    HASH_ADD_KEYPTR(hh, state->symbol_index, entry->name, name_length, entry);
    return LD_OK;
}

static int ld_tapi_reexport_push(ld_tapi_parse_state_t *state,
                                 const yaml_node_t *node, const char *name,
                                 size_t length) {
    if (length == 0U || length > LD_TAPI_MAX_INSTALL_NAME_LENGTH ||
        memchr(name, '\0', length)) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                "TAPI re-export has an invalid install-name");
    }
    for (size_t i = 0; i < state->stub->reexport_count; i++) {
        if (strlen(state->stub->reexports[i]) == length &&
            memcmp(state->stub->reexports[i], name, length) == 0) {
            return LD_OK;
        }
    }
    ld_tapi_stub_t *stub = state->stub;
    char **items = ld_tapi_grow_array(stub->reexports,
                                      &stub->reexport_capacity,
                                      stub->reexport_count, sizeof(*items));
    if (!items) return LD_IO_ERROR;
    stub->reexports = items;
    char *copy = ld_tapi_string_copy(name, length);
    if (!copy) return LD_IO_ERROR;
    stub->reexports[stub->reexport_count++] = copy;
    return LD_OK;
}

static int ld_tapi_parse_version(ld_tapi_parse_state_t *state,
                                 const yaml_node_t *node, const char *field,
                                 uint32_t *version) {
    const char *value;
    size_t length;
    int result = ld_tapi_require_scalar(state, node, field, &value, &length);
    if (result != LD_OK) return result;

    uint32_t components[3] = {0};
    size_t cursor = 0;
    for (size_t component = 0; component < 3U; component++) {
        if (cursor == length || value[cursor] < '0' || value[cursor] > '9') {
            return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                    "invalid %s", field);
        }
        uint32_t limit = component == 0U ? UINT16_MAX : UINT8_MAX;
        uint32_t parsed = 0;
        while (cursor < length && value[cursor] >= '0' && value[cursor] <= '9') {
            uint32_t digit = (uint32_t) (value[cursor] - '0');
            if (parsed > (limit - digit) / 10U) {
                return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                        "%s component overflows", field);
            }
            parsed = parsed * 10U + digit;
            cursor++;
        }
        components[component] = parsed;
        if (cursor == length) break;
        if (value[cursor] != '.' || component == 2U) {
            return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                    "invalid %s", field);
        }
        cursor++;
    }
    if (cursor != length) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                "invalid %s", field);
    }
    *version = (components[0] << 16U) | (components[1] << 8U) | components[2];
    return LD_OK;
}

static int ld_tapi_symbol_sequence(ld_tapi_parse_state_t *state,
                                   yaml_document_t *document,
                                   yaml_node_t *mapping, const char *key,
                                   ld_tapi_symbol_kind_t kind, bool weak,
                                   bool reexport, bool enabled) {
    yaml_node_t *sequence;
    int result = ld_tapi_map_get(state, document, mapping, key, false, &sequence);
    if (result != LD_OK || !sequence) return result;
    if (sequence->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, sequence,
                                "invalid %s list: expected a sequence", key);
    }
    for (yaml_node_item_t *item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        const char *name;
        size_t name_length;
        result = ld_tapi_require_scalar(state, node, key, &name, &name_length);
        if (result != LD_OK) return result;
        if (enabled) {
            result = ld_tapi_symbol_push(state, node, name, name_length, kind,
                                         weak, reexport, NULL, 0U);
            if (result != LD_OK) return result;
        }
    }
    return LD_OK;
}

static int ld_tapi_prefixed_symbol(ld_tapi_parse_state_t *state,
                                   const yaml_node_t *node,
                                   const char *prefix, const char *name,
                                   size_t name_length, bool weak,
                                   bool reexport) {
    size_t prefix_length = strlen(prefix);
    if (name_length > LD_TAPI_MAX_SYMBOL_LENGTH - prefix_length) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, node,
                                "Objective-C export name is too long");
    }
    size_t length = prefix_length + name_length;
    char *symbol = malloc(length + 1U);
    if (!symbol) return LD_IO_ERROR;
    memcpy(symbol, prefix, prefix_length);
    memcpy(symbol + prefix_length, name, name_length);
    symbol[length] = '\0';
    int result = ld_tapi_symbol_push(state, node, symbol, length,
                                     LD_TAPI_SYMBOL_REGULAR, weak, reexport,
                                     NULL, 0U);
    free(symbol);
    return result;
}

static int ld_tapi_objc_sequence(ld_tapi_parse_state_t *state,
                                 yaml_document_t *document,
                                 yaml_node_t *mapping, const char *key,
                                 ld_tapi_objc_kind_t kind, bool weak,
                                 bool reexport, bool enabled) {
    yaml_node_t *sequence;
    int result = ld_tapi_map_get(state, document, mapping, key, false, &sequence);
    if (result != LD_OK || !sequence) return result;
    if (sequence->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, sequence,
                                "invalid %s list: expected a sequence", key);
    }
    for (yaml_node_item_t *item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        const char *name;
        size_t name_length;
        result = ld_tapi_require_scalar(state, node, key, &name, &name_length);
        if (result != LD_OK) return result;
        if (!enabled) continue;
        if (kind == LD_TAPI_OBJC_CLASS) {
            result = ld_tapi_prefixed_symbol(state, node, "_OBJC_CLASS_$_",
                                             name, name_length, weak, reexport);
            if (result == LD_OK) {
                result = ld_tapi_prefixed_symbol(state, node,
                                                 "_OBJC_METACLASS_$_", name,
                                                 name_length, weak, reexport);
            }
        } else {
            const char *prefix = kind == LD_TAPI_OBJC_IVAR
                                         ? "_OBJC_IVAR_$_"
                                         : "_OBJC_EHTYPE_$_";
            result = ld_tapi_prefixed_symbol(state, node, prefix, name,
                                             name_length, weak, reexport);
        }
        if (result != LD_OK) return result;
    }
    return LD_OK;
}

static int ld_tapi_process_symbol_mapping(ld_tapi_parse_state_t *state,
                                          yaml_document_t *document,
                                          yaml_node_t *mapping, bool enabled,
                                          bool reexport) {
    int result = ld_tapi_symbol_sequence(state, document, mapping, "symbols",
                                         LD_TAPI_SYMBOL_REGULAR, false,
                                         reexport, enabled);
    if (result == LD_OK) {
        result = ld_tapi_symbol_sequence(state, document, mapping,
                                         "weak-symbols",
                                         LD_TAPI_SYMBOL_REGULAR, true,
                                         reexport, enabled);
    }
    if (result == LD_OK) {
        result = ld_tapi_symbol_sequence(state, document, mapping,
                                         "weak-def-symbols",
                                         LD_TAPI_SYMBOL_REGULAR, true,
                                         reexport, enabled);
    }
    if (result == LD_OK) {
        result = ld_tapi_symbol_sequence(state, document, mapping,
                                         "absolute-symbols",
                                         LD_TAPI_SYMBOL_ABSOLUTE, false,
                                         reexport, enabled);
    }
    if (result == LD_OK) {
        result = ld_tapi_symbol_sequence(state, document, mapping,
                                         "thread-local-symbols",
                                         LD_TAPI_SYMBOL_TLV, false, reexport,
                                         enabled);
    }
    static const struct {
        const char *key;
        ld_tapi_objc_kind_t kind;
        bool weak;
    } objc_fields[] = {
            {"objc-classes", LD_TAPI_OBJC_CLASS, false},
            {"weak-objc-classes", LD_TAPI_OBJC_CLASS, true},
            {"objc-ivars", LD_TAPI_OBJC_IVAR, false},
            {"weak-objc-ivars", LD_TAPI_OBJC_IVAR, true},
            {"objc-eh-types", LD_TAPI_OBJC_EH_TYPE, false},
            {"weak-objc-eh-types", LD_TAPI_OBJC_EH_TYPE, true},
    };
    for (size_t i = 0;
         result == LD_OK && i < sizeof(objc_fields) / sizeof(objc_fields[0]);
         i++) {
        result = ld_tapi_objc_sequence(
                state, document, mapping, objc_fields[i].key,
                objc_fields[i].kind, objc_fields[i].weak, reexport, enabled);
    }
    return result;
}

static int ld_tapi_process_library_sequence(ld_tapi_parse_state_t *state,
                                            yaml_document_t *document,
                                            yaml_node_t *mapping,
                                            const char *key, bool enabled) {
    yaml_node_t *sequence;
    int result = ld_tapi_map_get(state, document, mapping, key, false, &sequence);
    if (result != LD_OK || !sequence) return result;
    if (sequence->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, sequence,
                                "invalid %s list: expected a sequence", key);
    }
    for (yaml_node_item_t *item = sequence->data.sequence.items.start;
         item < sequence->data.sequence.items.top; item++) {
        yaml_node_t *node = yaml_document_get_node(document, *item);
        const char *name;
        size_t name_length;
        result = ld_tapi_require_scalar(state, node, key, &name, &name_length);
        if (result != LD_OK) return result;
        if (enabled) {
            result = ld_tapi_reexport_push(state, node, name, name_length);
            if (result != LD_OK) return result;
        }
    }
    return LD_OK;
}

static int ld_tapi_block_applies(ld_tapi_parse_state_t *state,
                                 yaml_document_t *document,
                                 yaml_node_t *mapping, unsigned version,
                                 bool document_applies, bool *applies) {
    const char *key = version == 3U ? "archs" : "targets";
    yaml_node_t *targets;
    int result = ld_tapi_map_get(state, document, mapping, key, true, &targets);
    if (result != LD_OK) return result;
    static const char *const v3_archs[] = {"arm64"};
    static const char *const v4_targets[] = {
            "arm64-macos",
            "arm64e-macos",
            "arm64-macosx",
    };
    bool block_matches = false;
    result = ld_tapi_sequence_contains(
            state, document, targets, key,
            version == 3U ? v3_archs : v4_targets,
            version == 3U ? sizeof(v3_archs) / sizeof(v3_archs[0])
                          : sizeof(v4_targets) / sizeof(v4_targets[0]),
            &block_matches);
    if (result == LD_OK) *applies = document_applies && block_matches;
    return result;
}

static int ld_tapi_document_version(ld_tapi_parse_state_t *state,
                                    yaml_document_t *document,
                                    yaml_node_t *root, unsigned *version) {
    yaml_node_t *version_node;
    int result = ld_tapi_map_get(state, document, root, "tbd-version", false,
                                 &version_node);
    if (result != LD_OK) return result;
    unsigned tag_version = 0;
    if (root->tag && strstr((const char *) root->tag, "tapi-tbd-v3")) {
        tag_version = 3U;
    } else if (root->tag && strstr((const char *) root->tag, "tapi-tbd-v2")) {
        tag_version = 2U;
    }
    unsigned field_version = 0;
    if (version_node) {
        const char *value;
        size_t length;
        result = ld_tapi_require_scalar(state, version_node, "tbd-version",
                                        &value, &length);
        if (result != LD_OK) return result;
        if (length == 1U && (value[0] == '3' || value[0] == '4')) {
            field_version = (unsigned) (value[0] - '0');
        } else {
            return ld_tapi_error_at(state->error, LD_UNSUPPORTED, version_node,
                                    "unsupported TAPI tbd-version");
        }
    }
    if (tag_version && field_version && tag_version != field_version) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, version_node,
                                "TAPI tag and tbd-version disagree");
    }
    *version = field_version ? field_version : tag_version;
    if (*version != 3U && *version != 4U) {
        return ld_tapi_error_at(state->error, LD_UNSUPPORTED, root,
                                "only TAPI v3 and v4 stubs are supported");
    }
    return LD_OK;
}

static int ld_tapi_document_applies(ld_tapi_parse_state_t *state,
                                    yaml_document_t *document,
                                    yaml_node_t *root, unsigned version,
                                    bool *applies) {
    if (version == 4U) {
        yaml_node_t *targets;
        int result = ld_tapi_map_get(state, document, root, "targets", true,
                                     &targets);
        if (result != LD_OK) return result;
        static const char *const accepted[] = {
                "arm64-macos",
                "arm64e-macos",
                "arm64-macosx",
        };
        return ld_tapi_sequence_contains(
                state, document, targets, "targets", accepted,
                sizeof(accepted) / sizeof(accepted[0]), applies);
    }

    yaml_node_t *archs;
    yaml_node_t *platform;
    int result = ld_tapi_map_get(state, document, root, "archs", true, &archs);
    if (result == LD_OK) {
        result = ld_tapi_map_get(state, document, root, "platform", true,
                                 &platform);
    }
    if (result != LD_OK) return result;
    static const char *const accepted_archs[] = {"arm64"};
    bool arch_matches = false;
    result = ld_tapi_sequence_contains(
            state, document, archs, "archs", accepted_archs,
            sizeof(accepted_archs) / sizeof(accepted_archs[0]), &arch_matches);
    if (result != LD_OK) return result;
    const char *platform_name;
    size_t platform_length;
    result = ld_tapi_require_scalar(state, platform, "platform",
                                    &platform_name, &platform_length);
    if (result != LD_OK) return result;
    bool platform_matches =
            (platform_length == 5U && memcmp(platform_name, "macos", 5U) == 0) ||
            (platform_length == 6U && memcmp(platform_name, "macosx", 6U) == 0) ||
            (platform_length == 8U && memcmp(platform_name, "zippered", 8U) == 0);
    *applies = arch_matches && platform_matches;
    return LD_OK;
}

static int ld_tapi_process_export_blocks(ld_tapi_parse_state_t *state,
                                         yaml_document_t *document,
                                         yaml_node_t *root, unsigned version,
                                         bool document_applies,
                                         const char *key, bool reexport) {
    yaml_node_t *blocks;
    int result = ld_tapi_map_get(state, document, root, key, false, &blocks);
    if (result != LD_OK || !blocks) return result;
    if (blocks->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, blocks,
                                "%s must be a sequence", key);
    }
    for (yaml_node_item_t *item = blocks->data.sequence.items.start;
         item < blocks->data.sequence.items.top; item++) {
        yaml_node_t *block = yaml_document_get_node(document, *item);
        if (!block || block->type != YAML_MAPPING_NODE) {
            return ld_tapi_error_at(state->error, LD_INVALID_INPUT, block,
                                    "%s entries must be mappings", key);
        }
        bool applies = false;
        result = ld_tapi_block_applies(state, document, block, version,
                                       document_applies, &applies);
        if (result == LD_OK) {
            result = ld_tapi_process_symbol_mapping(state, document, block,
                                                    applies, reexport);
        }
        if (result != LD_OK) return result;
        if (version == 3U) {
            result = ld_tapi_process_library_sequence(
                    state, document, block, "re-exports", applies);
            if (result == LD_OK) {
                result = ld_tapi_process_library_sequence(
                        state, document, block, "reexports", applies);
            }
            if (result != LD_OK) return result;
        }
    }
    return LD_OK;
}

static int ld_tapi_process_reexported_libraries(
        ld_tapi_parse_state_t *state, yaml_document_t *document,
        yaml_node_t *root, bool document_applies) {
    yaml_node_t *blocks;
    int result = ld_tapi_map_get(state, document, root,
                                 "reexported-libraries", false, &blocks);
    if (result != LD_OK || !blocks) return result;
    if (blocks->type != YAML_SEQUENCE_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, blocks,
                                "reexported-libraries must be a sequence");
    }
    for (yaml_node_item_t *item = blocks->data.sequence.items.start;
         item < blocks->data.sequence.items.top; item++) {
        yaml_node_t *block = yaml_document_get_node(document, *item);
        if (block && block->type == YAML_SCALAR_NODE) {
            const char *name;
            size_t length;
            result = ld_tapi_require_scalar(state, block,
                                            "reexported-libraries", &name,
                                            &length);
            if (result == LD_OK && document_applies) {
                result = ld_tapi_reexport_push(state, block, name, length);
            }
        } else if (block && block->type == YAML_MAPPING_NODE) {
            bool applies = false;
            result = ld_tapi_block_applies(state, document, block, 4U,
                                           document_applies, &applies);
            if (result == LD_OK) {
                result = ld_tapi_process_library_sequence(
                        state, document, block, "libraries", applies);
            }
        } else {
            result = ld_tapi_error_at(
                    state->error, LD_INVALID_INPUT, block,
                    "reexported-libraries entries must be mappings or scalars");
        }
        if (result != LD_OK) return result;
    }
    return LD_OK;
}

static int ld_tapi_process_document(ld_tapi_parse_state_t *state,
                                    yaml_document_t *document,
                                    yaml_node_t *root, size_t document_index) {
    if (!root || root->type != YAML_MAPPING_NODE) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, root,
                                "TAPI document root must be a mapping");
    }
    unsigned version;
    int result = ld_tapi_document_version(state, document, root, &version);
    if (result != LD_OK) return result;

    yaml_node_t *install_name;
    result = ld_tapi_map_get(state, document, root, "install-name", true,
                             &install_name);
    if (result != LD_OK) return result;
    const char *install_value;
    size_t install_length;
    result = ld_tapi_require_scalar(state, install_name, "install-name",
                                    &install_value, &install_length);
    if (result != LD_OK) return result;
    if (install_length > LD_TAPI_MAX_INSTALL_NAME_LENGTH) {
        return ld_tapi_error_at(state->error, LD_INVALID_INPUT, install_name,
                                "TAPI install-name is too long");
    }
    if (document_index == 0U) {
        state->stub->install_name =
                ld_tapi_string_copy(install_value, install_length);
        if (!state->stub->install_name) return LD_IO_ERROR;

        yaml_node_t *current_version;
        yaml_node_t *compatibility_version;
        result = ld_tapi_map_get(state, document, root, "current-version",
                                 false, &current_version);
        if (result == LD_OK) {
            result = ld_tapi_map_get(state, document, root,
                                     "compatibility-version", false,
                                     &compatibility_version);
        }
        if (result != LD_OK) return result;
        if (current_version) {
            result = ld_tapi_parse_version(state, current_version,
                                           "current-version",
                                           &state->stub->current_version);
        }
        if (result == LD_OK && compatibility_version) {
            result = ld_tapi_parse_version(
                    state, compatibility_version, "compatibility-version",
                    &state->stub->compatibility_version);
        }
        if (result != LD_OK) return result;
    }

    bool document_applies = false;
    result = ld_tapi_document_applies(state, document, root, version,
                                      &document_applies);
    if (result != LD_OK) return result;
    result = ld_tapi_process_export_blocks(state, document, root, version,
                                           document_applies, "exports", false);
    if (result != LD_OK) return result;
    if (version == 4U) {
        result = ld_tapi_process_export_blocks(state, document, root, version,
                                               document_applies, "reexports",
                                               true);
        if (result == LD_OK) {
            result = ld_tapi_process_reexported_libraries(
                    state, document, root, document_applies);
        }
        if (result == LD_OK) {
            /* TAPI v4 permits Objective-C exports directly on a document in
               addition to the target-filtered export blocks. */
            result = ld_tapi_process_symbol_mapping(state, document, root,
                                                    document_applies, false);
        }
    }
    return result;
}

static void ld_tapi_symbol_index_deinit(ld_tapi_symbol_index_t **index) {
    ld_tapi_symbol_index_t *entry;
    ld_tapi_symbol_index_t *next;
    HASH_ITER(hh, *index, entry, next) {
        HASH_DEL(*index, entry);
        free(entry);
    }
}

void ld_tapi_stub_deinit(ld_tapi_stub_t *stub) {
    if (!stub) return;
    free(stub->install_name);
    for (size_t i = 0; i < stub->symbol_count; i++) {
        free(stub->symbols[i].name);
        free(stub->symbols[i].imported_name);
    }
    free(stub->symbols);
    for (size_t i = 0; i < stub->reexport_count; i++) {
        free(stub->reexports[i]);
    }
    free(stub->reexports);
    memset(stub, 0, sizeof(*stub));
}

int ld_tapi_parse(const uint8_t *bytes, size_t size, ld_tapi_stub_t *stub,
                  ld_tapi_error_t *error) {
    if (!stub || !error || (!bytes && size != 0U)) return LD_INVALID_ARGUMENT;
    memset(stub, 0, sizeof(*stub));
    memset(error, 0, sizeof(*error));
    stub->current_version = LD_TAPI_DEFAULT_DYLIB_VERSION;
    stub->compatibility_version = LD_TAPI_DEFAULT_DYLIB_VERSION;
    if (size == 0U || memchr(bytes, '\0', size)) {
        snprintf(error->message, sizeof(error->message),
                 "text-based stub must be non-empty UTF-8 text");
        return LD_INVALID_INPUT;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        snprintf(error->message, sizeof(error->message),
                 "out of memory initializing YAML parser");
        return LD_IO_ERROR;
    }
    yaml_parser_set_input_string(&parser, bytes, size);
    ld_tapi_parse_state_t state = {
            .stub = stub,
            .error = error,
    };
    int result = LD_OK;
    size_t document_count = 0;
    for (;;) {
        yaml_document_t document;
        if (!yaml_parser_load(&parser, &document)) {
            error->line = parser.problem_mark.line + 1U;
            error->column = parser.problem_mark.column + 1U;
            snprintf(error->message, sizeof(error->message), "%s",
                     parser.problem ? parser.problem : "malformed YAML");
            result = parser.error == YAML_MEMORY_ERROR ? LD_IO_ERROR
                                                       : LD_INVALID_INPUT;
            break;
        }
        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (!root) {
            yaml_document_delete(&document);
            break;
        }
        result = ld_tapi_process_document(&state, &document, root,
                                          document_count++);
        yaml_document_delete(&document);
        if (result != LD_OK) break;
    }
    yaml_parser_delete(&parser);
    ld_tapi_symbol_index_deinit(&state.symbol_index);

    if (result == LD_OK && document_count == 0U) {
        snprintf(error->message, sizeof(error->message),
                 "text-based stub contains no YAML document");
        result = LD_INVALID_INPUT;
    }
    if (result == LD_OK && !stub->install_name) {
        snprintf(error->message, sizeof(error->message),
                 "text-based stub has no install-name");
        result = LD_INVALID_INPUT;
    }
    if (result != LD_OK) {
        if (result == LD_IO_ERROR && error->message[0] == '\0') {
            snprintf(error->message, sizeof(error->message),
                     "out of memory parsing text-based stub");
        }
        ld_tapi_stub_deinit(stub);
    }
    return result;
}

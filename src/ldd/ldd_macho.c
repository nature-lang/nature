#include "ldd_internal.h"
#include "sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LDD_PAGE_SIZE 0x4000ULL
#define LDD_CODE_PAGE_SIZE 0x1000ULL
#define LDD_MAX_PATH 4096

/* Mach-O layout and classic dyld stream construction follow the algorithms
   used by Zig's Mach-O linker at commit 738d2be9d6b6ef3ff3559130c05159ef53336224.
   See README.md and ZIG-LICENSE.txt for provenance and scope. */

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} ldd_bytes_t;

static void ldd_copy_name16(char destination[16], const char *source) {
    size_t length = source ? strnlen(source, 16) : 0;
    memset(destination, 0, 16);
    if (length != 0) {
        memcpy(destination, source, length);
    }
}

static void ldd_copy_input_name(char destination[17], const char source[16]) {
    memcpy(destination, source, 16);
    destination[16] = '\0';
}

static uint64_t ldd_symbol_address(const ldd_symbol_t *symbol);
static uint64_t ldd_local_symbol_address(const ldd_input_symbol_t *input);
static ldd_symbol_t *ldd_relocation_symbol(ldd_context_t *ctx, ldd_object_t *object,
                                           const ldd_relocation_t *relocation);
static uint64_t ldd_relocation_target(ldd_context_t *ctx, ldd_object_t *object,
                                      const ldd_relocation_t *relocation);
static int ldd_symbol_name_compare(const void *left, const void *right);
static bool ldd_relocation_accepts_addend(uint8_t type);
static int ldd_prepare_boundary_symbols(ldd_context_t *ctx);
static int ldd_resolve_boundary_symbols(ldd_context_t *ctx);
static int ldd_resolve_aliases(ldd_context_t *ctx);
static bool ldd_is_branch26(uint32_t instruction);
static int64_t ldd_branch26_implicit_addend(uint32_t instruction);
static bool ldd_is_adrp(uint32_t instruction);
static bool ldd_is_addsub_immediate(uint32_t instruction);
static bool ldd_is_load_store_unsigned(uint32_t instruction);

/* These object-file segments are emitted by newer SDK toolchains (and by
   legacy Objective-C compilers), but the executable layout below deliberately
   uses ld64's canonical four data/text segments.  Keep this mapping in one
   place so section collection and linker-defined boundary symbols agree. */
static const char *ldd_canonical_segment(const char *name) {
    if (strcmp(name, "__TEXT_EXEC") == 0) return "__TEXT";
    if (strcmp(name, "__AUTH_CONST") == 0 || strcmp(name, "__OBJC_RO") == 0) {
        return "__DATA_CONST";
    }
    if (strcmp(name, "__AUTH") == 0 || strcmp(name, "__DATA_DIRTY") == 0 ||
        strcmp(name, "__OBJC") == 0) {
        return "__DATA";
    }
    return name;
}

static int ldd_output_push(ldd_output_list_t *list, ldd_output_section_t *section) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 32U;
        ldd_output_section_t **items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = section;
    return LDD_OK;
}

static int ldd_symbol_push(ldd_symbol_list_t *list, ldd_symbol_t *symbol) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 64U;
        ldd_symbol_t **items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = symbol;
    return LDD_OK;
}

static int ldd_fixup_push(ldd_fixup_list_t *list, uint32_t segment, uint64_t offset) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 64U;
        ldd_fixup_t *items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = (ldd_fixup_t) {segment, offset};
    return LDD_OK;
}

static int ldd_bind_push(ldd_bind_list_t *list, ldd_bind_t bind) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 128U;
        ldd_bind_t *items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = bind;
    return LDD_OK;
}

static int ldd_branch_thunk_push(ldd_branch_thunk_list_t *list, ldd_branch_thunk_t thunk) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 16U;
        ldd_branch_thunk_t *items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) return LDD_IO_ERROR;
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = thunk;
    return LDD_OK;
}

static void ldd_bytes_init(ldd_bytes_t *bytes) {
    memset(bytes, 0, sizeof(*bytes));
}

static int ldd_bytes_reserve(ldd_bytes_t *bytes, size_t required) {
    if (required <= bytes->capacity) {
        return LDD_OK;
    }
    size_t next = bytes->capacity ? bytes->capacity : 64U;
    while (next < required) {
        if (next > SIZE_MAX / 2U) {
            return LDD_IO_ERROR;
        }
        next *= 2U;
    }
    uint8_t *data = realloc(bytes->data, next);
    if (!data) {
        return LDD_IO_ERROR;
    }
    bytes->data = data;
    bytes->capacity = next;
    return LDD_OK;
}

static int ldd_bytes_put(ldd_bytes_t *bytes, const void *data, size_t size) {
    if (size > SIZE_MAX - bytes->size) {
        return LDD_IO_ERROR;
    }
    if (ldd_bytes_reserve(bytes, bytes->size + size) != LDD_OK) {
        return LDD_IO_ERROR;
    }
    memcpy(bytes->data + bytes->size, data, size);
    bytes->size += size;
    return LDD_OK;
}

static int ldd_bytes_u8(ldd_bytes_t *bytes, uint8_t value) {
    return ldd_bytes_put(bytes, &value, 1);
}

static int ldd_bytes_uleb(ldd_bytes_t *bytes, uint64_t value) {
    do {
        uint8_t byte = (uint8_t) (value & 0x7fU);
        value >>= 7U;
        if (value) {
            byte |= 0x80U;
        }
        if (ldd_bytes_u8(bytes, byte) != LDD_OK) {
            return LDD_IO_ERROR;
        }
    } while (value);
    return LDD_OK;
}

static int ldd_bytes_sleb(ldd_bytes_t *bytes, int64_t value) {
    bool more = true;
    while (more) {
        uint8_t byte = (uint8_t) (value & 0x7f);
        bool sign = (byte & 0x40U) != 0;
        value >>= 7;
        more = !((value == 0 && !sign) || (value == -1 && sign));
        if (more) {
            byte |= 0x80U;
        }
        if (ldd_bytes_u8(bytes, byte) != LDD_OK) {
            return LDD_IO_ERROR;
        }
    }
    return LDD_OK;
}

typedef struct ldd_export_node ldd_export_node_t;

typedef struct {
    uint8_t edge;
    ldd_export_node_t *node;
} ldd_export_child_t;

struct ldd_export_node {
    ldd_export_child_t *children;
    size_t child_count;
    size_t child_capacity;
    ldd_symbol_t *symbol;
    uint64_t offset;
    size_t encoded_size;
};

typedef struct {
    ldd_export_node_t **nodes;
    size_t count;
    size_t capacity;
} ldd_export_tree_t;

static size_t ldd_uleb_size(uint64_t value) {
    size_t size = 1;
    while (value >= 0x80U) {
        value >>= 7U;
        size++;
    }
    return size;
}

static ldd_export_node_t *ldd_export_node_new(ldd_export_tree_t *tree) {
    if (tree->count == tree->capacity) {
        size_t next = tree->capacity ? tree->capacity * 2U : 256U;
        ldd_export_node_t **nodes = ldd_realloc_array(tree->nodes, tree->capacity, next, sizeof(*nodes));
        if (!nodes) return NULL;
        tree->nodes = nodes;
        tree->capacity = next;
    }
    ldd_export_node_t *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    tree->nodes[tree->count++] = node;
    return node;
}

static ldd_export_node_t *ldd_export_child(ldd_export_tree_t *tree, ldd_export_node_t *parent, uint8_t edge) {
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i].edge == edge) return parent->children[i].node;
    }
    if (parent->child_count >= UINT8_MAX) return NULL;
    if (parent->child_count == parent->child_capacity) {
        size_t next = parent->child_capacity ? parent->child_capacity * 2U : 4U;
        ldd_export_child_t *children = ldd_realloc_array(parent->children, parent->child_capacity, next, sizeof(*children));
        if (!children) return NULL;
        parent->children = children;
        parent->child_capacity = next;
    }
    ldd_export_node_t *child = ldd_export_node_new(tree);
    if (!child) return NULL;
    parent->children[parent->child_count++] = (ldd_export_child_t) {edge, child};
    return child;
}

static void ldd_export_tree_deinit(ldd_export_tree_t *tree) {
    for (size_t i = 0; i < tree->count; i++) {
        free(tree->nodes[i]->children);
        free(tree->nodes[i]);
    }
    free(tree->nodes);
    memset(tree, 0, sizeof(*tree));
}

static uint32_t ldd_read_u32(const uint8_t *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint64_t ldd_read_u64(const uint8_t *p) {
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void ldd_write_u32(uint8_t *p, uint32_t value) {
    memcpy(p, &value, sizeof(value));
}

static void ldd_write_u64(uint8_t *p, uint64_t value) {
    memcpy(p, &value, sizeof(value));
}

static void ldd_write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t) (value >> 24U);
    p[1] = (uint8_t) (value >> 16U);
    p[2] = (uint8_t) (value >> 8U);
    p[3] = (uint8_t) value;
}

static void ldd_write_be64(uint8_t *p, uint64_t value) {
    ldd_write_be32(p, (uint32_t) (value >> 32U));
    ldd_write_be32(p + 4, (uint32_t) value);
}

static bool ldd_align_up_checked(uint64_t value, uint64_t alignment, uint64_t *result) {
    if (alignment <= 1) {
        *result = value;
        return true;
    }
    uint64_t padding = alignment - 1U;
    if (value > UINT64_MAX - padding) {
        return false;
    }
    *result = (value + padding) & ~padding;
    return true;
}

static uint64_t ldd_align_up(uint64_t value, uint64_t alignment) {
    uint64_t result = UINT64_MAX;
    (void) ldd_align_up_checked(value, alignment, &result);
    return result;
}


static bool ldd_symbol_is_definition(const ldd_input_symbol_t *symbol) {
    uint8_t type = symbol->entry.n_type & LDD_N_TYPE;
    return type == LDD_N_SECT || type == LDD_N_ABS ||
           type == LDD_N_INDR || (type == LDD_N_UNDF && symbol->entry.n_value != 0);
}

static bool ldd_symbol_is_common(const ldd_input_symbol_t *symbol) {
    return (symbol->entry.n_type & LDD_N_TYPE) == LDD_N_UNDF && symbol->entry.n_value != 0;
}

static bool ldd_symbol_is_external(const ldd_input_symbol_t *symbol) {
    return (symbol->entry.n_type & LDD_N_EXT) != 0;
}

static ldd_symbol_t *ldd_symbol_find(ldd_context_t *ctx, const char *name) {
    ldd_symbol_t *symbol = NULL;
    HASH_FIND_STR(ctx->symbols, name, symbol);
    return symbol;
}

static int ldd_definition_rank(ldd_symbol_kind_t kind, bool weak) {
    switch (kind) {
        case LDD_SYMBOL_DEFINED:
        case LDD_SYMBOL_ABSOLUTE:
            return weak ? 3 : 4;
        case LDD_SYMBOL_COMMON:
            return weak ? 1 : 2;
        case LDD_SYMBOL_UNDEFINED:
        case LDD_SYMBOL_IMPORT:
        default:
            return 0;
    }
}

static bool ldd_definition_better(ldd_symbol_kind_t new_kind, bool new_weak,
                                  ldd_symbol_kind_t old_kind, bool old_weak) {
    return ldd_definition_rank(new_kind, new_weak) > ldd_definition_rank(old_kind, old_weak);
}

static ldd_symbol_t *ldd_symbol_get_or_create(ldd_context_t *ctx, const char *name) {
    if (!name || !*name) {
        return NULL;
    }
    ldd_symbol_t *symbol = ldd_symbol_find(ctx, name);
    if (symbol) {
        return symbol;
    }
    symbol = calloc(1, sizeof(*symbol));
    if (!symbol) {
        ldd_fail(ctx, LDD_IO_ERROR, "out of memory for symbol '%s'", name);
        return NULL;
    }
    symbol->name = strdup(name);
    if (!symbol->name) {
        free(symbol);
        ldd_fail(ctx, LDD_IO_ERROR, "out of memory for symbol '%s'", name);
        return NULL;
    }
    symbol->kind = LDD_SYMBOL_UNDEFINED;
    symbol->got_index = UINT32_MAX;
    symbol->stub_index = UINT32_MAX;
    symbol->objc_stub_index = UINT32_MAX;
    symbol->dylib_ordinal = 1;
    HASH_ADD_KEYPTR(hh, ctx->symbols, symbol->name, strlen(symbol->name), symbol);
    return symbol;
}

static int ldd_register_input_symbol(ldd_context_t *ctx, ldd_object_t *object, ldd_input_symbol_t *input) {
    const char *name = input->name;
    if (!name || !*name || !ldd_symbol_is_external(input) || (input->entry.n_type & LDD_N_STAB)) {
        return LDD_OK;
    }
    bool common = ldd_symbol_is_common(input);
    uint8_t type = input->entry.n_type & LDD_N_TYPE;
    bool alias = type == LDD_N_INDR;
    bool definition = type == LDD_N_SECT || type == LDD_N_ABS || common || alias;
    bool weak = (input->entry.n_desc & LDD_N_WEAK_DEF) != 0;
    bool weak_ref = (input->entry.n_desc & LDD_N_WEAK_REF) != 0;
    ldd_symbol_t *symbol = ldd_symbol_get_or_create(ctx, name);
    if (!symbol) {
        return ctx->error ? ctx->error : LDD_IO_ERROR;
    }
    if (!definition) {
        if (symbol->kind == LDD_SYMBOL_UNDEFINED) {
            symbol->weak_ref = symbol->object ? symbol->weak_ref && weak_ref : weak_ref;
            symbol->object = object;
            symbol->input = input;
        }
        return LDD_OK;
    }
    ldd_symbol_t *alias_target = NULL;
    if (alias) {
        if (!input->alias_name || !*input->alias_name || strcmp(name, input->alias_name) == 0) {
            return ldd_fail(ctx, LDD_SYMBOL_ERROR, "invalid indirect symbol target for '%s'", name);
        }
        alias_target = ldd_symbol_get_or_create(ctx, input->alias_name);
        if (!alias_target) {
            return ctx->error ? ctx->error : LDD_IO_ERROR;
        }
    }
    if (common && symbol->kind == LDD_SYMBOL_COMMON) {
        uint32_t alignment = (input->entry.n_desc >> 8U) & 0xfU;
        if (input->entry.n_value > symbol->size) {
            symbol->size = input->entry.n_value;
            symbol->object = object;
            symbol->input = input;
        }
        if (alignment > symbol->align) symbol->align = alignment;
        /* A common symbol is weak only when every contributing tentative
           definition is weak.  This keeps the result independent of archive
           member/input order. */
        symbol->weak = symbol->weak && weak;
        return LDD_OK;
    }
    if (symbol->kind == LDD_SYMBOL_DEFINED || symbol->kind == LDD_SYMBOL_ABSOLUTE || symbol->alias) {
        if (!common && !symbol->weak && !weak) {
            const char *old_path = symbol->object && symbol->object->file ? symbol->object->file->path : "<linker>";
            const char *old_member = symbol->object && symbol->object->member_name ? symbol->object->member_name : NULL;
            const char *new_path = object && object->file ? object->file->path : "<linker>";
            const char *new_member = object && object->member_name ? object->member_name : NULL;
            return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                            "duplicate symbol definition: %s (previously in '%s'%s%s, again in '%s'%s%s)", name,
                            old_path, old_member ? " member " : "", old_member ? old_member : "", new_path,
                            new_member ? " member " : "", new_member ? new_member : "");
        }
        if (!ldd_definition_better(common              ? LDD_SYMBOL_COMMON
                                   : type == LDD_N_ABS ? LDD_SYMBOL_ABSOLUTE
                                                       : LDD_SYMBOL_DEFINED,
                                   weak, symbol->kind, symbol->weak)) {
            return LDD_OK;
        }
    }
    symbol->kind = common ? LDD_SYMBOL_COMMON : type == LDD_N_ABS ? LDD_SYMBOL_ABSOLUTE
                                                                  : LDD_SYMBOL_DEFINED;
    symbol->object = object;
    symbol->input = input;
    symbol->value = input->entry.n_value;
    symbol->size = common ? input->entry.n_value : 0;
    symbol->align = (input->entry.n_desc >> 8U) & 0xfU;
    symbol->weak = weak;
    symbol->weak_ref = weak_ref;
    symbol->alias = alias;
    symbol->alias_target = alias_target;
    return LDD_OK;
}

static int ldd_register_object_symbols(ldd_context_t *ctx, ldd_object_t *object) {
    for (size_t i = 0; i < object->symbol_count; i++) {
        if (ldd_register_input_symbol(ctx, object, &object->symbols[i]) != LDD_OK) {
            return ctx->error;
        }
    }
    return LDD_OK;
}

/* Resolve N_INDR chains after all currently selected objects have contributed
   their symbols.  An unresolved target is retained as a definition-shaped
   alias until final symbol checking so archive precedence remains stable. */
static int ldd_resolve_aliases(ldd_context_t *ctx) {
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!symbol->alias) {
            continue;
        }
        ldd_symbol_t *target = symbol->alias_target;
        size_t steps = 0;
        while (target && target->alias) {
            if (target == symbol || ++steps > ctx->objects.count + ctx->dylibs.count + 1024U) {
                return ldd_fail(ctx, LDD_SYMBOL_ERROR, "indirect symbol alias cycle involving '%s'", symbol->name);
            }
            target = target->alias_target;
        }
        if (!target) {
            return ldd_fail(ctx, LDD_SYMBOL_ERROR, "indirect symbol '%s' has no target", symbol->name);
        }
        bool alias_weak = symbol->weak;
        /* Keep an unresolved alias ranked as a definition until final symbol
           checking.  This prevents a later weak archive member with the same
           alias name from replacing a strong N_INDR definition. */
        symbol->kind = target->kind == LDD_SYMBOL_UNDEFINED ? LDD_SYMBOL_DEFINED : target->kind;
        symbol->value = target->value;
        symbol->size = target->size;
        symbol->align = target->align;
        /* The alias carries its own weak-definition bit.  Its target's weak
           status affects the target symbol, but must not strengthen an alias
           that was emitted as weak (or weaken a strong alias). */
        symbol->weak = alias_weak;
        symbol->weak_ref = target->weak_ref;
        symbol->dynamic = target->dynamic;
        symbol->dylib_ordinal = target->dylib_ordinal;
        symbol->output = target->output;
        symbol->output_offset = target->output_offset;
    }
    return LDD_OK;
}

static bool ldd_object_defines_unresolved(ldd_context_t *ctx, ldd_object_t *object) {
    for (size_t i = 0; i < object->symbol_count; i++) {
        ldd_input_symbol_t *input = &object->symbols[i];
        if (!input->name || !ldd_symbol_is_external(input) || !ldd_symbol_is_definition(input)) {
            continue;
        }
        ldd_symbol_t *symbol = ldd_symbol_find(ctx, input->name);
        if (!symbol || symbol->kind == LDD_SYMBOL_UNDEFINED || symbol->kind == LDD_SYMBOL_IMPORT) {
            if (symbol) return true;
            continue;
        }
        ldd_symbol_kind_t new_kind = ldd_symbol_is_common(input)
                                             ? LDD_SYMBOL_COMMON
                                             : ((input->entry.n_type & LDD_N_TYPE) == LDD_N_ABS
                                                        ? LDD_SYMBOL_ABSOLUTE
                                                        : LDD_SYMBOL_DEFINED);
        bool new_weak = (input->entry.n_desc & LDD_N_WEAK_DEF) != 0;
        if (ldd_definition_better(new_kind, new_weak, symbol->kind, symbol->weak) ||
            (new_kind == LDD_SYMBOL_COMMON && symbol->kind == LDD_SYMBOL_COMMON &&
             input->entry.n_value > symbol->size)) {
            return true;
        }
    }
    return false;
}

static int ldd_resolve_archives(ldd_context_t *ctx) {
    bool changed;
    do {
        changed = false;
        for (size_t i = 0; i < ctx->objects.count; i++) {
            ldd_object_t *object = ctx->objects.items[i];
            if (!object->archive_member || object->selected || !ldd_object_defines_unresolved(ctx, object)) {
                continue;
            }
            object->selected = true;
            if (ldd_register_object_symbols(ctx, object) != LDD_OK) {
                return ctx->error;
            }
            changed = true;
        }
    } while (changed);
    return LDD_OK;
}

static ldd_output_section_t *ldd_find_output(ldd_context_t *ctx, const char *segname, const char *sectname) {
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        ldd_output_section_t *section = ctx->outputs.items[i];
        if (strcmp(section->segname, segname) == 0 && strcmp(section->sectname, sectname) == 0) {
            return section;
        }
    }
    return NULL;
}

/*
 * ld64 accepts linker-defined boundary symbols in both their canonical form
 * ("section$start$...") and the form produced by assemblers which prepend a
 * Mach-O underscore ("_section$start$...").  Keep parsing deliberately
 * conservative: only these four forms are claimed, and malformed forms are
 * retained as boundary symbols so that the eventual diagnostic names the
 * offending target instead of silently turning it into a dylib import.
 */
static ldd_boundary_kind_t ldd_boundary_name_kind(const char *name, const char **payload) {
    static const struct {
        const char *prefix;
        ldd_boundary_kind_t kind;
    } prefixes[] = {
            {"section$start$", LDD_BOUNDARY_SECTION_START},
            {"section$end$", LDD_BOUNDARY_SECTION_END},
            {"segment$start$", LDD_BOUNDARY_SEGMENT_START},
            {"segment$end$", LDD_BOUNDARY_SEGMENT_END},
    };
    if (!name) {
        return LDD_BOUNDARY_NONE;
    }
    const char *candidate = name;
    for (size_t pass = 0; pass < 2U; pass++) {
        for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            size_t prefix_length = strlen(prefixes[i].prefix);
            if (strncmp(candidate, prefixes[i].prefix, prefix_length) == 0) {
                if (payload) *payload = candidate + prefix_length;
                return prefixes[i].kind;
            }
        }
        if (pass == 0 && candidate[0] == '_') {
            candidate++;
        } else {
            break;
        }
    }
    return LDD_BOUNDARY_NONE;
}

static void ldd_boundary_copy(char destination[17], const char *source, size_t length) {
    /* Mach-O segment and section names are at most 16 bytes.  An empty value
       deliberately records an invalid target and is diagnosed after layout. */
    if (!source || length == 0 || length >= 17U) {
        destination[0] = '\0';
        return;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void ldd_parse_boundary_name(ldd_symbol_t *symbol) {
    const char *payload = NULL;
    ldd_boundary_kind_t kind = ldd_boundary_name_kind(symbol->name, &payload);
    if (kind == LDD_BOUNDARY_NONE) {
        return;
    }
    symbol->boundary_kind = kind;
    symbol->linker_defined = true;
    symbol->boundary_segment[0] = '\0';
    symbol->boundary_section[0] = '\0';

    if (kind == LDD_BOUNDARY_SECTION_START || kind == LDD_BOUNDARY_SECTION_END) {
        const char *separator = payload ? strchr(payload, '$') : NULL;
        if (!separator) {
            /* Keep the kind, but leave the section empty for a useful error. */
            ldd_boundary_copy(symbol->boundary_segment, payload, payload ? strlen(payload) : 0);
            return;
        }
        ldd_boundary_copy(symbol->boundary_segment, payload, (size_t) (separator - payload));
        ldd_boundary_copy(symbol->boundary_section, separator + 1, strlen(separator + 1));
    } else {
        /* A segment target cannot contain another '$'.  Even when it does,
           retaining the complete payload makes the unknown-segment diagnostic
           deterministic and avoids accidentally resolving a prefix. */
        ldd_boundary_copy(symbol->boundary_segment, payload, payload ? strlen(payload) : 0);
    }
}

static int ldd_prepare_boundary_symbols(ldd_context_t *ctx) {
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->kind != LDD_SYMBOL_UNDEFINED || symbol->linker_defined) {
            continue;
        }
        ldd_parse_boundary_name(symbol);
        if (!symbol->linker_defined) {
            continue;
        }
        /* The final value is assigned after section/segment layout. */
        symbol->kind = LDD_SYMBOL_ABSOLUTE;
        symbol->value = 0;
        symbol->dynamic = false;
        symbol->weak = false;
        symbol->weak_ref = false;
    }
    return LDD_OK;
}

static ldd_output_section_t *ldd_get_output(ldd_context_t *ctx, const char *segname, const char *sectname,
                                            uint32_t flags, uint32_t align, bool zerofill) {
    ldd_output_section_t *section = ldd_find_output(ctx, segname, sectname);
    if (section) {
        if (section->zerofill != zerofill || (section->flags & LDD_SECTION_TYPE) != (flags & LDD_SECTION_TYPE)) {
            ldd_fail(ctx, LDD_INVALID_INPUT, "incompatible input sections %s,%s", segname, sectname);
            return NULL;
        }
        if (align > section->align) {
            section->align = align;
        }
        section->flags |= flags & ~LDD_SECTION_TYPE;
        return section;
    }
    section = calloc(1, sizeof(*section));
    if (!section) {
        return NULL;
    }
    ldd_copy_name16(section->segname, segname);
    ldd_copy_name16(section->sectname, sectname);
    section->flags = flags;
    section->align = align;
    section->zerofill = zerofill;
    if (ldd_output_push(&ctx->outputs, section) != LDD_OK) {
        free(section);
        return NULL;
    }
    return section;
}

static int ldd_output_reserve(ldd_output_section_t *section, uint64_t required) {
    if (required > SIZE_MAX) {
        return LDD_IO_ERROR;
    }
    if ((size_t) required <= section->data_capacity) {
        return LDD_OK;
    }
    size_t next = section->data_capacity ? section->data_capacity : 256U;
    while (next < required) {
        if (next > SIZE_MAX / 2U) {
            return LDD_IO_ERROR;
        }
        next *= 2U;
    }
    uint8_t *data = realloc(section->data, next);
    if (!data) {
        return LDD_IO_ERROR;
    }
    memset(data + section->data_capacity, 0, next - section->data_capacity);
    section->data = data;
    section->data_capacity = next;
    return LDD_OK;
}

static uint32_t ldd_section_alignment(const ldd_section_64_t *section) {
    uint32_t align = section->align;
    if (strncmp(section->sectname, "__thread_vars", 16) == 0 ||
        strncmp(section->sectname, "__thread_ptrs", 16) == 0 ||
        (section->flags & LDD_SECTION_TYPE) == LDD_S_THREAD_LOCAL_VARIABLE_POINTERS) {
        if (align < 3U) align = 3U;
    }
    return align;
}

static void ldd_output_section_names(const ldd_input_section_t *input, char segname[17], char sectname[17]) {
    ldd_copy_input_name(segname, input->header.segname);
    ldd_copy_input_name(sectname, input->header.sectname);
    const char *canonical = ldd_canonical_segment(segname);
    if (canonical != segname) snprintf(segname, 17, "%s", canonical);
    /* ld64 places initializer pointers in the read-only data segment. */
    if (strcmp(segname, "__DATA") == 0 && strcmp(sectname, "__mod_init_func") == 0) {
        snprintf(segname, 17, "__DATA_CONST");
    }
}

static int ldd_collect_sections(ldd_context_t *ctx) {
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) {
            continue;
        }
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            ldd_input_section_t *input = &object->sections[section_index];
            if (input->ignored || input->header.size == 0) {
                continue;
            }
            char segname[17], sectname[17];
            ldd_output_section_names(input, segname, sectname);
            if (strcmp(segname, "__TEXT") != 0 && strcmp(segname, "__DATA_CONST") != 0 &&
                strcmp(segname, "__DATA") != 0) {
                return ldd_fail(ctx, LDD_UNSUPPORTED, "unsupported input segment '%s' section '%s' in '%s'%s%s",
                                segname, sectname, object->file->path, object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            uint32_t type = input->header.flags & LDD_SECTION_TYPE;
            bool zerofill = type == LDD_S_ZEROFILL || type == LDD_S_GB_ZEROFILL ||
                            type == LDD_S_THREAD_LOCAL_ZEROFILL;
            ldd_output_section_t *output = ldd_get_output(ctx, segname, sectname, input->header.flags,
                                                          ldd_section_alignment(&input->header), zerofill);
            if (!output) {
                return ctx->error ? ctx->error : ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating section %s,%s", segname, sectname);
            }
            uint64_t alignment = 1ULL << output->align;
            uint64_t aligned_size;
            if (!ldd_align_up_checked(output->size, alignment, &aligned_size) ||
                input->header.size > UINT64_MAX - aligned_size) {
                return ldd_fail(ctx, LDD_OUTPUT_ERROR, "section %s,%s is too large", segname, sectname);
            }
            output->size = aligned_size;
            input->output = output;
            input->output_offset = output->size;
            output->size += input->header.size;
            if (!zerofill) {
                if (ldd_output_reserve(output, output->size) != LDD_OK) {
                    return ldd_fail(ctx, LDD_IO_ERROR, "out of memory merging section %s,%s", segname, sectname);
                }
                memcpy(output->data + input->output_offset, input->data, (size_t) input->header.size);
            }
        }
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->kind != LDD_SYMBOL_DEFINED || !symbol->input || !symbol->object || symbol->input->entry.n_sect == 0 ||
            symbol->input->entry.n_sect > symbol->object->section_count) {
            continue;
        }
        ldd_input_section_t *input = &symbol->object->sections[symbol->input->entry.n_sect - 1U];
        if (!input->output) {
            continue;
        }
        symbol->output = input->output;
        uint64_t relative = symbol->input->entry.n_value >= input->header.addr ? symbol->input->entry.n_value - input->header.addr : symbol->input->entry.n_value;
        symbol->output_offset = input->output_offset + relative;
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->kind != LDD_SYMBOL_COMMON) {
            continue;
        }
        if (!ctx->common) {
            ctx->common = ldd_get_output(ctx, "__DATA", "__common", LDD_S_ZEROFILL, 3, true);
            if (!ctx->common) {
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating common section");
            }
        }
        uint64_t alignment = 1ULL << (symbol->align ? symbol->align : 3U);
        uint64_t aligned_size;
        if (!ldd_align_up_checked(ctx->common->size, alignment, &aligned_size) ||
            symbol->size > UINT64_MAX - aligned_size) {
            return ldd_fail(ctx, LDD_OUTPUT_ERROR, "common symbol '%s' is too large", symbol->name);
        }
        ctx->common->size = aligned_size;
        symbol->output = ctx->common;
        symbol->output_offset = ctx->common->size;
        ctx->common->size += symbol->size;
        if (symbol->align > ctx->common->align) {
            ctx->common->align = symbol->align;
        }
    }
    return LDD_OK;
}

static ldd_symbol_t *ldd_symbol_for_input(ldd_context_t *ctx, ldd_object_t *object, uint32_t index) {
    if (index >= object->symbol_count) {
        return NULL;
    }
    ldd_input_symbol_t *input = &object->symbols[index];
    if (!input->name || !ldd_symbol_is_external(input)) {
        return NULL;
    }
    ldd_symbol_t *symbol = ldd_symbol_find(ctx, input->name);
    size_t steps = 0;
    while (symbol && symbol->alias && symbol->alias_target && symbol->alias_target != symbol &&
           ++steps <= ctx->objects.count + ctx->dylibs.count + 1024U) {
        symbol = symbol->alias_target;
    }
    return symbol;
}

static int ldd_make_synthetic_sections(ldd_context_t *ctx) {
    if (ctx->got_count) {
        ctx->got = ldd_get_output(ctx, "__DATA_CONST", "__got", LDD_S_NON_LAZY_SYMBOL_POINTERS, 3, false);
        if (!ctx->got) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating GOT");
        }
        ctx->got->size = (uint64_t) ctx->got_count * 8U;
        if (ldd_output_reserve(ctx->got, ctx->got->size) != LDD_OK) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating GOT");
        }
    }
    if (ctx->stub_count) {
        ctx->stubs = ldd_get_output(ctx, "__TEXT", "__stubs", LDD_S_SYMBOL_STUBS | LDD_S_ATTR_PURE_INSTRUCTIONS, 2, false);
        if (!ctx->stubs) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating stubs");
        }
        ctx->stubs->size = (uint64_t) ctx->stub_count * 12U;
        if (ldd_output_reserve(ctx->stubs, ctx->stubs->size) != LDD_OK) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating stubs");
        }
    }
    ldd_symbol_list_t objc_symbols = {0};
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!symbol->objc_selector_stub) {
            continue;
        }
        if (ldd_symbol_push(&objc_symbols, symbol) != LDD_OK) {
            free(objc_symbols.items);
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting Objective-C selector stubs");
        }
    }
    if (objc_symbols.count) {
        qsort(objc_symbols.items, objc_symbols.count, sizeof(objc_symbols.items[0]), ldd_symbol_name_compare);
        ctx->objc_methname = ldd_get_output(ctx, "__TEXT", "__objc_methname", LDD_S_CSTRING_LITERALS, 0, false);
        ctx->objc_selrefs = ldd_get_output(ctx, "__DATA", "__objc_selrefs", 0x10000005U, 3, false);
        ctx->objc_stubs = ldd_get_output(ctx, "__TEXT", "__objc_stubs",
                                         LDD_S_ATTR_PURE_INSTRUCTIONS | LDD_S_ATTR_SOME_INSTRUCTIONS, 5, false);
        if (!ctx->objc_methname || !ctx->objc_selrefs || !ctx->objc_stubs) {
            free(objc_symbols.items);
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating Objective-C selector sections");
        }
        ctx->objc_stub_count = (uint32_t) objc_symbols.count;
        for (size_t i = 0; i < objc_symbols.count; i++) {
            ldd_symbol_t *symbol = objc_symbols.items[i];
            const char *selector = symbol->name + 14;
            size_t selector_size = strlen(selector) + 1U;
            if (ctx->objc_methname->size > UINT64_MAX - selector_size ||
                ctx->objc_selrefs->size > UINT64_MAX - sizeof(uint64_t)) {
                free(objc_symbols.items);
                return ldd_fail(ctx, LDD_OUTPUT_ERROR, "Objective-C selector metadata is too large");
            }
            symbol->objc_selector_index = (uint32_t) i;
            symbol->objc_stub_index = (uint32_t) i;
            symbol->objc_methname_offset = ctx->objc_methname->size;
            if (ldd_output_reserve(ctx->objc_methname, ctx->objc_methname->size + selector_size) != LDD_OK) {
                free(objc_symbols.items);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory adding Objective-C selector name");
            }
            memcpy(ctx->objc_methname->data + ctx->objc_methname->size, selector, selector_size);
            ctx->objc_methname->size += selector_size;
            symbol->objc_selref_offset = ctx->objc_selrefs->size;
            if (ldd_output_reserve(ctx->objc_selrefs, ctx->objc_selrefs->size + sizeof(uint64_t)) != LDD_OK) {
                free(objc_symbols.items);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory adding Objective-C selector reference");
            }
            memset(ctx->objc_selrefs->data + ctx->objc_selrefs->size, 0, sizeof(uint64_t));
            ctx->objc_selrefs->size += sizeof(uint64_t);
            symbol->output = ctx->objc_stubs;
            symbol->output_offset = (uint64_t) i * 32U;
        }
        ctx->objc_stubs->size = (uint64_t) objc_symbols.count * 32U;
        if (ldd_output_reserve(ctx->objc_stubs, ctx->objc_stubs->size) != LDD_OK) {
            free(objc_symbols.items);
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating Objective-C selector stubs");
        }
    }
    free(objc_symbols.items);
    return LDD_OK;
}

static int ldd_scan_relocations(ldd_context_t *ctx) {
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) {
            continue;
        }
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            ldd_input_section_t *section = &object->sections[section_index];
            if (section->ignored || !section->output || !section->relocations) {
                continue;
            }
            for (uint32_t relocation_index = 0; relocation_index < section->header.nreloc; relocation_index++) {
                const uint8_t *raw = section->relocations + (size_t) relocation_index * 8U;
                ldd_relocation_t relocation;
                uint32_t first = ldd_read_u32(raw);
                uint32_t second = ldd_read_u32(raw + 4);
                relocation.address = first;
                relocation.symbolnum = second & 0x00ffffffU;
                relocation.pcrel = (uint8_t) ((second >> 24U) & 1U);
                relocation.length = (uint8_t) ((second >> 25U) & 3U);
                relocation.external = (uint8_t) ((second >> 27U) & 1U);
                relocation.type = (uint8_t) ((second >> 28U) & 0xfU);
                if (relocation.type == LDD_ARM64_RELOC_ADDEND) {
                    if (relocation_index + 1U >= section->header.nreloc) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unpaired ARM64 addend relocation in '%s'", object->file->path);
                    }
                    const uint8_t *next_raw = section->relocations + (size_t) (relocation_index + 1U) * 8U;
                    uint32_t next_word = ldd_read_u32(next_raw + 4);
                    uint8_t next_type = (uint8_t) ((next_word >> 28U) & 0xfU);
                    if (ldd_read_u32(next_raw) != relocation.address || !ldd_relocation_accepts_addend(next_type)) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 addend pair in '%s'", object->file->path);
                    }
                    continue;
                }
                if (relocation.type > LDD_ARM64_RELOC_ADDEND) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unsupported ARM64 relocation type %u in '%s'",
                                    relocation.type, object->file->path);
                }
                if (relocation.address >= section->header.size) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation outside %.*s,%.*s in '%s'",
                                    16, section->header.segname, 16, section->header.sectname, object->file->path);
                }
                uint64_t relocation_width = 1ULL << relocation.length;
                uint64_t access_width = (relocation.type == LDD_ARM64_RELOC_UNSIGNED ||
                                         relocation.type == LDD_ARM64_RELOC_SUBTRACTOR)
                                                ? relocation_width
                                                : 4U;
                if ((relocation.type == LDD_ARM64_RELOC_UNSIGNED || relocation.type == LDD_ARM64_RELOC_SUBTRACTOR)
                            ? (relocation_width != 4U && relocation_width != 8U)
                            : (relocation_width != 4U)) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 relocation width in '%s'", object->file->path);
                }
                bool valid_mode = true;
                switch (relocation.type) {
                    case LDD_ARM64_RELOC_UNSIGNED:
                    case LDD_ARM64_RELOC_SUBTRACTOR:
                        valid_mode = !relocation.pcrel;
                        break;
                    case LDD_ARM64_RELOC_BRANCH26:
                    case LDD_ARM64_RELOC_PAGE21:
                    case LDD_ARM64_RELOC_GOT_LOAD_PAGE21:
                    case LDD_ARM64_RELOC_TLVP_LOAD_PAGE21:
                        /* The arm64 Mach-O encodings for these relocations
                           carry a symbol-table index, not a section index.
                           A local target is still represented with
                           r_extern=1 and a non-external nlist entry (as
                           clang emits for static functions/data). */
                        valid_mode = relocation.pcrel && relocation.length == 2 && relocation.external;
                        break;
                    case LDD_ARM64_RELOC_PAGEOFF12:
                    case LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12:
                    case LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12:
                        valid_mode = !relocation.pcrel && relocation.length == 2 && relocation.external;
                        break;
                    case LDD_ARM64_RELOC_POINTER_TO_GOT:
                        /* ARM64_RELOC_POINTER_TO_GOT is the PC-relative
                           address of a GOT slot.  The absolute 64-bit form
                           is not part of the Mach-O arm64 ABI (and is
                           rejected by Zig's validateRelocType); accepting it
                           would produce a field that dyld cannot interpret
                           as a GOT relocation. */
                        valid_mode = relocation.pcrel && relocation.length == 2 && relocation.external;
                        break;
                    default:
                        break;
                }
                if (!valid_mode) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 relocation mode in '%s'", object->file->path);
                }
                if (!section->output->data || access_width > section->header.size - relocation.address) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation exceeds %.*s,%.*s in '%s'",
                                    16, section->header.segname, 16, section->header.sectname, object->file->path);
                }
                if (relocation.type != LDD_ARM64_RELOC_UNSIGNED &&
                    relocation.type != LDD_ARM64_RELOC_SUBTRACTOR &&
                    relocation.type != LDD_ARM64_RELOC_POINTER_TO_GOT) {
                    uint32_t instruction = ldd_read_u32(section->data + relocation.address);
                    bool valid_instruction = true;
                    if (relocation.type == LDD_ARM64_RELOC_BRANCH26) {
                        valid_instruction = ldd_is_branch26(instruction);
                        if (valid_instruction && ldd_branch26_implicit_addend(instruction) != 0) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                            "ARM64 BRANCH26 at offset 0x%x has an embedded addend; use ARM64_RELOC_ADDEND in '%s'%s%s",
                                            relocation.address, object->file->path,
                                            object->member_name ? " member " : "",
                                            object->member_name ? object->member_name : "");
                        }
                    } else if (relocation.type == LDD_ARM64_RELOC_PAGE21 ||
                               relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGE21 ||
                               relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGE21) {
                        valid_instruction = ldd_is_adrp(instruction);
                    } else if (relocation.type == LDD_ARM64_RELOC_PAGEOFF12 ||
                               relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 ||
                               relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12) {
                        valid_instruction = ldd_is_addsub_immediate(instruction) ||
                                            ldd_is_load_store_unsigned(instruction);
                    }
                    if (!valid_instruction) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "invalid ARM64 instruction for relocation type %u in '%s'",
                                        relocation.type, object->file->path);
                    }
                }
                ldd_symbol_t *symbol = relocation.external ? ldd_symbol_for_input(ctx, object, relocation.symbolnum) : NULL;
                if (relocation.external && !symbol) {
                    if (relocation.symbolnum >= object->symbol_count) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation symbol index is outside '%s'", object->file->path);
                    }
                    const ldd_input_symbol_t *input_symbol = &object->symbols[relocation.symbolnum];
                    uint8_t input_type = input_symbol->entry.n_type & LDD_N_TYPE;
                    if (input_type == LDD_N_UNDF && (input_symbol->entry.n_desc & LDD_N_WEAK_REF) == 0) {
                        const char *name = input_symbol->name;
                        return ldd_fail(ctx, LDD_SYMBOL_ERROR, "relocation references unknown symbol '%s'",
                                        name ? name : "<unnamed>");
                    }
                }
                if (!relocation.external && (relocation.symbolnum == 0 || relocation.symbolnum > object->section_count)) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation section index is outside '%s'", object->file->path);
                }
                if (!symbol) {
                    continue;
                }
                if (symbol->objc_selector_stub) {
                    if (relocation.type != LDD_ARM64_RELOC_BRANCH26) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "Objective-C selector stub '%s' has a non-branch relocation in '%s'",
                                        symbol->name, object->file->path);
                    }
                    if (!symbol->objc_dispatch) {
                        return ldd_fail(ctx, LDD_SYMBOL_ERROR, "Objective-C selector stub '%s' has no objc_msgSend target",
                                        symbol->name);
                    }
                    if (symbol->objc_dispatch->got_index == UINT32_MAX) {
                        symbol->objc_dispatch->got_index = ctx->got_count++;
                    }
                    continue;
                }
                if (relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGE21 ||
                    relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 ||
                    relocation.type == LDD_ARM64_RELOC_POINTER_TO_GOT ||
                    relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGE21 ||
                    relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12) {
                    if (symbol->got_index == UINT32_MAX) {
                        symbol->got_index = ctx->got_count++;
                    }
                }
                if (symbol->kind == LDD_SYMBOL_IMPORT) {
                    if (relocation.type == LDD_ARM64_RELOC_BRANCH26) {
                        if (symbol->stub_index == UINT32_MAX) {
                            symbol->stub_index = ctx->stub_count++;
                        }
                        if (symbol->got_index == UINT32_MAX) {
                            symbol->got_index = ctx->got_count++;
                        }
                    }
                }
            }
        }
    }
    return ldd_make_synthetic_sections(ctx);
}

static int ldd_segment_rank(const char *name) {
    if (strcmp(name, "__TEXT") == 0) {
        return 0;
    }
    if (strcmp(name, "__DATA_CONST") == 0) {
        return 1;
    }
    if (strcmp(name, "__DATA") == 0) {
        return 2;
    }
    return 3;
}

static int ldd_section_rank(const char *name) {
    if (strcmp(name, "__text") == 0) return 0;
    if (strcmp(name, "__stubs") == 0) return 1;
    if (strcmp(name, "__objc_stubs") == 0) return 2;
    if (strcmp(name, "__branch_islands") == 0) return 3;
    if (strcmp(name, "__stub_helper") == 0) return 3;
    if (strcmp(name, "__cstring") == 0) return 4;
    if (strcmp(name, "__objc_methname") == 0) return 5;
    if (strcmp(name, "__const") == 0) return 6;
    if (strcmp(name, "__literal4") == 0) return 7;
    if (strcmp(name, "__literal8") == 0) return 8;
    if (strcmp(name, "__literal16") == 0) return 9;
    if (strcmp(name, "__unwind_info") == 0) return 10;
    if (strcmp(name, "__eh_frame") == 0) return 11;
    if (strcmp(name, "__got") == 0) return 0;
    if (strcmp(name, "__mod_init_func") == 0) return 1;
    if (strcmp(name, "__la_symbol_ptr") == 0) return 0;
    if (strcmp(name, "__data") == 0) return 1;
    if (strcmp(name, "__thread_data") == 0) return 2;
    if (strcmp(name, "__thread_vars") == 0) return 3;
    if (strcmp(name, "__thread_ptrs") == 0) return 4;
    if (strcmp(name, "__thread_bss") == 0) return 5;
    if (strcmp(name, "__objc_selrefs") == 0) return 1;
    if (strcmp(name, "__common") == 0) return 8;
    if (strcmp(name, "__bss") == 0) return 9;
    return 5;
}

static int ldd_output_compare(const void *left, const void *right) {
    const ldd_output_section_t *a = *(const ldd_output_section_t *const *) left;
    const ldd_output_section_t *b = *(const ldd_output_section_t *const *) right;
    int segment = ldd_segment_rank(a->segname) - ldd_segment_rank(b->segname);
    if (segment) return segment;
    if (a->zerofill != b->zerofill) return a->zerofill ? 1 : -1;
    int section = ldd_section_rank(a->sectname) - ldd_section_rank(b->sectname);
    if (section) return section;
    return strcmp(a->sectname, b->sectname);
}

static ldd_segment_layout_t *ldd_find_segment(ldd_context_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->segment_count; i++) {
        if (strcmp(ctx->segments[i].name, name) == 0) {
            return &ctx->segments[i];
        }
    }
    return NULL;
}

static size_t ldd_output_count_for_segment(const ldd_context_t *ctx, const char *name) {
    size_t count = 0;
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        if (strcmp(ctx->outputs.items[i]->segname, name) == 0) count++;
    }
    return count;
}

static size_t ldd_builtin_dylib_count(const ldd_context_t *ctx) {
    return 1U + ctx->options->frameworks.count;
}

static bool ldd_builtin_dylib_path(const ldd_context_t *ctx, size_t index, char *path, size_t path_size) {
    if (index == 0) {
        return snprintf(path, path_size, "/usr/lib/libSystem.B.dylib") > 0;
    }
    size_t framework_index = index - 1U;
    if (framework_index < ctx->options->frameworks.count) {
        const char *name = ctx->options->frameworks.items[framework_index];
        char marker[LDD_MAX_PATH];
        int marker_length = snprintf(marker, sizeof(marker), "/%s.framework/", name);
        if (marker_length < 0 || (size_t) marker_length >= sizeof(marker)) return false;
        for (size_t i = 0; i < ctx->dylibs.count; i++) {
            const ldd_dylib_input_t *dylib = &ctx->dylibs.items[i];
            if (!dylib->reexport_only && strstr(dylib->install_name, marker)) {
                int result = snprintf(path, path_size, "%s", dylib->install_name);
                return result >= 0 && (size_t) result < path_size;
            }
        }
        int result = snprintf(path, path_size,
                              "/System/Library/Frameworks/%s.framework/Versions/A/%s", name, name);
        return result >= 0 && (size_t) result < path_size;
    }
    return false;
}

static bool ldd_dylib_install_name_is_builtin(const ldd_context_t *ctx, const char *install_name,
                                              size_t *builtin_index) {
    size_t count = ldd_builtin_dylib_count(ctx);
    for (size_t i = 0; i < count; i++) {
        char path[LDD_MAX_PATH];
        if (ldd_builtin_dylib_path(ctx, i, path, sizeof(path)) && strcmp(path, install_name) == 0) {
            if (builtin_index) *builtin_index = i;
            return true;
        }
    }
    return false;
}

static bool ldd_dylib_is_duplicate_extra(const ldd_context_t *ctx, size_t index) {
    if (ctx->dylibs.items[index].reexport_only) return true;
    const char *install_name = ctx->dylibs.items[index].install_name;
    if (ldd_dylib_install_name_is_builtin(ctx, install_name, NULL)) return true;
    for (size_t i = 0; i < index; i++) {
        if (strcmp(ctx->dylibs.items[i].install_name, install_name) == 0) return true;
    }
    return false;
}

static size_t ldd_dylib_count(const ldd_context_t *ctx) {
    size_t count = ldd_builtin_dylib_count(ctx);
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (!ldd_dylib_is_duplicate_extra(ctx, i)) count++;
    }
    return count;
}

static bool ldd_dylib_path(const ldd_context_t *ctx, size_t index, char *path, size_t path_size) {
    size_t builtin_count = ldd_builtin_dylib_count(ctx);
    if (index < builtin_count) return ldd_builtin_dylib_path(ctx, index, path, path_size);
    size_t extra_index = index - builtin_count;
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (ldd_dylib_is_duplicate_extra(ctx, i)) continue;
        if (extra_index-- == 0) {
            int result = snprintf(path, path_size, "%s", ctx->dylibs.items[i].install_name);
            return result >= 0 && (size_t) result < path_size;
        }
    }
    return false;
}

static const ldd_dylib_input_t *ldd_dylib_metadata(const ldd_context_t *ctx, const char *install_name) {
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (strcmp(ctx->dylibs.items[i].install_name, install_name) == 0) {
            return &ctx->dylibs.items[i];
        }
    }
    return NULL;
}

static uint32_t ldd_dylib_ordinal(const ldd_context_t *ctx, size_t input_index) {
    size_t builtin_index;
    if (ldd_dylib_install_name_is_builtin(ctx, ctx->dylibs.items[input_index].install_name, &builtin_index)) {
        return (uint32_t) builtin_index + 1U;
    }
    size_t ordinal = ldd_builtin_dylib_count(ctx) + 1U;
    for (size_t i = 0; i < input_index; i++) {
        if (!ldd_dylib_is_duplicate_extra(ctx, i)) ordinal++;
        if (strcmp(ctx->dylibs.items[i].install_name, ctx->dylibs.items[input_index].install_name) == 0) {
            return (uint32_t) (ordinal - 1U);
        }
    }
    return (uint32_t) ordinal;
}

static uint64_t ldd_load_commands_size(const ldd_context_t *ctx) {
    uint64_t size = sizeof(ldd_mach_header_64_t);
    size += sizeof(ldd_segment_command_64_t); /* __PAGEZERO */
    for (size_t i = 0; i < ctx->segment_count; i++) {
        const ldd_segment_layout_t *segment = &ctx->segments[i];
        size += sizeof(ldd_segment_command_64_t) + ldd_output_count_for_segment(ctx, segment->name) * sizeof(ldd_section_64_t);
    }
    size += sizeof(ldd_dyld_info_command_t);
    size += sizeof(ldd_symtab_command_t);
    size += sizeof(ldd_dysymtab_command_t);
    size += 32; /* LC_LOAD_DYLINKER */
    size += sizeof(ldd_uuid_command_t);
    size += sizeof(ldd_build_version_command_t);
    size += sizeof(ldd_source_version_command_t);
    size += sizeof(ldd_entry_point_command_t);
    for (size_t i = 0; i < ldd_dylib_count(ctx); i++) {
        char path[LDD_MAX_PATH];
        if (!ldd_dylib_path(ctx, i, path, sizeof(path))) {
            return UINT64_MAX;
        }
        size_t path_size = strlen(path) + 1U;
        size += (sizeof(ldd_dylib_command_t) + path_size + 7U) & ~7U;
    }
    size += sizeof(ldd_linkedit_data_command_t); /* function starts */
    size += sizeof(ldd_linkedit_data_command_t); /* data in code */
    if (ctx->options->adhoc_codesign) size += sizeof(ldd_linkedit_data_command_t);
    return ldd_align_up(size, 8);
}

static int ldd_layout_align(ldd_context_t *ctx, uint64_t value, uint64_t alignment,
                            uint64_t *result, const char *what) {
    if (!ldd_align_up_checked(value, alignment, result)) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "%s alignment overflows", what);
    }
    return LDD_OK;
}

static int ldd_layout_add(ldd_context_t *ctx, uint64_t left, uint64_t right,
                          uint64_t *result, const char *what) {
    if (right > UINT64_MAX - left) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "%s size overflows", what);
    }
    *result = left + right;
    return LDD_OK;
}

static int ldd_layout_sections(ldd_context_t *ctx) {
    qsort(ctx->outputs.items, ctx->outputs.count, sizeof(ctx->outputs.items[0]), ldd_output_compare);
    bool has_data_const = ldd_output_count_for_segment(ctx, "__DATA_CONST") != 0;
    bool has_data = ldd_output_count_for_segment(ctx, "__DATA") != 0;
    ctx->segment_count = 0;
    snprintf(ctx->segments[ctx->segment_count].name, sizeof(ctx->segments[ctx->segment_count].name), "__TEXT");
    ctx->segments[ctx->segment_count].command_index = 1;
    ctx->segments[ctx->segment_count].maxprot = LDD_VM_PROT_READ | LDD_VM_PROT_EXECUTE;
    ctx->segments[ctx->segment_count].initprot = LDD_VM_PROT_READ | LDD_VM_PROT_EXECUTE;
    ctx->segment_count++;
    if (has_data_const) {
        snprintf(ctx->segments[ctx->segment_count].name, sizeof(ctx->segments[ctx->segment_count].name), "__DATA_CONST");
        ctx->segments[ctx->segment_count].command_index = (uint32_t) ctx->segment_count + 1U;
        ctx->segments[ctx->segment_count].maxprot = LDD_VM_PROT_READ | LDD_VM_PROT_WRITE;
        ctx->segments[ctx->segment_count].initprot = LDD_VM_PROT_READ | LDD_VM_PROT_WRITE;
        ctx->segment_count++;
    }
    if (has_data) {
        snprintf(ctx->segments[ctx->segment_count].name, sizeof(ctx->segments[ctx->segment_count].name), "__DATA");
        ctx->segments[ctx->segment_count].command_index = (uint32_t) ctx->segment_count + 1U;
        ctx->segments[ctx->segment_count].maxprot = LDD_VM_PROT_READ | LDD_VM_PROT_WRITE;
        ctx->segments[ctx->segment_count].initprot = LDD_VM_PROT_READ | LDD_VM_PROT_WRITE;
        ctx->segment_count++;
    }
    snprintf(ctx->segments[ctx->segment_count].name, sizeof(ctx->segments[ctx->segment_count].name), "__LINKEDIT");
    ctx->segments[ctx->segment_count].command_index = (uint32_t) ctx->segment_count + 1U;
    ctx->segments[ctx->segment_count].maxprot = LDD_VM_PROT_READ;
    ctx->segments[ctx->segment_count].initprot = LDD_VM_PROT_READ;
    ctx->segment_count++;
    ctx->header_size = ldd_load_commands_size(ctx);
    if (ctx->header_size >= LDD_PAGE_SIZE) {
        return ldd_fail(ctx, LDD_UNSUPPORTED, "Mach-O load commands exceed the first page");
    }

    ldd_segment_layout_t *text = ldd_find_segment(ctx, "__TEXT");
    text->vmaddr = LDD_IMAGE_BASE;
    text->fileoff = 0;
    uint64_t cursor = ctx->header_size;
    uint32_t section_index = 1;
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        ldd_output_section_t *section = ctx->outputs.items[i];
        if (strcmp(section->segname, "__TEXT") != 0) continue;
        if (ldd_layout_align(ctx, cursor, 1ULL << section->align, &cursor, "__TEXT section") != LDD_OK) {
            return ctx->error;
        }
        section->fileoff = section->zerofill ? 0 : cursor;
        if (ldd_layout_add(ctx, text->vmaddr, cursor, &section->addr, "__TEXT address") != LDD_OK) {
            return ctx->error;
        }
        section->file_size = section->zerofill ? 0 : section->size;
        section->segment_index = text->command_index;
        section->section_index = section_index++;
        if (ldd_layout_add(ctx, cursor, section->size, &cursor, "__TEXT segment") != LDD_OK) {
            return ctx->error;
        }
    }
    if (ldd_layout_align(ctx, cursor, LDD_PAGE_SIZE, &text->filesize, "__TEXT segment") != LDD_OK) {
        return ctx->error;
    }
    text->vmsize = text->filesize;

    uint64_t file_cursor = text->filesize;
    uint64_t memory_cursor;
    if (ldd_layout_add(ctx, LDD_IMAGE_BASE, text->vmsize, &memory_cursor, "__TEXT virtual address") != LDD_OK) {
        return ctx->error;
    }
    for (size_t segment_no = 1; segment_no + 1 < ctx->segment_count; segment_no++) {
        ldd_segment_layout_t *segment = &ctx->segments[segment_no];
        if (ldd_layout_align(ctx, file_cursor, LDD_PAGE_SIZE, &file_cursor, "segment file offset") != LDD_OK ||
            ldd_layout_align(ctx, memory_cursor, LDD_PAGE_SIZE, &memory_cursor, "segment virtual address") != LDD_OK) {
            return ctx->error;
        }
        segment->fileoff = file_cursor;
        segment->vmaddr = memory_cursor;
        uint64_t segment_file_end = file_cursor;
        uint64_t segment_memory_end = memory_cursor;
        for (size_t i = 0; i < ctx->outputs.count; i++) {
            ldd_output_section_t *section = ctx->outputs.items[i];
            if (strcmp(section->segname, segment->name) != 0) continue;
            if (ldd_layout_align(ctx, segment_memory_end, 1ULL << section->align,
                                 &segment_memory_end, "section virtual address") != LDD_OK) {
                return ctx->error;
            }
            section->addr = segment_memory_end;
            section->segment_index = segment->command_index;
            section->section_index = section_index++;
            if (section->zerofill) {
                section->fileoff = 0;
                section->file_size = 0;
            } else {
                if (ldd_layout_align(ctx, segment_file_end, 1ULL << section->align,
                                     &segment_file_end, "section file offset") != LDD_OK) {
                    return ctx->error;
                }
                section->fileoff = segment_file_end;
                section->file_size = section->size;
                if (ldd_layout_add(ctx, segment_file_end, section->size,
                                   &segment_file_end, "segment file size") != LDD_OK) {
                    return ctx->error;
                }
            }
            if (ldd_layout_add(ctx, segment_memory_end, section->size,
                               &segment_memory_end, "segment virtual size") != LDD_OK) {
                return ctx->error;
            }
        }
        if (ldd_layout_align(ctx, segment_file_end - segment->fileoff, LDD_PAGE_SIZE,
                             &segment->filesize, "segment file size") != LDD_OK ||
            ldd_layout_align(ctx, segment_memory_end - segment->vmaddr, LDD_PAGE_SIZE,
                             &segment->vmsize, "segment virtual size") != LDD_OK ||
            ldd_layout_add(ctx, segment->fileoff, segment->filesize,
                           &file_cursor, "segment file end") != LDD_OK ||
            ldd_layout_add(ctx, segment->vmaddr, segment->vmsize,
                           &memory_cursor, "segment virtual end") != LDD_OK) {
            return ctx->error;
        }
    }
    ldd_segment_layout_t *linkedit = ldd_find_segment(ctx, "__LINKEDIT");
    if (ldd_layout_align(ctx, file_cursor, LDD_PAGE_SIZE, &linkedit->fileoff, "__LINKEDIT file offset") != LDD_OK ||
        ldd_layout_align(ctx, memory_cursor, LDD_PAGE_SIZE, &linkedit->vmaddr, "__LINKEDIT virtual address") != LDD_OK) {
        return ctx->error;
    }
    ctx->linkedit_fileoff = linkedit->fileoff;
    int boundary_result = ldd_resolve_boundary_symbols(ctx);
    if (boundary_result != LDD_OK) {
        return boundary_result;
    }
    ldd_symbol_t *entry = ldd_symbol_find(ctx, ctx->entry_name);
    if (!entry || (entry->kind != LDD_SYMBOL_DEFINED && entry->kind != LDD_SYMBOL_ABSOLUTE)) {
        return ldd_fail(ctx, LDD_SYMBOL_ERROR, "entry symbol '%s' is not defined", ctx->entry_name);
    }
    ctx->entry_address = ldd_symbol_address(entry);
    if (ctx->entry_address < LDD_IMAGE_BASE) {
        return ldd_fail(ctx, LDD_SYMBOL_ERROR, "entry symbol has invalid address");
    }
    ctx->entry_fileoff = ctx->entry_address - LDD_IMAGE_BASE;
    return LDD_OK;
}

static int ldd_resolve_boundary_symbols(ldd_context_t *ctx) {
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!symbol->linker_defined || symbol->boundary_kind == LDD_BOUNDARY_NONE) {
            continue;
        }
        uint64_t value = 0;
        switch (symbol->boundary_kind) {
            case LDD_BOUNDARY_SEGMENT_START:
            case LDD_BOUNDARY_SEGMENT_END: {
                ldd_segment_layout_t *segment = NULL;
                if (strcmp(symbol->boundary_segment, "__PAGEZERO") == 0) {
                    /* __PAGEZERO is implicit in our segment table. */
                    value = symbol->boundary_kind == LDD_BOUNDARY_SEGMENT_START ? 0 : LDD_IMAGE_BASE;
                    break;
                }
                segment = ldd_find_segment(ctx, symbol->boundary_segment);
                if (!segment && strcmp(symbol->boundary_segment, "__TEXT_EXEC") == 0) {
                    segment = ldd_find_segment(ctx, "__TEXT");
                } else if (!segment && strcmp(symbol->boundary_segment, "__AUTH_CONST") == 0) {
                    segment = ldd_find_segment(ctx, "__DATA_CONST");
                } else if (!segment && strcmp(symbol->boundary_segment, "__AUTH") == 0) {
                    segment = ldd_find_segment(ctx, "__DATA");
                }
                if (!segment) {
                    segment = ldd_find_segment(ctx, ldd_canonical_segment(symbol->boundary_segment));
                }
                if (!segment || !symbol->boundary_segment[0]) {
                    return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                                    "linker-defined symbol '%s' references unknown segment '%s'",
                                    symbol->name, symbol->boundary_segment[0] ? symbol->boundary_segment : "<empty>");
                }
                if (symbol->boundary_kind == LDD_BOUNDARY_SEGMENT_START) {
                    value = segment->vmaddr;
                } else if (segment->vmaddr > UINT64_MAX - segment->vmsize) {
                    return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                                    "linker-defined symbol '%s' segment '%s' address overflows",
                                    symbol->name, symbol->boundary_segment);
                } else {
                    value = segment->vmaddr + segment->vmsize;
                }
                break;
            }
            case LDD_BOUNDARY_SECTION_START:
            case LDD_BOUNDARY_SECTION_END: {
                ldd_output_section_t *section = NULL;
                if (symbol->boundary_segment[0] && symbol->boundary_section[0]) {
                    section = ldd_find_output(ctx, symbol->boundary_segment, symbol->boundary_section);
                    /* Input sections in these segments are folded into the
                       canonical output segments during collection.  Accept
                       the original linker-defined spelling as ld64 does. */
                    if (!section && strcmp(symbol->boundary_segment, "__TEXT_EXEC") == 0) {
                        section = ldd_find_output(ctx, "__TEXT", symbol->boundary_section);
                    } else if (!section && strcmp(symbol->boundary_segment, "__AUTH_CONST") == 0) {
                        section = ldd_find_output(ctx, "__DATA_CONST", symbol->boundary_section);
                    } else if (!section && strcmp(symbol->boundary_segment, "__AUTH") == 0) {
                        section = ldd_find_output(ctx, "__DATA", symbol->boundary_section);
                    } else if (!section && strcmp(symbol->boundary_segment, "__DATA") == 0 &&
                               strcmp(symbol->boundary_section, "__mod_init_func") == 0) {
                        section = ldd_find_output(ctx, "__DATA_CONST", "__mod_init_func");
                    }
                    if (!section) {
                        section = ldd_find_output(ctx, ldd_canonical_segment(symbol->boundary_segment),
                                                  symbol->boundary_section);
                    }
                }
                if (!section) {
                    return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                                    "linker-defined symbol '%s' references unknown section '%s,%s'",
                                    symbol->name, symbol->boundary_segment[0] ? symbol->boundary_segment : "<empty>",
                                    symbol->boundary_section[0] ? symbol->boundary_section : "<empty>");
                }
                if (symbol->boundary_kind == LDD_BOUNDARY_SECTION_START) {
                    value = section->addr;
                } else if (section->addr > UINT64_MAX - section->size) {
                    return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                                    "linker-defined symbol '%s' section '%s,%s' address overflows",
                                    symbol->name, symbol->boundary_segment, symbol->boundary_section);
                } else {
                    value = section->addr + section->size;
                }
                break;
            }
            case LDD_BOUNDARY_NONE:
                continue;
        }
        symbol->kind = LDD_SYMBOL_ABSOLUTE;
        symbol->value = value;
    }
    return LDD_OK;
}

static int64_t ldd_sign_extend(uint64_t value, unsigned bits) {
    uint64_t mask = 1ULL << (bits - 1U);
    return (int64_t) ((value ^ mask) - mask);
}

static bool ldd_relocation_accepts_addend(uint8_t type) {
    /* Clang emits an explicit ADDEND pair for arm64 source expressions with a
       non-zero constant, including BRANCH26.  The page and pageoff forms are
       the most common, but rejecting a branch pair makes otherwise valid
       `bl _symbol+N` objects unusable. */
    return type == LDD_ARM64_RELOC_BRANCH26 || type == LDD_ARM64_RELOC_PAGE21 ||
           type == LDD_ARM64_RELOC_PAGEOFF12;
}

static uint32_t ldd_patch_adrp(uint32_t instruction, int64_t page_delta) {
    int64_t immediate = page_delta >> 12;
    uint32_t value = (uint32_t) immediate & 0x1fffffU;
    instruction &= ~((0x3U << 29U) | (0x7ffffU << 5U));
    instruction |= ((value & 0x3U) << 29U) | (((value >> 2U) & 0x7ffffU) << 5U);
    return instruction;
}

static uint32_t ldd_patch_branch26(uint32_t instruction, int64_t delta) {
    int64_t immediate = delta >> 2;
    instruction &= ~0x03ffffffU;
    instruction |= (uint32_t) immediate & 0x03ffffffU;
    return instruction;
}

static int64_t ldd_branch26_implicit_addend(uint32_t instruction) {
    int64_t immediate = ldd_sign_extend(instruction & 0x03ffffffU, 26U);
    return immediate * 4;
}

static bool ldd_add_signed_u64(uint64_t value, int64_t addend, uint64_t *result) {
    if (addend >= 0) {
        uint64_t unsigned_addend = (uint64_t) addend;
        if (unsigned_addend > UINT64_MAX - value) {
            return false;
        }
        *result = value + unsigned_addend;
        return true;
    }
    uint64_t magnitude = (uint64_t) (-(addend + 1)) + 1U;
    if (magnitude > value) {
        return false;
    }
    *result = value - magnitude;
    return true;
}

/* ARM64_RELOC_UNSIGNED and ARM64_RELOC_SUBTRACTOR carry a signed addend in
   the bytes at the relocation site.  Keep the calculation in a wider signed
   type so malformed objects cannot silently wrap before the requested field
   width is checked. */
static bool ldd_relocation_value(uint64_t target, int64_t raw_addend, int64_t explicit_addend,
                                 uint8_t length, uint64_t *result) {
    if (target > (uint64_t) INT64_MAX || (length != 2U && length != 3U)) {
        return false;
    }
    __int128 value = (__int128) (int64_t) target + raw_addend + explicit_addend;
    __int128 minimum = length == 2U ? INT32_MIN : INT64_MIN;
    __int128 maximum = length == 2U ? UINT32_MAX : UINT64_MAX;
    if (value < minimum || value > maximum) {
        return false;
    }
    *result = (uint64_t) value;
    return true;
}

static bool ldd_relocation_difference(uint64_t minuend, uint64_t subtrahend,
                                      int64_t raw_addend, int64_t explicit_addend,
                                      uint8_t length, uint64_t *result) {
    if (minuend > (uint64_t) INT64_MAX || subtrahend > (uint64_t) INT64_MAX ||
        (length != 2U && length != 3U)) {
        return false;
    }
    __int128 value = (__int128) (int64_t) minuend - (__int128) (int64_t) subtrahend +
                     raw_addend + explicit_addend;
    __int128 minimum = length == 2U ? INT32_MIN : INT64_MIN;
    __int128 maximum = length == 2U ? UINT32_MAX : INT64_MAX;
    if (value < minimum || value > maximum) {
        return false;
    }
    *result = (uint64_t) value;
    return true;
}

static bool ldd_add_i64_checked(int64_t left, int64_t right, int64_t *result) {
    __int128 value = (__int128) left + right;
    if (value < INT64_MIN || value > INT64_MAX) {
        return false;
    }
    *result = (int64_t) value;
    return true;
}

static bool ldd_is_branch26(uint32_t instruction) {
    uint32_t opcode = instruction & 0xfc000000U;
    return opcode == 0x14000000U || opcode == 0x94000000U;
}

static bool ldd_is_adrp(uint32_t instruction) {
    return (instruction & 0x9f000000U) == 0x90000000U;
}

static bool ldd_is_addsub_immediate(uint32_t instruction) {
    return (instruction & 0x1f000000U) == 0x11000000U;
}

static bool ldd_is_load_store_unsigned(uint32_t instruction) {
    return (instruction & 0x3b000000U) == 0x39000000U;
}

static unsigned ldd_pageoff_shift(uint32_t instruction) {
    if (ldd_is_addsub_immediate(instruction)) {
        return 0;
    }
    if (!ldd_is_load_store_unsigned(instruction)) {
        return UINT_MAX;
    }
    unsigned size = (instruction >> 30U) & 3U;
    if ((instruction & (1U << 26U)) != 0 && size == 0) {
        unsigned vector_opcode = (instruction >> 22U) & 3U;
        if (vector_opcode == 2U || vector_opcode == 3U) {
            return 4;
        }
    }
    return size;
}

static bool ldd_patch_pageoff(uint32_t instruction, uint64_t offset, uint32_t *patched) {
    unsigned shift = ldd_pageoff_shift(instruction);
    if (shift == UINT_MAX) {
        return false;
    }
    uint64_t scaled = offset >> shift;
    if ((offset & ((1ULL << shift) - 1U)) != 0 || scaled > 0xfffU) {
        return false;
    }
    uint32_t immediate = (uint32_t) scaled;
    instruction &= ~(0xfffU << 10U);
    instruction |= immediate << 10U;
    *patched = instruction;
    return true;
}

static int ldd_segment_offset(ldd_context_t *ctx, uint64_t address, uint32_t *segment, uint64_t *offset) {
    for (size_t i = 0; i < ctx->segment_count; i++) {
        ldd_segment_layout_t *layout = &ctx->segments[i];
        if (address >= layout->vmaddr && address - layout->vmaddr < layout->vmsize) {
            *segment = layout->command_index;
            *offset = address - layout->vmaddr;
            return LDD_OK;
        }
    }
    return LDD_RELOCATION_ERROR;
}

static int ldd_add_rebase(ldd_context_t *ctx, uint64_t address) {
    uint32_t segment;
    uint64_t offset;
    if (ldd_segment_offset(ctx, address, &segment, &offset) != LDD_OK) {
        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "rebase address 0x%llx is outside an output segment",
                        (unsigned long long) address);
    }
    int result = ldd_fixup_push(&ctx->rebases, segment, offset);
    return result == LDD_OK ? LDD_OK : ldd_fail(ctx, LDD_IO_ERROR, "out of memory recording rebase fixup");
}

static int ldd_add_bind(ldd_context_t *ctx, ldd_symbol_t *symbol, uint64_t address, int64_t addend) {
    uint32_t segment;
    uint64_t offset;
    if (ldd_segment_offset(ctx, address, &segment, &offset) != LDD_OK) {
        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "bind address 0x%llx is outside an output segment",
                        (unsigned long long) address);
    }
    int result = ldd_bind_push(&ctx->binds,
                               (ldd_bind_t) {
                                       .symbol = symbol,
                                       .segment = segment,
                                       .offset = offset,
                                       .addend = addend,
                                       .weak = symbol->weak_ref,
                                       .weak_definition = false,
                               });
    return result == LDD_OK ? LDD_OK : ldd_fail(ctx, LDD_IO_ERROR, "out of memory recording bind fixup");
}

static int ldd_add_weak_bind(ldd_context_t *ctx, ldd_symbol_t *symbol,
                             uint64_t address, int64_t addend) {
    uint32_t segment;
    uint64_t offset;
    if (ldd_segment_offset(ctx, address, &segment, &offset) != LDD_OK) {
        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                        "weak bind address 0x%llx is outside an output segment",
                        (unsigned long long) address);
    }
    int result = ldd_bind_push(&ctx->binds,
                               (ldd_bind_t) {
                                       .symbol = symbol,
                                       .segment = segment,
                                       .offset = offset,
                                       .addend = addend,
                                       .weak = false,
                                       .weak_definition = true,
                               });
    return result == LDD_OK
                   ? LDD_OK
                   : ldd_fail(ctx, LDD_IO_ERROR,
                              "out of memory recording weak bind fixup");
}

static uint64_t ldd_tlv_template_base(const ldd_context_t *ctx) {
    uint64_t base = UINT64_MAX;
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        const ldd_output_section_t *section = ctx->outputs.items[i];
        uint32_t type = section->flags & LDD_SECTION_TYPE;
        if ((type == LDD_S_THREAD_LOCAL_REGULAR || type == LDD_S_THREAD_LOCAL_ZEROFILL) && section->addr < base) {
            base = section->addr;
        }
    }
    return base == UINT64_MAX ? 0 : base;
}

static int ldd_write_stub_data(ldd_context_t *ctx) {
    if (!ctx->stubs) {
        return LDD_OK;
    }
    for (size_t i = 0; i < ctx->dynamic_symbols.count; i++) {
        ldd_symbol_t *symbol = ctx->dynamic_symbols.items[i];
        if (symbol->stub_index == UINT32_MAX) continue;
        uint8_t *stub = ctx->stubs->data + (size_t) symbol->stub_index * 12U;
        uint64_t stub_address = ctx->stubs->addr + (uint64_t) symbol->stub_index * 12U;
        uint64_t got_address = ctx->got->addr + (uint64_t) symbol->got_index * 8U;
        uint32_t adrp = ldd_patch_adrp(0x90000210U, (int64_t) ((got_address & ~0xfffULL) - (stub_address & ~0xfffULL)));
        uint32_t ldr = 0xf9400210U;
        ldr |= ((uint32_t) ((got_address & 0xfffU) >> 3U) & 0xfffU) << 10U;
        uint32_t br = 0xd61f0200U;
        ldd_write_u32(stub, adrp);
        ldd_write_u32(stub + 4, ldr);
        ldd_write_u32(stub + 8, br);
    }
    return LDD_OK;
}

static int ldd_write_objc_stub_data(ldd_context_t *ctx) {
    if (!ctx->objc_stubs) {
        return LDD_OK;
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!symbol->objc_selector_stub) {
            continue;
        }
        if (!symbol->objc_dispatch || symbol->objc_dispatch->got_index == UINT32_MAX || !ctx->got ||
            symbol->objc_stub_index == UINT32_MAX) {
            return ldd_fail(ctx, LDD_SYMBOL_ERROR, "incomplete Objective-C selector stub '%s'", symbol->name);
        }
        uint64_t stub_address = ctx->objc_stubs->addr + (uint64_t) symbol->objc_stub_index * 32U;
        uint64_t selref_address = ctx->objc_selrefs->addr + symbol->objc_selref_offset;
        uint64_t methname_address = ctx->objc_methname->addr + symbol->objc_methname_offset;
        uint64_t got_address = ctx->got->addr + (uint64_t) symbol->objc_dispatch->got_index * 8U;
        if ((selref_address & 7U) != 0 || (got_address & 7U) != 0 ||
            (selref_address & 0xfffU) > 0xff8U || (got_address & 0xfffU) > 0xff8U) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unaligned Objective-C selector metadata for '%s'", symbol->name);
        }
        int64_t sel_page_delta = (int64_t) (selref_address & ~0xfffULL) - (int64_t) (stub_address & ~0xfffULL);
        int64_t got_page_delta = (int64_t) (got_address & ~0xfffULL) - (int64_t) ((stub_address + 8U) & ~0xfffULL);
        if (sel_page_delta < -(1LL << 32) || sel_page_delta >= (1LL << 32) ||
            got_page_delta < -(1LL << 32) || got_page_delta >= (1LL << 32)) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "Objective-C selector stub '%s' is out of ADRP range", symbol->name);
        }
        uint8_t *selref = ctx->objc_selrefs->data + symbol->objc_selref_offset;
        ldd_write_u64(selref, methname_address);
        if (ldd_add_rebase(ctx, selref_address) != LDD_OK) {
            return ctx->error;
        }
        uint8_t *stub = ctx->objc_stubs->data + (size_t) symbol->objc_stub_index * 32U;
        uint32_t adrp_selector = ldd_patch_adrp(0x90000001U, sel_page_delta);
        uint32_t ldr_selector = 0xf9400021U | (((uint32_t) ((selref_address & 0xfffU) >> 3U)) << 10U);
        uint32_t adrp_dispatch = ldd_patch_adrp(0x90000010U, got_page_delta);
        uint32_t ldr_dispatch = 0xf9400210U | (((uint32_t) ((got_address & 0xfffU) >> 3U)) << 10U);
        ldd_write_u32(stub, adrp_selector);
        ldd_write_u32(stub + 4, ldr_selector);
        ldd_write_u32(stub + 8, adrp_dispatch);
        ldd_write_u32(stub + 12, ldr_dispatch);
        ldd_write_u32(stub + 16, 0xd61f0200U);
        ldd_write_u32(stub + 20, 0xd4200020U);
        ldd_write_u32(stub + 24, 0xd4200020U);
        ldd_write_u32(stub + 28, 0xd4200020U);
    }
    return LDD_OK;
}

static ldd_branch_thunk_t *ldd_find_branch_thunk(ldd_context_t *ctx, ldd_object_t *object,
                                                 uint32_t section_index,
                                                 uint32_t relocation_index) {
    for (size_t i = 0; i < ctx->branch_thunks.count; i++) {
        ldd_branch_thunk_t *thunk = &ctx->branch_thunks.items[i];
        if (thunk->object == object && thunk->section_index == section_index &&
            thunk->relocation_index == relocation_index) {
            return thunk;
        }
    }
    return NULL;
}

static int ldd_apply_relocations(ldd_context_t *ctx) {
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            ldd_input_section_t *section = &object->sections[section_index];
            if (section->ignored || !section->output || !section->relocations || section->header.size == 0) continue;
            int64_t pending_addend = 0;
            bool has_pending_addend = false;
            for (uint32_t relocation_index = 0; relocation_index < section->header.nreloc; relocation_index++) {
                const uint8_t *raw = section->relocations + (size_t) relocation_index * 8U;
                ldd_relocation_t relocation;
                uint32_t first = ldd_read_u32(raw), second = ldd_read_u32(raw + 4);
                relocation.address = first;
                relocation.symbolnum = second & 0x00ffffffU;
                relocation.pcrel = (uint8_t) ((second >> 24U) & 1U);
                relocation.length = (uint8_t) ((second >> 25U) & 3U);
                relocation.external = (uint8_t) ((second >> 27U) & 1U);
                relocation.type = (uint8_t) ((second >> 28U) & 0xfU);
                if (relocation.type == LDD_ARM64_RELOC_ADDEND) {
                    if (has_pending_addend || relocation_index + 1U >= section->header.nreloc) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unpaired ARM64 addend relocation in '%s'", object->file->path);
                    }
                    const uint8_t *next_raw = section->relocations + (size_t) (relocation_index + 1U) * 8U;
                    uint32_t next_word = ldd_read_u32(next_raw + 4);
                    uint8_t next_type = (uint8_t) ((next_word >> 28U) & 0xfU);
                    if (ldd_read_u32(next_raw) != relocation.address || !ldd_relocation_accepts_addend(next_type)) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 addend pair in '%s'", object->file->path);
                    }
                    pending_addend += ldd_sign_extend(relocation.symbolnum, 24);
                    has_pending_addend = true;
                    continue;
                }
                if (relocation.type > LDD_ARM64_RELOC_ADDEND) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unsupported ARM64 relocation type %u in '%s'",
                                    relocation.type, object->file->path);
                }
                if (relocation.address >= section->header.size) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation outside input section in '%s'", object->file->path);
                }
                uint64_t width = 1ULL << relocation.length;
                uint64_t access_width = (relocation.type == LDD_ARM64_RELOC_UNSIGNED ||
                                         relocation.type == LDD_ARM64_RELOC_SUBTRACTOR)
                                                ? width
                                                : 4U;
                if ((relocation.type == LDD_ARM64_RELOC_UNSIGNED || relocation.type == LDD_ARM64_RELOC_SUBTRACTOR)
                            ? (width != 4U && width != 8U)
                            : (width != 4U)) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 relocation width in '%s'", object->file->path);
                }
                if (!section->output->data || access_width > section->header.size - relocation.address) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR, "relocation exceeds input section in '%s'", object->file->path);
                }
                uint8_t *location = section->output->data + section->output_offset + relocation.address;
                uint64_t place = section->output->addr + section->output_offset + relocation.address;
                ldd_symbol_t *symbol = ldd_relocation_symbol(ctx, object, &relocation);
                uint64_t target = 0;
                if (symbol && symbol->got_index != UINT32_MAX &&
                    (relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGE21 ||
                     relocation.type == LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 ||
                     relocation.type == LDD_ARM64_RELOC_POINTER_TO_GOT ||
                     relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGE21 ||
                     relocation.type == LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12)) {
                    target = ctx->got->addr + (uint64_t) symbol->got_index * 8U;
                } else if (symbol && symbol->kind == LDD_SYMBOL_IMPORT) {
                    if (relocation.type == LDD_ARM64_RELOC_BRANCH26) {
                        target = ctx->stubs->addr + (uint64_t) symbol->stub_index * 12U;
                    }
                } else {
                    target = ldd_relocation_target(ctx, object, &relocation);
                }
                int64_t addend = has_pending_addend ? pending_addend : 0;
                pending_addend = 0;
                has_pending_addend = false;
                if (relocation.type == LDD_ARM64_RELOC_SUBTRACTOR) {
                    if (relocation_index + 1U >= section->header.nreloc) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unpaired ARM64 subtractor relocation in '%s'", object->file->path);
                    }
                    const uint8_t *next_raw = section->relocations + (size_t) (++relocation_index) * 8U;
                    uint32_t next_word = ldd_read_u32(next_raw + 4);
                    uint32_t next_type = (next_word >> 28U) & 0xfU;
                    if (next_type != LDD_ARM64_RELOC_UNSIGNED || ldd_read_u32(next_raw) != relocation.address ||
                        ((next_word >> 25U) & 3U) != relocation.length || ((next_word >> 24U) & 1U) != relocation.pcrel ||
                        ((next_word >> 27U) & 1U) != relocation.external) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid ARM64 subtractor pair in '%s'", object->file->path);
                    }
                    ldd_relocation_t next = relocation;
                    next.symbolnum = next_word & 0x00ffffffU;
                    next.external = (uint8_t) ((next_word >> 27U) & 1U);
                    ldd_symbol_t *subtractor = symbol;
                    ldd_symbol_t *minuend_symbol = ldd_relocation_symbol(ctx, object, &next);
                    uint64_t minuend = minuend_symbol ? ldd_symbol_address(minuend_symbol) : ldd_relocation_target(ctx, object, &next);
                    uint64_t subtrahend = subtractor ? ldd_symbol_address(subtractor) : target;
                    int64_t raw_addend = width == 8 ? (int64_t) ldd_read_u64(location)
                                                    : (int64_t) (int32_t) ldd_read_u32(location);
                    uint64_t value;
                    if (!ldd_relocation_difference(minuend, subtrahend, raw_addend, addend,
                                                   relocation.length, &value)) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "ARM64 subtractor relocation overflows %u-byte field in '%s'",
                                        (unsigned) width, object->file->path);
                    }
                    if (width == 8) {
                        ldd_write_u64(location, value);
                    } else if (width == 4) {
                        ldd_write_u32(location, (uint32_t) value);
                    } else {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unsupported subtractor width in '%s'", object->file->path);
                    }
                    continue;
                }
                if (relocation.type == LDD_ARM64_RELOC_UNSIGNED) {
                    uint64_t raw_value = width == 8 ? ldd_read_u64(location) : width == 4 ? ldd_read_u32(location)
                                                                                          : 0;
                    int64_t raw_addend = width == 8 ? (int64_t) raw_value : (int64_t) (int32_t) raw_value;
                    if (width != 8 && width != 4) return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unsupported unsigned relocation width");
                    if (symbol && symbol->kind == LDD_SYMBOL_IMPORT) {
                        int64_t bind_addend;
                        if (!ldd_add_i64_checked(raw_addend, addend, &bind_addend)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                            "ARM64 unsigned relocation addend overflows in '%s'", object->file->path);
                        }
                        if (ldd_add_bind(ctx, symbol, place, bind_addend) != LDD_OK) return ctx->error;
                    } else if ((section->header.flags & LDD_SECTION_TYPE) == LDD_S_THREAD_LOCAL_VARIABLES &&
                               (relocation.address % 24U) == 16U) {
                        uint64_t tlv_base = ldd_tlv_template_base(ctx);
                        if (!tlv_base || target < tlv_base) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "invalid TLV initializer relocation in '%s'", object->file->path);
                        }
                        uint64_t value;
                        if (!ldd_relocation_value(target - tlv_base, raw_addend, addend,
                                                  relocation.length, &value)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                            "ARM64 TLV initializer relocation overflows %u-byte field in '%s'",
                                            (unsigned) width, object->file->path);
                        }
                        if (width == 8) ldd_write_u64(location, value);
                        else
                            ldd_write_u32(location, (uint32_t) value);
                    } else {
                        uint64_t value;
                        if (!ldd_relocation_value(target, raw_addend, addend, relocation.length, &value)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                            "ARM64 unsigned relocation overflows %u-byte field in '%s'",
                                            (unsigned) width, object->file->path);
                        }
                        if (width == 8) ldd_write_u64(location, value);
                        else
                            ldd_write_u32(location, (uint32_t) value);
                        if (width == 8 && ldd_add_rebase(ctx, place) != LDD_OK) return ctx->error;
                        if (width == 8 && symbol && symbol->weak) {
                            int64_t weak_addend;
                            if (!ldd_add_i64_checked(raw_addend, addend, &weak_addend)) {
                                return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                                "ARM64 weak bind addend overflows in '%s'",
                                                object->file->path);
                            }
                            if (ldd_add_weak_bind(ctx, symbol, place, weak_addend) != LDD_OK) {
                                return ctx->error;
                            }
                        }
                    }
                    continue;
                }
                uint32_t instruction = ldd_read_u32(location);
                switch (relocation.type) {
                    case LDD_ARM64_RELOC_BRANCH26: {
                        uint64_t adjusted_target;
                        ldd_branch_thunk_t *thunk = ldd_find_branch_thunk(
                                ctx, object, (uint32_t) section_index, relocation_index);
                        if (thunk) {
                            adjusted_target = ctx->branch_islands->addr + thunk->output_offset;
                        } else if (!ldd_add_signed_u64(target, addend, &adjusted_target)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                            "ARM64 branch relocation addend overflow in '%s'",
                                            object->file->path);
                        }
                        if (
                                adjusted_target > (uint64_t) INT64_MAX || place > (uint64_t) INT64_MAX) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 branch relocation addend overflow in '%s'", object->file->path);
                        }
                        int64_t delta = (int64_t) adjusted_target - (int64_t) place;
                        if ((delta & 3) || delta < -(128LL << 20) || delta >= (128LL << 20)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 branch relocation out of range in '%s'", object->file->path);
                        }
                        ldd_write_u32(location, ldd_patch_branch26(instruction, delta));
                        break;
                    }
                    case LDD_ARM64_RELOC_PAGE21: {
                        uint64_t adjusted_target;
                        if (!ldd_add_signed_u64(target, addend, &adjusted_target) ||
                            adjusted_target > (uint64_t) INT64_MAX || place > (uint64_t) INT64_MAX) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 page relocation addend overflow in '%s'", object->file->path);
                        }
                        int64_t delta = (int64_t) (adjusted_target & ~0xfffULL) - (int64_t) (place & ~0xfffULL);
                        if (delta < -(1LL << 32) || delta >= (1LL << 32))
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 page relocation out of range target=0x%llx place=0x%llx symbol='%s' in '%s' member '%s' section '%.*s' reloc %u", (unsigned long long) target, (unsigned long long) place, symbol && symbol->name ? symbol->name : "<local>", object->file->path, object->member_name ? object->member_name : "", 16, section->header.sectname, relocation_index);
                        ldd_write_u32(location, ldd_patch_adrp(instruction, delta));
                        break;
                    }
                    case LDD_ARM64_RELOC_PAGEOFF12:
                    case LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12:
                    case LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12: {
                        uint64_t adjusted_target;
                        if (!ldd_add_signed_u64(target, addend, &adjusted_target)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 pageoff relocation addend overflow in '%s'", object->file->path);
                        }
                        uint32_t patched;
                        if (!ldd_patch_pageoff(instruction, adjusted_target & 0xfffULL, &patched)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 pageoff relocation is misaligned or out of range in '%s'", object->file->path);
                        }
                        ldd_write_u32(location, patched);
                        break;
                    }
                    case LDD_ARM64_RELOC_GOT_LOAD_PAGE21:
                    case LDD_ARM64_RELOC_TLVP_LOAD_PAGE21: {
                        uint64_t adjusted_target;
                        if (!ldd_add_signed_u64(target, addend, &adjusted_target) ||
                            adjusted_target > (uint64_t) INT64_MAX || place > (uint64_t) INT64_MAX) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 GOT/TLV page relocation addend overflow in '%s'", object->file->path);
                        }
                        int64_t delta = (int64_t) (adjusted_target & ~0xfffULL) - (int64_t) (place & ~0xfffULL);
                        if (delta < -(1LL << 32) || delta >= (1LL << 32)) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 GOT/TLV page relocation out of range in '%s'", object->file->path);
                        }
                        ldd_write_u32(location, ldd_patch_adrp(instruction, delta));
                        break;
                    }
                    case LDD_ARM64_RELOC_POINTER_TO_GOT: {
                        uint64_t adjusted_target;
                        if (!ldd_add_signed_u64(target, addend, &adjusted_target) ||
                            adjusted_target > (uint64_t) INT64_MAX || place > (uint64_t) INT64_MAX) {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 GOT relocation addend overflow in '%s'", object->file->path);
                        }
                        if (relocation.pcrel && width == 4) {
                            int64_t delta = (int64_t) adjusted_target - (int64_t) place;
                            if (delta < INT32_MIN || delta > INT32_MAX) {
                                return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 GOT relocation out of range in '%s'", object->file->path);
                            }
                            ldd_write_u32(location, (uint32_t) (int32_t) delta);
                        } else if (!relocation.pcrel && width == 8) {
                            ldd_write_u64(location, adjusted_target);
                            if (ldd_add_rebase(ctx, place) != LDD_OK) return ctx->error;
                        } else {
                            return ldd_fail(ctx, LDD_RELOCATION_ERROR, "ARM64 GOT relocation has invalid width or mode in '%s'", object->file->path);
                        }
                        break;
                    }
                    default:
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR, "unsupported ARM64 relocation type %u in '%s'", relocation.type, object->file->path);
                }
            }
        }
    }
    if (ldd_write_stub_data(ctx) != LDD_OK) return ctx->error;
    if (ldd_write_objc_stub_data(ctx) != LDD_OK) return ctx->error;
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->got_index == UINT32_MAX) continue;
        uint64_t got_address = ctx->got->addr + (uint64_t) symbol->got_index * 8U;
        if (symbol->kind == LDD_SYMBOL_IMPORT) {
            if (ldd_add_bind(ctx, symbol, got_address, 0) != LDD_OK) return ctx->error;
        } else {
            ldd_write_u64(ctx->got->data + (size_t) symbol->got_index * 8U, ldd_symbol_address(symbol));
            if (ldd_add_rebase(ctx, got_address) != LDD_OK) return ctx->error;
            if (symbol->weak && ldd_add_weak_bind(ctx, symbol, got_address, 0) != LDD_OK) {
                return ctx->error;
            }
        }
    }
    return LDD_OK;
}

static int ldd_define_special(ldd_context_t *ctx, const char *name,
                              bool execute_header, bool hidden) {
    ldd_symbol_t *existing = ldd_symbol_find(ctx, name);
    if (existing && existing->kind != LDD_SYMBOL_UNDEFINED && existing->kind != LDD_SYMBOL_IMPORT) {
        existing->execute_header = execute_header;
        existing->linker_defined = hidden;
        return LDD_OK;
    }
    if (existing) {
        existing->kind = LDD_SYMBOL_ABSOLUTE;
        existing->value = LDD_IMAGE_BASE;
        existing->dynamic = false;
        existing->weak = false;
        existing->execute_header = execute_header;
        existing->linker_defined = hidden;
        return LDD_OK;
    }
    ldd_symbol_t *symbol = calloc(1, sizeof(*symbol));
    if (!symbol) return ldd_fail(ctx, LDD_IO_ERROR, "out of memory defining '%s'", name);
    symbol->name = strdup(name);
    symbol->kind = LDD_SYMBOL_ABSOLUTE;
    symbol->value = LDD_IMAGE_BASE;
    symbol->got_index = UINT32_MAX;
    symbol->stub_index = UINT32_MAX;
    symbol->objc_stub_index = UINT32_MAX;
    symbol->execute_header = execute_header;
    symbol->linker_defined = hidden;
    if (!symbol->name) {
        free(symbol);
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory defining '%s'", name);
    }
    HASH_ADD_KEYPTR(hh, ctx->symbols, symbol->name, strlen(symbol->name), symbol);
    return LDD_OK;
}

static int ldd_require_symbol(ldd_context_t *ctx, const char *name) {
    if (ldd_symbol_find(ctx, name)) return LDD_OK;
    ldd_symbol_t *symbol = calloc(1, sizeof(*symbol));
    if (!symbol) return ldd_fail(ctx, LDD_IO_ERROR, "out of memory requiring '%s'", name);
    symbol->name = strdup(name);
    symbol->kind = LDD_SYMBOL_UNDEFINED;
    symbol->got_index = UINT32_MAX;
    symbol->stub_index = UINT32_MAX;
    symbol->objc_stub_index = UINT32_MAX;
    if (!symbol->name) {
        free(symbol);
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory requiring '%s'", name);
    }
    HASH_ADD_KEYPTR(hh, ctx->symbols, symbol->name, strlen(symbol->name), symbol);
    return LDD_OK;
}

static int ldd_symbol_name_compare(const void *left, const void *right) {
    const ldd_symbol_t *a = *(const ldd_symbol_t *const *) left;
    const ldd_symbol_t *b = *(const ldd_symbol_t *const *) right;
    return strcmp(a->name, b->name);
}

static void ldd_export_symbol_payload(const ldd_symbol_t *symbol,
                                      uint64_t *flags, uint64_t *address) {
    *flags = symbol->weak ? LDD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION : 0U;
    if (symbol->execute_header) {
        *flags |= LDD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR;
        *address = 0;
        return;
    }
    if (symbol->kind == LDD_SYMBOL_ABSOLUTE) {
        *flags |= LDD_EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE;
        *address = symbol->value;
        return;
    }
    uint32_t section_type = symbol->output ? symbol->output->flags & LDD_SECTION_TYPE : LDD_S_REGULAR;
    if (section_type == LDD_S_THREAD_LOCAL_REGULAR ||
        section_type == LDD_S_THREAD_LOCAL_ZEROFILL) {
        *flags |= LDD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL;
    } else {
        *flags |= LDD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR;
    }
    uint64_t value = ldd_symbol_address(symbol);
    *address = value >= LDD_IMAGE_BASE ? value - LDD_IMAGE_BASE : 0;
}

static size_t ldd_export_node_size(const ldd_export_node_t *node) {
    size_t terminal_size = 0;
    if (node->symbol) {
        uint64_t flags, address;
        ldd_export_symbol_payload(node->symbol, &flags, &address);
        terminal_size = ldd_uleb_size(flags) + ldd_uleb_size(address);
    }
    size_t size = ldd_uleb_size(terminal_size) + terminal_size + 1U;
    for (size_t i = 0; i < node->child_count; i++) {
        size += 2U + ldd_uleb_size(node->children[i].node->offset);
    }
    return size;
}

static int ldd_make_export_trie(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    ldd_symbol_list_t symbols = {0};
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        bool regular = (symbol->kind == LDD_SYMBOL_DEFINED || symbol->kind == LDD_SYMBOL_COMMON) &&
                       symbol->output;
        bool absolute = symbol->kind == LDD_SYMBOL_ABSOLUTE && !symbol->linker_defined;
        if ((regular || absolute || symbol->execute_header) && !symbol->objc_selector_stub) {
            if (ldd_symbol_push(&symbols, symbol) != LDD_OK) {
                free(symbols.items);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting exported symbols");
            }
        }
    }
    qsort(symbols.items, symbols.count, sizeof(symbols.items[0]), ldd_symbol_name_compare);
    ldd_export_tree_t tree = {0};
    ldd_export_node_t *root = ldd_export_node_new(&tree);
    if (!root) {
        free(symbols.items);
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating export trie");
    }
    for (size_t i = 0; i < symbols.count; i++) {
        ldd_export_node_t *node = root;
        const uint8_t *name = (const uint8_t *) symbols.items[i]->name;
        for (size_t j = 0; name[j] != '\0'; j++) {
            node = ldd_export_child(&tree, node, name[j]);
            if (!node) {
                free(symbols.items);
                ldd_export_tree_deinit(&tree);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating export trie");
            }
        }
        node->symbol = symbols.items[i];
    }
    free(symbols.items);

    bool changed = true;
    bool converged = false;
    for (unsigned pass = 0; pass < 32U && changed; pass++) {
        changed = false;
        uint64_t offset = 0;
        for (size_t i = 0; i < tree.count; i++) {
            ldd_export_node_t *node = tree.nodes[i];
            size_t encoded_size = ldd_export_node_size(node);
            if (node->offset != offset || node->encoded_size != encoded_size) changed = true;
            node->offset = offset;
            node->encoded_size = encoded_size;
            if (offset > UINT64_MAX - encoded_size) {
                ldd_export_tree_deinit(&tree);
                return ldd_fail(ctx, LDD_OUTPUT_ERROR, "export trie is too large");
            }
            offset += encoded_size;
        }
        if (!changed) converged = true;
    }
    if (!converged) {
        ldd_export_tree_deinit(&tree);
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "export trie offset calculation did not converge");
    }
    for (size_t i = 0; i < tree.count; i++) {
        ldd_export_node_t *node = tree.nodes[i];
        size_t terminal_size = 0;
        uint64_t flags = 0, address = 0;
        if (node->symbol) {
            ldd_export_symbol_payload(node->symbol, &flags, &address);
            terminal_size = ldd_uleb_size(flags) + ldd_uleb_size(address);
        }
        size_t start = stream->size;
        if (ldd_bytes_uleb(stream, terminal_size) != LDD_OK ||
            (node->symbol && (ldd_bytes_uleb(stream, flags) != LDD_OK || ldd_bytes_uleb(stream, address) != LDD_OK)) ||
            ldd_bytes_u8(stream, (uint8_t) node->child_count) != LDD_OK) {
            ldd_export_tree_deinit(&tree);
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding export trie");
        }
        for (size_t child = 0; child < node->child_count; child++) {
            uint8_t edge[2] = {node->children[child].edge, 0};
            if (ldd_bytes_put(stream, edge, sizeof(edge)) != LDD_OK ||
                ldd_bytes_uleb(stream, node->children[child].node->offset) != LDD_OK) {
                ldd_export_tree_deinit(&tree);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding export trie");
            }
        }
        if (stream->size - start != node->encoded_size) {
            ldd_export_tree_deinit(&tree);
            return ldd_fail(ctx, LDD_OUTPUT_ERROR, "export trie offset calculation did not converge");
        }
    }
    ldd_export_tree_deinit(&tree);
    return LDD_OK;
}

static int ldd_u64_compare(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return a < b ? -1 : a > b;
}

static int ldd_make_function_starts(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    uint64_t *addresses = NULL;
    size_t count = 0, capacity = 0;
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!symbol->output || strcmp(symbol->output->segname, "__TEXT") != 0 ||
            (symbol->output->flags & (LDD_S_ATTR_PURE_INSTRUCTIONS | LDD_S_ATTR_SOME_INSTRUCTIONS)) == 0 ||
            symbol->objc_selector_stub) continue;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2U : 256U;
            uint64_t *items = ldd_realloc_array(addresses, capacity, next, sizeof(*items));
            if (!items) {
                free(addresses);
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting function starts");
            }
            addresses = items;
            capacity = next;
        }
        addresses[count++] = ldd_symbol_address(symbol);
    }
    qsort(addresses, count, sizeof(*addresses), ldd_u64_compare);
    uint64_t previous = LDD_IMAGE_BASE;
    for (size_t i = 0; i < count; i++) {
        if (addresses[i] < previous || (i && addresses[i] == addresses[i - 1U])) continue;
        if (ldd_bytes_uleb(stream, addresses[i] - previous) != LDD_OK) {
            free(addresses);
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding function starts");
        }
        previous = addresses[i];
    }
    free(addresses);
    if (ldd_bytes_u8(stream, 0) != LDD_OK) return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding function starts");
    return LDD_OK;
}

static int ldd_data_in_code_compare(const void *left, const void *right) {
    const ldd_data_in_code_entry_t *a = left;
    const ldd_data_in_code_entry_t *b = right;
    return a->offset < b->offset ? -1 : a->offset > b->offset;
}

static int ldd_make_data_in_code(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    ldd_data_in_code_entry_t *entries = NULL;
    size_t count = 0, capacity = 0;
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t entry_index = 0; entry_index < object->data_in_code_count; entry_index++) {
            ldd_data_in_code_entry_t input;
            memcpy(&input, object->data_in_code + entry_index * sizeof(input),
                   sizeof(input));
            ldd_input_section_t *owner = NULL;
            for (size_t section_index = 0; section_index < object->section_count; section_index++) {
                ldd_input_section_t *section = &object->sections[section_index];
                if (section->ignored || !section->output || section->header.offset > input.offset) continue;
                uint64_t relative = (uint64_t) input.offset - section->header.offset;
                if (relative <= section->header.size && input.length <= section->header.size - relative) {
                    owner = section;
                    break;
                }
            }
            if (!owner) {
                free(entries);
                return ldd_fail(ctx, LDD_INVALID_INPUT, "data-in-code entry is outside sections in '%s'", object->file->path);
            }
            uint64_t output_offset = owner->output->fileoff + owner->output_offset +
                                     ((uint64_t) input.offset - owner->header.offset);
            if (output_offset > UINT32_MAX) {
                free(entries);
                return ldd_fail(ctx, LDD_OUTPUT_ERROR, "data-in-code output offset is too large");
            }
            if (count == capacity) {
                size_t next = capacity ? capacity * 2U : 32U;
                ldd_data_in_code_entry_t *items = ldd_realloc_array(entries, capacity, next, sizeof(*items));
                if (!items) {
                    free(entries);
                    return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting data-in-code entries");
                }
                entries = items;
                capacity = next;
            }
            input.offset = (uint32_t) output_offset;
            entries[count++] = input;
        }
    }
    qsort(entries, count, sizeof(*entries), ldd_data_in_code_compare);
    int result = count ? ldd_bytes_put(stream, entries, count * sizeof(*entries)) : LDD_OK;
    free(entries);
    if (result != LDD_OK) return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding data-in-code entries");
    return LDD_OK;
}

static int ldd_fixup_compare(const void *left, const void *right) {
    const ldd_fixup_t *a = left;
    const ldd_fixup_t *b = right;
    if (a->segment != b->segment) return a->segment < b->segment ? -1 : 1;
    return a->offset < b->offset ? -1 : a->offset > b->offset;
}

static int ldd_bind_compare(const void *left, const void *right) {
    const ldd_bind_t *a = left;
    const ldd_bind_t *b = right;
    if (a->segment != b->segment) return a->segment < b->segment ? -1 : 1;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return strcmp(a->symbol->name, b->symbol->name);
}

static int ldd_make_rebase_stream(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    if (!ctx->rebases.count) {
        return ldd_bytes_u8(stream, LDD_REBASE_OPCODE_DONE) == LDD_OK
                       ? LDD_OK
                       : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding rebase stream");
    }
    qsort(ctx->rebases.items, ctx->rebases.count, sizeof(ctx->rebases.items[0]), ldd_fixup_compare);
    if (ldd_bytes_u8(stream, LDD_REBASE_OPCODE_SET_TYPE_IMM | LDD_REBASE_TYPE_POINTER) != LDD_OK)
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding rebase stream");
    for (size_t i = 0; i < ctx->rebases.count; i++) {
        ldd_fixup_t fixup = ctx->rebases.items[i];
        if (fixup.segment > 15U) return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many Mach-O segments for rebase stream");
        if (ldd_bytes_u8(stream, LDD_REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | (uint8_t) fixup.segment) != LDD_OK ||
            ldd_bytes_uleb(stream, fixup.offset) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_REBASE_OPCODE_DO_REBASE_IMM_TIMES | 1U) != LDD_OK)
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding rebase stream");
    }
    return ldd_bytes_u8(stream, LDD_REBASE_OPCODE_DONE) == LDD_OK
                   ? LDD_OK
                   : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding rebase stream");
}

static int ldd_make_bind_stream(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    if (!ctx->binds.count) {
        return ldd_bytes_u8(stream, LDD_BIND_OPCODE_DONE) == LDD_OK
                       ? LDD_OK
                       : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding bind stream");
    }
    qsort(ctx->binds.items, ctx->binds.count, sizeof(ctx->binds.items[0]), ldd_bind_compare);
    int64_t current_addend = 0;
    for (size_t i = 0; i < ctx->binds.count; i++) {
        ldd_bind_t *bind = &ctx->binds.items[i];
        /* Weak imports still use the ordinary bind stream; the flag is
           attached to the symbol opcode.  The separate weak-bind stream is
           reserved for locations that point at weak definitions. */
        if (bind->weak_definition) continue;
        if (bind->segment > 15U) return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many Mach-O segments for bind stream");
        uint32_t ordinal = bind->symbol->dylib_ordinal ? bind->symbol->dylib_ordinal : 1U;
        const char *bind_name = strncmp(bind->symbol->name, "_objc_msgSend$", 14) == 0
                                        ? "_objc_msgSend"
                                        : bind->symbol->name;
        if ((ordinal <= 15U
                     ? ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | (uint8_t) ordinal)
                     : (ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB) == LDD_OK
                                ? ldd_bytes_uleb(stream, ordinal)
                                : LDD_IO_ERROR)) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM |
                                         (bind->weak ? LDD_BIND_SYMBOL_FLAGS_WEAK_IMPORT : 0U)) != LDD_OK ||
            ldd_bytes_put(stream, bind_name, strlen(bind_name) + 1U) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_TYPE_IMM | LDD_BIND_TYPE_POINTER) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | (uint8_t) bind->segment) != LDD_OK ||
            ldd_bytes_uleb(stream, bind->offset) != LDD_OK)
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding bind stream");
        if (bind->addend != current_addend) {
            if (ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_ADDEND_SLEB) != LDD_OK ||
                ldd_bytes_sleb(stream, bind->addend) != LDD_OK)
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding bind stream");
            current_addend = bind->addend;
        }
        if (ldd_bytes_u8(stream, LDD_BIND_OPCODE_DO_BIND) != LDD_OK)
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding bind stream");
    }
    return ldd_bytes_u8(stream, LDD_BIND_OPCODE_DONE) == LDD_OK
                   ? LDD_OK
                   : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding bind stream");
}

static int ldd_make_weak_bind_stream(ldd_context_t *ctx, ldd_bytes_t *stream) {
    ldd_bytes_init(stream);
    bool any = false;
    for (size_t i = 0; i < ctx->binds.count; i++) {
        if (ctx->binds.items[i].weak_definition) {
            any = true;
            break;
        }
    }
    if (!any) {
        return ldd_bytes_u8(stream, LDD_BIND_OPCODE_DONE) == LDD_OK
                       ? LDD_OK
                       : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding weak bind stream");
    }
    qsort(ctx->binds.items, ctx->binds.count, sizeof(ctx->binds.items[0]), ldd_bind_compare);
    int64_t current_addend = 0;
    for (size_t i = 0; i < ctx->binds.count; i++) {
        ldd_bind_t *bind = &ctx->binds.items[i];
        if (!bind->weak_definition) continue;
        if (bind->segment > 15U) return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many Mach-O segments for weak bind stream");
        const char *bind_name = strncmp(bind->symbol->name, "_objc_msgSend$", 14) == 0
                                        ? "_objc_msgSend"
                                        : bind->symbol->name;
        if (ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM) != LDD_OK ||
            ldd_bytes_put(stream, bind_name, strlen(bind_name) + 1U) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_TYPE_IMM | LDD_BIND_TYPE_POINTER) != LDD_OK ||
            ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | (uint8_t) bind->segment) != LDD_OK ||
            ldd_bytes_uleb(stream, bind->offset) != LDD_OK) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding weak bind stream");
        }
        if (bind->addend != current_addend) {
            if (ldd_bytes_u8(stream, LDD_BIND_OPCODE_SET_ADDEND_SLEB) != LDD_OK ||
                ldd_bytes_sleb(stream, bind->addend) != LDD_OK) {
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding weak bind stream");
            }
            current_addend = bind->addend;
        }
        if (ldd_bytes_u8(stream, LDD_BIND_OPCODE_DO_BIND) != LDD_OK) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding weak bind stream");
        }
    }
    return ldd_bytes_u8(stream, LDD_BIND_OPCODE_DONE) == LDD_OK
                   ? LDD_OK
                   : ldd_fail(ctx, LDD_IO_ERROR, "out of memory encoding weak bind stream");
}

typedef struct {
    ldd_bytes_t rebase;
    ldd_bytes_t bind;
    ldd_bytes_t weak_bind;
    ldd_bytes_t exports;
    ldd_bytes_t function_starts;
    ldd_bytes_t data_in_code;
    ldd_bytes_t symtab;
    ldd_bytes_t strtab;
    ldd_bytes_t indirect;
    uint32_t symoff;
    uint32_t stroff;
    uint32_t indirectoff;
    uint32_t rebaseoff;
    uint32_t bindoff;
    uint32_t weak_bindoff;
    uint32_t exportoff;
    uint32_t exportsize;
    uint32_t function_startsoff;
    uint32_t data_in_codeoff;
    uint32_t nlocalsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
} ldd_linkedit_t;

static void ldd_linkedit_deinit(ldd_linkedit_t *linkedit) {
    free(linkedit->rebase.data);
    free(linkedit->bind.data);
    free(linkedit->weak_bind.data);
    free(linkedit->exports.data);
    free(linkedit->function_starts.data);
    free(linkedit->data_in_code.data);
    free(linkedit->symtab.data);
    free(linkedit->strtab.data);
    free(linkedit->indirect.data);
    memset(linkedit, 0, sizeof(*linkedit));
}

static bool ldd_output_local_symbol(const ldd_input_symbol_t *input,
                                    const ldd_output_section_t **output_section) {
    if (!input || !input->name || !*input->name || !input->object ||
        (input->entry.n_type & (LDD_N_STAB | LDD_N_EXT)) != 0) {
        return false;
    }
    uint8_t type = input->entry.n_type & LDD_N_TYPE;
    if (type == LDD_N_ABS) {
        if (output_section) *output_section = NULL;
        return true;
    }
    if (type != LDD_N_SECT || input->entry.n_sect == 0 ||
        input->entry.n_sect > input->object->section_count) {
        return false;
    }
    const ldd_input_section_t *section = &input->object->sections[input->entry.n_sect - 1U];
    if (section->ignored || !section->output || section->output->section_index == 0 ||
        section->output->section_index > UINT8_MAX) {
        return false;
    }
    if (output_section) *output_section = section->output;
    return true;
}

static int ldd_make_symbol_tables(ldd_context_t *ctx, ldd_linkedit_t *linkedit) {
    ldd_bytes_init(&linkedit->symtab);
    ldd_bytes_init(&linkedit->strtab);
    ldd_bytes_init(&linkedit->indirect);
    ldd_symbol_list_t defined = {0};
    ldd_symbol_list_t imports = {0};
    size_t local_count = 0;
    uint8_t execute_header_section = 0;
    uint8_t zero = 0;
    if (ldd_bytes_u8(&linkedit->strtab, zero) != LDD_OK) goto oom;
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        const ldd_output_section_t *output = ctx->outputs.items[i];
        if (strcmp(output->segname, "__TEXT") == 0 && output->section_index != 0 &&
            output->section_index <= UINT8_MAX &&
            (execute_header_section == 0 || output->section_index < execute_header_section)) {
            execute_header_section = (uint8_t) output->section_index;
        }
    }
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t symbol_index = 0; symbol_index < object->symbol_count; symbol_index++) {
            if (ldd_output_local_symbol(&object->symbols[symbol_index], NULL)) {
                if (local_count == UINT32_MAX) goto too_many;
                local_count++;
            }
        }
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        /* Boundary symbols are linker implementation details.  ld64 uses
           them to fix relocations but does not expose their synthetic names
           in the final external symbol table. */
        if (symbol->linker_defined) {
            continue;
        }
        if (symbol->kind == LDD_SYMBOL_DEFINED || symbol->kind == LDD_SYMBOL_ABSOLUTE ||
            symbol->kind == LDD_SYMBOL_COMMON) {
            if (ldd_symbol_push(&defined, symbol) != LDD_OK) goto oom;
        } else if (symbol->kind == LDD_SYMBOL_IMPORT && !symbol->alias) {
            if (ldd_symbol_push(&imports, symbol) != LDD_OK) goto oom;
        }
    }
    if (defined.count > UINT32_MAX - local_count ||
        imports.count > UINT32_MAX - local_count - defined.count) goto too_many;
    qsort(defined.items, defined.count, sizeof(defined.items[0]), ldd_symbol_name_compare);
    qsort(imports.items, imports.count, sizeof(imports.items[0]), ldd_symbol_name_compare);

    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t symbol_index = 0; symbol_index < object->symbol_count; symbol_index++) {
            ldd_input_symbol_t *input = &object->symbols[symbol_index];
            const ldd_output_section_t *output = NULL;
            if (!ldd_output_local_symbol(input, &output)) continue;
            if (linkedit->strtab.size > UINT32_MAX) goto too_large;
            uint32_t string_offset = (uint32_t) linkedit->strtab.size;
            if (ldd_bytes_put(&linkedit->strtab, input->name, strlen(input->name) + 1U) != LDD_OK) goto oom;
            ldd_nlist_64_t entry = {0};
            entry.n_strx = string_offset;
            entry.n_type = input->entry.n_type & LDD_N_TYPE;
            entry.n_desc = input->entry.n_desc;
            entry.n_value = ldd_local_symbol_address(input);
            if (output) entry.n_sect = (uint8_t) output->section_index;
            if (ldd_bytes_put(&linkedit->symtab, &entry, sizeof(entry)) != LDD_OK) goto oom;
        }
    }

    for (size_t group = 0; group < 2; group++) {
        ldd_symbol_list_t *list = group == 0 ? &defined : &imports;
        for (size_t i = 0; i < list->count; i++) {
            ldd_symbol_t *symbol = list->items[i];
            symbol->symtab_index = (uint32_t) (local_count + (group == 0 ? 0 : defined.count) + i);
            if (linkedit->strtab.size > UINT32_MAX) goto too_large;
            uint32_t string_offset = (uint32_t) linkedit->strtab.size;
            if (ldd_bytes_put(&linkedit->strtab, symbol->name, strlen(symbol->name) + 1U) != LDD_OK) goto oom;
            ldd_nlist_64_t entry = {0};
            entry.n_strx = string_offset;
            if (group == 0) {
                if (symbol->execute_header) {
                    if (execute_header_section == 0) goto invalid_header;
                    entry.n_type = LDD_N_SECT | LDD_N_EXT;
                    entry.n_sect = execute_header_section;
                    entry.n_desc |= LDD_N_REFERENCED_DYNAMICALLY;
                } else if (symbol->kind == LDD_SYMBOL_ABSOLUTE || !symbol->output) {
                    entry.n_type = LDD_N_ABS | LDD_N_EXT;
                } else {
                    entry.n_type = LDD_N_SECT | LDD_N_EXT;
                    entry.n_sect = (uint8_t) symbol->output->section_index;
                    if (symbol->objc_selector_stub) {
                        entry.n_type |= LDD_N_PEXT;
                    }
                }
                entry.n_value = ldd_symbol_address(symbol);
                if (symbol->weak) entry.n_desc |= LDD_N_WEAK_DEF;
            } else {
                entry.n_type = LDD_N_UNDF | LDD_N_EXT;
                entry.n_desc = (uint16_t) ((symbol->dylib_ordinal ? symbol->dylib_ordinal : 1U) << 8U);
                if (symbol->weak_ref) entry.n_desc |= LDD_N_WEAK_REF;
            }
            if (ldd_bytes_put(&linkedit->symtab, &entry, sizeof(entry)) != LDD_OK) goto oom;
        }
    }
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        ldd_output_section_t *section = ctx->outputs.items[i];
        if (section == ctx->got) {
            section->flags = (section->flags & ~LDD_SECTION_TYPE) | LDD_S_NON_LAZY_SYMBOL_POINTERS;
            for (uint32_t slot = 0; slot < ctx->got_count; slot++) {
                for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
                    if (symbol->got_index == slot) {
                        uint32_t value = symbol->kind == LDD_SYMBOL_IMPORT ? symbol->symtab_index : 0x80000000U;
                        if (ldd_bytes_put(&linkedit->indirect, &value, sizeof(value)) != LDD_OK) goto oom;
                        break;
                    }
                }
            }
            section->reserved1 = 0;
        } else if (section == ctx->stubs) {
            section->flags = (section->flags & ~LDD_SECTION_TYPE) | LDD_S_SYMBOL_STUBS;
            section->reserved1 = (uint32_t) (linkedit->indirect.size / 4U);
            section->reserved2 = 12;
            for (size_t stub = 0; stub < ctx->stub_count; stub++) {
                for (size_t j = 0; j < imports.count; j++) {
                    if (imports.items[j]->stub_index == stub) {
                        uint32_t value = imports.items[j]->symtab_index;
                        if (ldd_bytes_put(&linkedit->indirect, &value, sizeof(value)) != LDD_OK) goto oom;
                        break;
                    }
                }
            }
        }
    }
    linkedit->nlocalsym = (uint32_t) local_count;
    linkedit->nextdefsym = (uint32_t) defined.count;
    linkedit->iundefsym = (uint32_t) (local_count + defined.count);
    free(defined.items);
    free(imports.items);
    return LDD_OK;

too_many:
    free(defined.items);
    free(imports.items);
    return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many symbols for Mach-O symbol table");

too_large:
    free(defined.items);
    free(imports.items);
    return ldd_fail(ctx, LDD_OUTPUT_ERROR, "Mach-O string table exceeds 32-bit offsets");

invalid_header:
    free(defined.items);
    free(imports.items);
    return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                    "cannot place __mh_execute_header without an output __TEXT section");

oom:
    free(defined.items);
    free(imports.items);
    return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating Mach-O symbol tables");
}

static size_t ldd_write_segment_command(ldd_context_t *ctx, uint8_t *image, size_t position,
                                        const ldd_segment_layout_t *segment, bool pagezero) {
    uint32_t nsects = pagezero ? 0 : (uint32_t) ldd_output_count_for_segment(ctx, segment->name);
    uint32_t cmdsize = (uint32_t) (sizeof(ldd_segment_command_64_t) + nsects * sizeof(ldd_section_64_t));
    ldd_segment_command_64_t command = {0};
    command.cmd = LDD_LC_SEGMENT_64;
    command.cmdsize = cmdsize;
    snprintf(command.segname, sizeof(command.segname), "%s", pagezero ? "__PAGEZERO" : segment->name);
    command.vmaddr = pagezero ? 0 : segment->vmaddr;
    command.vmsize = pagezero ? LDD_IMAGE_BASE : segment->vmsize;
    command.fileoff = pagezero ? 0 : segment->fileoff;
    command.filesize = pagezero ? 0 : segment->filesize;
    command.maxprot = pagezero ? 0 : segment->maxprot;
    command.initprot = pagezero ? 0 : segment->initprot;
    command.nsects = nsects;
    if (!pagezero && strcmp(segment->name, "__DATA_CONST") == 0) {
        command.flags = 0x10U; /* SG_READ_ONLY */
    }
    memcpy(image + position, &command, sizeof(command));
    position += sizeof(command);
    if (!pagezero) {
        for (size_t i = 0; i < ctx->outputs.count; i++) {
            ldd_output_section_t *section = ctx->outputs.items[i];
            if (strcmp(section->segname, segment->name) != 0) continue;
            ldd_section_64_t output = {0};
            ldd_copy_name16(output.sectname, section->sectname);
            ldd_copy_name16(output.segname, section->segname);
            output.addr = section->addr;
            output.size = section->size;
            output.offset = (uint32_t) section->fileoff;
            output.align = section->align;
            output.flags = section->flags;
            output.reserved1 = section->reserved1;
            output.reserved2 = section->reserved2;
            memcpy(image + position, &output, sizeof(output));
            position += sizeof(output);
        }
    }
    return position;
}

static size_t ldd_write_dylib_command(uint8_t *image, size_t position, const char *path,
                                      uint32_t current_version, uint32_t compatibility_version) {
    size_t path_size = strlen(path) + 1U;
    uint32_t command_size = (uint32_t) ((sizeof(ldd_dylib_command_t) + path_size + 7U) & ~7U);
    ldd_dylib_command_t command = {0};
    command.cmd = LDD_LC_LOAD_DYLIB;
    command.cmdsize = command_size;
    command.name_offset = sizeof(command);
    command.timestamp = 2;
    command.current_version = current_version;
    command.compatibility_version = compatibility_version;
    memcpy(image + position, &command, sizeof(command));
    memcpy(image + position + sizeof(command), path, path_size);
    return position + command_size;
}

static size_t ldd_write_load_commands(ldd_context_t *ctx, uint8_t *image, const ldd_linkedit_t *linkedit,
                                      uint32_t code_signature_offset, uint32_t code_signature_size) {
    size_t position = sizeof(ldd_mach_header_64_t);
    position = ldd_write_segment_command(ctx, image, position, NULL, true);
    for (size_t i = 0; i < ctx->segment_count; i++) {
        position = ldd_write_segment_command(ctx, image, position, &ctx->segments[i], false);
    }
    ldd_dyld_info_command_t dyld = {0};
    dyld.cmd = LDD_LC_DYLD_INFO_ONLY;
    dyld.cmdsize = sizeof(dyld);
    dyld.rebase_off = linkedit->rebaseoff;
    dyld.rebase_size = (uint32_t) linkedit->rebase.size;
    dyld.bind_off = linkedit->bindoff;
    dyld.bind_size = (uint32_t) linkedit->bind.size;
    dyld.weak_bind_off = linkedit->weak_bindoff;
    dyld.weak_bind_size = (uint32_t) linkedit->weak_bind.size;
    dyld.export_off = linkedit->exportoff;
    dyld.export_size = (uint32_t) linkedit->exports.size;
    memcpy(image + position, &dyld, sizeof(dyld));
    position += sizeof(dyld);

    ldd_symtab_command_t symtab = {0};
    symtab.cmd = LDD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = linkedit->symoff;
    symtab.nsyms = (uint32_t) (linkedit->symtab.size / sizeof(ldd_nlist_64_t));
    symtab.stroff = linkedit->stroff;
    symtab.strsize = (uint32_t) linkedit->strtab.size;
    memcpy(image + position, &symtab, sizeof(symtab));
    position += sizeof(symtab);

    ldd_dysymtab_command_t dysym = {0};
    dysym.cmd = LDD_LC_DYSYMTAB;
    dysym.cmdsize = sizeof(dysym);
    dysym.ilocalsym = 0;
    dysym.nlocalsym = linkedit->nlocalsym;
    dysym.iextdefsym = linkedit->nlocalsym;
    dysym.nextdefsym = linkedit->nextdefsym;
    dysym.iundefsym = linkedit->iundefsym;
    dysym.nundefsym = (uint32_t) (linkedit->symtab.size / sizeof(ldd_nlist_64_t)) - linkedit->iundefsym;
    dysym.indirectsymoff = linkedit->indirectoff;
    dysym.nindirectsyms = (uint32_t) (linkedit->indirect.size / sizeof(uint32_t));
    memcpy(image + position, &dysym, sizeof(dysym));
    position += sizeof(dysym);

    uint8_t dylinker[32] = {0};
    ldd_write_u32(dylinker, LDD_LC_LOAD_DYLINKER);
    ldd_write_u32(dylinker + 4, sizeof(dylinker));
    ldd_write_u32(dylinker + 8, 12);
    memcpy(dylinker + 12, "/usr/lib/dyld", sizeof("/usr/lib/dyld"));
    memcpy(image + position, dylinker, sizeof(dylinker));
    position += sizeof(dylinker);

    ldd_uuid_command_t uuid = {0};
    uuid.cmd = LDD_LC_UUID;
    uuid.cmdsize = sizeof(uuid);
    uint8_t digest[32];
    ldd_sha256(image, (size_t) ctx->linkedit_fileoff, digest);
    memcpy(uuid.uuid, digest, sizeof(uuid.uuid));
    memcpy(image + position, &uuid, sizeof(uuid));
    position += sizeof(uuid);

    ldd_build_version_command_t build = {0};
    build.cmd = LDD_LC_BUILD_VERSION;
    build.cmdsize = sizeof(build);
    build.platform = LDD_PLATFORM_MACOS;
    build.minos = ctx->min_version;
    build.sdk = ctx->sdk_version;
    memcpy(image + position, &build, sizeof(build));
    position += sizeof(build);

    ldd_source_version_command_t source = {0};
    source.cmd = LDD_LC_SOURCE_VERSION;
    source.cmdsize = sizeof(source);
    memcpy(image + position, &source, sizeof(source));
    position += sizeof(source);

    ldd_entry_point_command_t entry = {0};
    entry.cmd = LDD_LC_MAIN;
    entry.cmdsize = sizeof(entry);
    entry.entryoff = ctx->entry_fileoff;
    memcpy(image + position, &entry, sizeof(entry));
    position += sizeof(entry);
    for (size_t i = 0; i < ldd_dylib_count(ctx); i++) {
        char path[LDD_MAX_PATH];
        if (!ldd_dylib_path(ctx, i, path, sizeof(path))) {
            return 0;
        }
        const ldd_dylib_input_t *metadata = ldd_dylib_metadata(ctx, path);
        uint32_t current_version = metadata ? metadata->current_version : LDD_DEFAULT_DYLIB_VERSION;
        uint32_t compatibility_version = metadata ? metadata->compatibility_version : LDD_DEFAULT_DYLIB_VERSION;
        position = ldd_write_dylib_command(image, position, path, current_version, compatibility_version);
    }

    ldd_linkedit_data_command_t function_starts = {0};
    function_starts.cmd = LDD_LC_FUNCTION_STARTS;
    function_starts.cmdsize = sizeof(function_starts);
    function_starts.dataoff = linkedit->function_startsoff;
    function_starts.datasize = (uint32_t) linkedit->function_starts.size;
    memcpy(image + position, &function_starts, sizeof(function_starts));
    position += sizeof(function_starts);

    ldd_linkedit_data_command_t data_in_code = {0};
    data_in_code.cmd = LDD_LC_DATA_IN_CODE;
    data_in_code.cmdsize = sizeof(data_in_code);
    data_in_code.dataoff = linkedit->data_in_codeoff;
    data_in_code.datasize = (uint32_t) linkedit->data_in_code.size;
    memcpy(image + position, &data_in_code, sizeof(data_in_code));
    position += sizeof(data_in_code);

    if (ctx->options->adhoc_codesign) {
        ldd_linkedit_data_command_t code = {0};
        code.cmd = LDD_LC_CODE_SIGNATURE;
        code.cmdsize = sizeof(code);
        code.dataoff = code_signature_offset;
        code.datasize = code_signature_size;
        memcpy(image + position, &code, sizeof(code));
        position += sizeof(code);
    }
    return position;
}

static bool ldd_code_signature_size(uint64_t code_limit, size_t identifier_size, size_t *result) {
    if (code_limit > UINT64_MAX - (LDD_CODE_PAGE_SIZE - 1U) || identifier_size > UINT64_MAX - 89U) {
        return false;
    }
    uint64_t slots = (code_limit + LDD_CODE_PAGE_SIZE - 1U) / LDD_CODE_PAGE_SIZE;
    uint64_t directory = 89U + identifier_size;
    if (slots > (UINT64_MAX - directory) / 32U) {
        return false;
    }
    directory += slots * 32U;
    if (directory > UINT64_MAX - 20U) {
        return false;
    }
    uint64_t total = 12U + 8U + directory;
    uint64_t aligned = ldd_align_up(total, 16);
    if (aligned < total || aligned > SIZE_MAX) {
        return false;
    }
    *result = (size_t) aligned;
    return true;
}

static int ldd_write_code_signature(uint8_t *signature, const uint8_t *code, uint64_t code_limit,
                                    uint64_t exec_segment_size, const char *identifier, size_t output_size) {
    size_t identifier_size = strlen(identifier);
    size_t signature_size = 0;
    if (!ldd_code_signature_size(code_limit, identifier_size, &signature_size) || signature_size > output_size) {
        return LDD_OUTPUT_ERROR;
    }
    uint64_t slots = (code_limit + LDD_CODE_PAGE_SIZE - 1U) / LDD_CODE_PAGE_SIZE;
    uint32_t directory_length = (uint32_t) (88U + identifier_size + 1U + slots * 32U);
    uint32_t directory_offset = 20U;
    ldd_write_be32(signature, LDD_CSMAGIC_EMBEDDED_SIGNATURE);
    ldd_write_be32(signature + 4, (uint32_t) (12U + 8U + directory_length));
    ldd_write_be32(signature + 8, 1);
    ldd_write_be32(signature + 12, 0);
    ldd_write_be32(signature + 16, directory_offset);
    uint8_t *directory = signature + directory_offset;
    ldd_write_be32(directory, LDD_CSMAGIC_CODEDIRECTORY);
    ldd_write_be32(directory + 4, directory_length);
    ldd_write_be32(directory + 8, LDD_CS_SUPPORTSEXECSEG);
    ldd_write_be32(directory + 12, LDD_CS_ADHOC | LDD_CS_LINKER_SIGNED);
    ldd_write_be32(directory + 16, 88U + (uint32_t) identifier_size + 1U);
    ldd_write_be32(directory + 20, 88U);
    ldd_write_be32(directory + 24, 0);
    ldd_write_be32(directory + 28, (uint32_t) slots);
    ldd_write_be32(directory + 32, (uint32_t) code_limit);
    directory[36] = 32;
    directory[37] = 2;
    directory[38] = 0;
    directory[39] = 12;
    ldd_write_be32(directory + 40, 0);
    ldd_write_be32(directory + 44, 0);
    ldd_write_be32(directory + 48, 0);
    ldd_write_be32(directory + 52, 0);
    ldd_write_be64(directory + 56, code_limit);
    ldd_write_be64(directory + 64, 0);
    ldd_write_be64(directory + 72, exec_segment_size);
    ldd_write_be64(directory + 80, LDD_CS_EXECSEG_MAIN_BINARY);
    memcpy(directory + 88, identifier, identifier_size + 1U);
    uint8_t *hashes = directory + 88 + identifier_size + 1U;
    for (uint64_t slot = 0; slot < slots; slot++) {
        uint64_t offset = slot * LDD_CODE_PAGE_SIZE;
        size_t length = (size_t) (code_limit - offset > LDD_CODE_PAGE_SIZE ? LDD_CODE_PAGE_SIZE : code_limit - offset);
        ldd_sha256(code + offset, length, hashes + slot * 32U);
    }
    return LDD_OK;
}


static int ldd_write_output_file(ldd_context_t *ctx, const uint8_t *image, size_t size) {
    size_t path_length = strlen(ctx->options->output_path);
    if (path_length > SIZE_MAX - 16U) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "output path is too long");
    }
    char *temporary_path = malloc(path_length + 16U);
    if (!temporary_path) {
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating temporary output path");
    }
    snprintf(temporary_path, path_length + 16U, "%s.tmp.XXXXXX", ctx->options->output_path);
    int fd = mkstemp(temporary_path);
    if (fd < 0) {
        int saved = errno;
        free(temporary_path);
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot create temporary output for '%s': %s",
                        ctx->options->output_path, strerror(saved));
    }
    size_t written = 0;
    while (written < size) {
        ssize_t result = write(fd, image + written, size - written);
        if (result <= 0) {
            int saved = errno;
            close(fd);
            unlink(temporary_path);
            free(temporary_path);
            return ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot write '%s': %s", ctx->options->output_path,
                            result == 0 ? "short write" : strerror(saved));
        }
        written += (size_t) result;
    }
    if (fchmod(fd, 0755) != 0) {
        int saved = errno;
        close(fd);
        unlink(temporary_path);
        free(temporary_path);
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot chmod '%s': %s", ctx->options->output_path, strerror(saved));
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        unlink(temporary_path);
        free(temporary_path);
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot flush '%s': %s", ctx->options->output_path, strerror(saved));
    }
    close(fd);
    if (rename(temporary_path, ctx->options->output_path) != 0) {
        int saved = errno;
        unlink(temporary_path);
        free(temporary_path);
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot replace '%s': %s", ctx->options->output_path, strerror(saved));
    }
    free(temporary_path);
    return LDD_OK;
}

static bool ldd_is_objc_selector_stub_name(const char *name) {
    return name && strncmp(name, "_objc_msgSend$", 14) == 0 && name[14] != '\0';
}

/* Clang emits selector-specialized objc_msgSend symbols as unresolved names.
   They are local entry points synthesized by ld64, not separate dylib imports. */
static int ldd_prepare_objc_selector_symbols(ldd_context_t *ctx) {
    bool found = false;
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (!ldd_is_objc_selector_stub_name(symbol->name)) {
            continue;
        }
        symbol->objc_selector_stub = true;
        symbol->kind = LDD_SYMBOL_DEFINED;
        symbol->dynamic = false;
        symbol->weak = false;
        found = true;
    }
    if (!found) {
        return LDD_OK;
    }
    int result = ldd_require_symbol(ctx, "_objc_msgSend");
    if (result != LDD_OK) {
        return result;
    }
    ldd_symbol_t *dispatch = ldd_symbol_find(ctx, "_objc_msgSend");
    if (!dispatch) {
        return ldd_fail(ctx, LDD_SYMBOL_ERROR, "cannot synthesize objc_msgSend dispatch symbol");
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->objc_selector_stub) {
            symbol->objc_dispatch = dispatch;
        }
    }
    return LDD_OK;
}

static bool ldd_dylib_exports_symbol(const ldd_dylib_input_t *dylib, const char *name) {
    for (size_t i = 0; i < dylib->export_count; i++) {
        if (strcmp(dylib->exports[i], name) == 0) return true;
    }
    for (size_t i = 0; i < dylib->weak_export_count; i++) {
        if (strcmp(dylib->weak_exports[i], name) == 0) return true;
    }
    return false;
}

static bool ldd_any_dylib_exports_symbol(const ldd_context_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (ldd_dylib_exports_symbol(&ctx->dylibs.items[i], name)) return true;
    }
    return false;
}

static int ldd_emit_image(ldd_context_t *ctx) {
    ldd_linkedit_t linkedit;
    memset(&linkedit, 0, sizeof(linkedit));
    uint8_t *image = NULL;
    int result = ldd_make_rebase_stream(ctx, &linkedit.rebase);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_bind_stream(ctx, &linkedit.bind);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_weak_bind_stream(ctx, &linkedit.weak_bind);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_export_trie(ctx, &linkedit.exports);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_function_starts(ctx, &linkedit.function_starts);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_data_in_code(ctx, &linkedit.data_in_code);
    if (result != LDD_OK) goto cleanup;
    result = ldd_make_symbol_tables(ctx, &linkedit);
    if (result != LDD_OK) goto cleanup;
    if (linkedit.rebase.size > UINT32_MAX || linkedit.bind.size > UINT32_MAX ||
        linkedit.weak_bind.size > UINT32_MAX ||
        linkedit.exports.size > UINT32_MAX || linkedit.function_starts.size > UINT32_MAX ||
        linkedit.data_in_code.size > UINT32_MAX || linkedit.symtab.size > UINT32_MAX ||
        linkedit.strtab.size > UINT32_MAX || linkedit.indirect.size > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit table exceeds Mach-O 32-bit size fields");
        goto cleanup;
    }
    uint64_t cursor = ctx->linkedit_fileoff;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.rebaseoff = (uint32_t) cursor;
    if (linkedit.rebase.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit data is too large");
        goto cleanup;
    }
    cursor += linkedit.rebase.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.bindoff = (uint32_t) cursor;
    if (linkedit.bind.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit data is too large");
        goto cleanup;
    }
    cursor += linkedit.bind.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.weak_bindoff = (uint32_t) cursor;
    if (linkedit.weak_bind.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "weak bind data is too large");
        goto cleanup;
    }
    cursor += linkedit.weak_bind.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.exportoff = (uint32_t) cursor;
    if (linkedit.exports.size > UINT32_MAX || linkedit.exports.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "export trie is too large");
        goto cleanup;
    }
    linkedit.exportsize = (uint32_t) linkedit.exports.size;
    cursor += linkedit.exports.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.symoff = (uint32_t) cursor;
    if (linkedit.symtab.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "symbol table is too large");
        goto cleanup;
    }
    cursor += linkedit.symtab.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.stroff = (uint32_t) cursor;
    if (linkedit.strtab.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "string table is too large");
        goto cleanup;
    }
    cursor += linkedit.strtab.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.indirectoff = (uint32_t) cursor;
    if (linkedit.indirect.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "indirect symbol table is too large");
        goto cleanup;
    }
    cursor += linkedit.indirect.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.function_startsoff = (uint32_t) cursor;
    if (linkedit.function_starts.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "function-start table is too large");
        goto cleanup;
    }
    cursor += linkedit.function_starts.size;
    cursor = ldd_align_up(cursor, 8);
    if (cursor > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "linkedit offset is too large");
        goto cleanup;
    }
    linkedit.data_in_codeoff = (uint32_t) cursor;
    if (linkedit.data_in_code.size > UINT64_MAX - cursor) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "data-in-code table is too large");
        goto cleanup;
    }
    cursor += linkedit.data_in_code.size;
    uint64_t signature_offset = ldd_align_up(cursor, 16);
    size_t signature_size = 0;
    if (ctx->options->adhoc_codesign &&
        !ldd_code_signature_size(signature_offset, strlen("a.out"), &signature_size)) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "code signature is too large");
        goto cleanup;
    }
    if (signature_size > UINT64_MAX - signature_offset) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "code signature is too large");
        goto cleanup;
    }
    uint64_t total_size = ldd_align_up(signature_offset + signature_size, LDD_PAGE_SIZE);
    if (total_size < signature_offset || total_size > SIZE_MAX || total_size > UINT32_MAX) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "output is too large");
        goto cleanup;
    }
    ldd_segment_layout_t *linkedit_segment = ldd_find_segment(ctx, "__LINKEDIT");
    if (!linkedit_segment || total_size < linkedit_segment->fileoff) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "invalid __LINKEDIT layout");
        goto cleanup;
    }
    linkedit_segment->filesize = ldd_align_up(total_size - linkedit_segment->fileoff, LDD_PAGE_SIZE);
    linkedit_segment->vmsize = linkedit_segment->filesize;
    ctx->linkedit_size = linkedit_segment->filesize;
    image = calloc(1, (size_t) total_size);
    if (!image) {
        result = ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating output image");
        goto cleanup;
    }
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        ldd_output_section_t *section = ctx->outputs.items[i];
        if (!section->zerofill && section->file_size) {
            memcpy(image + section->fileoff, section->data, (size_t) section->file_size);
        }
    }
    memcpy(image + linkedit.rebaseoff, linkedit.rebase.data, linkedit.rebase.size);
    memcpy(image + linkedit.bindoff, linkedit.bind.data, linkedit.bind.size);
    memcpy(image + linkedit.weak_bindoff, linkedit.weak_bind.data, linkedit.weak_bind.size);
    memcpy(image + linkedit.exportoff, linkedit.exports.data, linkedit.exports.size);
    memcpy(image + linkedit.symoff, linkedit.symtab.data, linkedit.symtab.size);
    memcpy(image + linkedit.stroff, linkedit.strtab.data, linkedit.strtab.size);
    memcpy(image + linkedit.indirectoff, linkedit.indirect.data, linkedit.indirect.size);
    memcpy(image + linkedit.function_startsoff, linkedit.function_starts.data, linkedit.function_starts.size);
    if (linkedit.data_in_code.size) {
        memcpy(image + linkedit.data_in_codeoff, linkedit.data_in_code.data, linkedit.data_in_code.size);
    }
    ldd_mach_header_64_t header = {0};
    header.magic = LDD_MH_MAGIC_64;
    header.cputype = LDD_CPU_TYPE_ARM64;
    header.cpusubtype = LDD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LDD_MH_EXECUTE;
    header.ncmds = (uint32_t) (ctx->segment_count + 11U + ldd_dylib_count(ctx) +
                               (ctx->options->adhoc_codesign ? 1U : 0U));
    header.sizeofcmds = (uint32_t) (ctx->header_size - sizeof(header));
    header.flags = LDD_MH_NOUNDEFS | LDD_MH_DYLDLINK | LDD_MH_TWOLEVEL;
    if (ctx->options->pie) header.flags |= LDD_MH_PIE;
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->weak && (symbol->kind == LDD_SYMBOL_DEFINED || symbol->kind == LDD_SYMBOL_COMMON)) {
            header.flags |= LDD_MH_WEAK_DEFINES;
        }
        if (symbol->weak_ref && symbol->kind == LDD_SYMBOL_IMPORT) {
            header.flags |= LDD_MH_BINDS_TO_WEAK;
        }
    }
    for (size_t i = 0; i < ctx->binds.count; i++) {
        if (ctx->binds.items[i].weak_definition) {
            header.flags |= LDD_MH_BINDS_TO_WEAK;
            break;
        }
    }
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        uint32_t type = ctx->outputs.items[i]->flags & LDD_SECTION_TYPE;
        if (type == LDD_S_THREAD_LOCAL_REGULAR || type == LDD_S_THREAD_LOCAL_ZEROFILL ||
            type == LDD_S_THREAD_LOCAL_VARIABLES || type == LDD_S_THREAD_LOCAL_VARIABLE_POINTERS ||
            type == LDD_S_THREAD_LOCAL_INIT_FUNCTION_POINTERS) {
            header.flags |= LDD_MH_HAS_TLV_DESCRIPTORS;
            break;
        }
    }
    memcpy(image, &header, sizeof(header));
    size_t command_end = ldd_write_load_commands(ctx, image, &linkedit, (uint32_t) signature_offset, (uint32_t) signature_size);
    if (command_end != ctx->header_size) {
        result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "internal load command size mismatch (%zu != %llu)", command_end,
                          (unsigned long long) ctx->header_size);
        goto cleanup;
    }
    if (ctx->options->adhoc_codesign) {
        ldd_segment_layout_t *text_segment = ldd_find_segment(ctx, "__TEXT");
        result = ldd_write_code_signature(image + signature_offset, image, signature_offset,
                                          text_segment->filesize, "a.out", signature_size);
        if (result != LDD_OK) {
            result = ldd_fail(ctx, LDD_OUTPUT_ERROR, "cannot create ad-hoc code signature");
            goto cleanup;
        }
    }
    result = ldd_write_output_file(ctx, image, (size_t) total_size);
cleanup:
    free(image);
    ldd_linkedit_deinit(&linkedit);
    return result;
}

static int ldd_finalize_symbols(ldd_context_t *ctx) {
    int objc_result = ldd_prepare_objc_selector_symbols(ctx);
    if (objc_result != LDD_OK) {
        return objc_result;
    }
    /* Framework flags are explicit requests.  Keep libSystem as ordinal 1 and
       route the Darwin framework symbol families to their corresponding
       two-level namespace ordinal. */
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->objc_selector_stub) {
            continue;
        }
        if (symbol->kind != LDD_SYMBOL_UNDEFINED && symbol->kind != LDD_SYMBOL_IMPORT) {
            continue;
        }
        const char *name = symbol->name ? symbol->name : "";
        for (size_t i = 0; i < ctx->dylibs.count; i++) {
            if (ldd_dylib_exports_symbol(&ctx->dylibs.items[i], name)) {
                size_t provider = i;
                if (ctx->dylibs.items[i].reexport_only &&
                    ctx->dylibs.items[i].has_reexport_owner &&
                    ctx->dylibs.items[i].reexport_owner < ctx->dylibs.count) {
                    provider = ctx->dylibs.items[i].reexport_owner;
                }
                symbol->dylib_ordinal = ldd_dylib_ordinal(ctx, provider);
                break;
            }
        }
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->objc_selector_stub || symbol->alias) {
            continue;
        }
        if (symbol->kind == LDD_SYMBOL_UNDEFINED) {
            if (!symbol->weak_ref && !ldd_any_dylib_exports_symbol(ctx, symbol->name)) {
                return ldd_fail(ctx, LDD_SYMBOL_ERROR, "undefined symbol '%s' referenced by '%s'%s%s",
                                symbol->name, symbol->object ? symbol->object->file->path : "<linker>",
                                symbol->object && symbol->object->member_name ? " member " : "",
                                symbol->object && symbol->object->member_name ? symbol->object->member_name : "");
            }
            symbol->kind = LDD_SYMBOL_IMPORT;
            symbol->dynamic = true;
        }
    }
    int alias_result = ldd_resolve_aliases(ctx);
    if (alias_result != LDD_OK) {
        return alias_result;
    }
    for (ldd_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->objc_selector_stub) {
            continue;
        }
        if (symbol->kind == LDD_SYMBOL_IMPORT) {
            symbol->dynamic = true;
            if (ldd_symbol_push(&ctx->dynamic_symbols, symbol) != LDD_OK) {
                return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting dynamic symbols");
            }
        }
    }
    return LDD_OK;
}

static uint64_t ldd_symbol_address(const ldd_symbol_t *symbol) {
    if (!symbol) {
        return 0;
    }
    if (symbol->kind == LDD_SYMBOL_ABSOLUTE) {
        return symbol->value;
    }
    if (symbol->output) {
        return symbol->output->addr + symbol->output_offset;
    }
    return symbol->value;
}

static uint64_t ldd_local_symbol_address(const ldd_input_symbol_t *input) {
    if (!input || !input->object) {
        return 0;
    }
    uint8_t type = input->entry.n_type & LDD_N_TYPE;
    if (type == LDD_N_ABS) {
        return input->entry.n_value;
    }
    if (type != LDD_N_SECT || input->entry.n_sect == 0 || input->entry.n_sect > input->object->section_count) {
        return 0;
    }
    ldd_input_section_t *section = &input->object->sections[input->entry.n_sect - 1U];
    if (!section->output) {
        return 0;
    }
    uint64_t relative = input->entry.n_value >= section->header.addr ? input->entry.n_value - section->header.addr : input->entry.n_value;
    return section->output->addr + section->output_offset + relative;
}

static ldd_symbol_t *ldd_relocation_symbol(ldd_context_t *ctx, ldd_object_t *object, const ldd_relocation_t *relocation) {
    if (!relocation->external || relocation->symbolnum >= object->symbol_count) {
        return NULL;
    }
    return ldd_symbol_for_input(ctx, object, relocation->symbolnum);
}

static uint64_t ldd_relocation_target(ldd_context_t *ctx, ldd_object_t *object, const ldd_relocation_t *relocation) {
    if (relocation->external) {
        ldd_symbol_t *symbol = ldd_relocation_symbol(ctx, object, relocation);
        if (symbol) {
            return ldd_symbol_address(symbol);
        }
        if (relocation->symbolnum < object->symbol_count) {
            return ldd_local_symbol_address(&object->symbols[relocation->symbolnum]);
        }
        return 0;
    }
    if (relocation->symbolnum == 0 || relocation->symbolnum > object->section_count) {
        return 0;
    }
    ldd_input_section_t *section = &object->sections[relocation->symbolnum - 1U];
    return section->output ? section->output->addr + section->output_offset : 0;
}

static uint64_t ldd_branch_target(ldd_context_t *ctx, ldd_object_t *object,
                                  const ldd_relocation_t *relocation) {
    ldd_symbol_t *symbol = ldd_relocation_symbol(ctx, object, relocation);
    if (symbol && symbol->kind == LDD_SYMBOL_IMPORT) {
        if (!ctx->stubs || symbol->stub_index == UINT32_MAX) return 0;
        return ctx->stubs->addr + (uint64_t) symbol->stub_index * 12U;
    }
    return ldd_relocation_target(ctx, object, relocation);
}

static bool ldd_branch26_delta(uint64_t target, uint64_t place, int64_t *delta) {
    if (target > (uint64_t) INT64_MAX || place > (uint64_t) INT64_MAX) return false;
    *delta = (int64_t) target - (int64_t) place;
    return (*delta & 3) == 0 && *delta >= -(128LL << 20) && *delta < (128LL << 20);
}

/* Zig's MachO.zig createThunks and MachO/Thunk.zig place an ADRP/ADD/BR
   trampoline within BRANCH26 reach of a callsite.  Nature currently emits a
   single compact island beside the other __TEXT stubs.  Both legs are
   checked below; very large text sections that require interleaved islands
   remain a diagnosed unsupported layout instead of being silently wrapped. */
static int ldd_scan_branch_thunks(ldd_context_t *ctx, bool *added) {
    *added = false;
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            ldd_input_section_t *section = &object->sections[section_index];
            if (section->ignored || !section->output || !section->relocations) continue;
            int64_t pending_addend = 0;
            bool has_pending_addend = false;
            for (uint32_t relocation_index = 0; relocation_index < section->header.nreloc;
                 relocation_index++) {
                const uint8_t *raw = section->relocations + (size_t) relocation_index * 8U;
                uint32_t second = ldd_read_u32(raw + 4);
                ldd_relocation_t relocation = {
                        .address = ldd_read_u32(raw),
                        .symbolnum = second & 0x00ffffffU,
                        .pcrel = (uint8_t) ((second >> 24U) & 1U),
                        .length = (uint8_t) ((second >> 25U) & 3U),
                        .external = (uint8_t) ((second >> 27U) & 1U),
                        .type = (uint8_t) ((second >> 28U) & 0xfU),
                };
                if (relocation.type == LDD_ARM64_RELOC_ADDEND) {
                    if (relocation_index + 1U >= section->header.nreloc) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "unpaired ARM64 addend relocation in '%s'", object->file->path);
                    }
                    const uint8_t *next_raw = section->relocations +
                                              (size_t) (relocation_index + 1U) * 8U;
                    uint32_t next_word = ldd_read_u32(next_raw + 4U);
                    uint8_t next_type = (uint8_t) ((next_word >> 28U) & 0xfU);
                    if (ldd_read_u32(next_raw) != relocation.address ||
                        !ldd_relocation_accepts_addend(next_type) || has_pending_addend) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "invalid ARM64 addend pair in '%s'", object->file->path);
                    }
                    pending_addend = ldd_sign_extend(relocation.symbolnum, 24U);
                    has_pending_addend = true;
                    continue;
                }
                if (relocation.type != LDD_ARM64_RELOC_BRANCH26) {
                    has_pending_addend = false;
                    continue;
                }

                int64_t branch_addend = has_pending_addend ? pending_addend : 0;
                has_pending_addend = false;

                uint64_t place = section->output->addr + section->output_offset + relocation.address;
                ldd_branch_thunk_t *existing = ldd_find_branch_thunk(
                        ctx, object, (uint32_t) section_index, relocation_index);
                if (existing) {
                    uint64_t island = ctx->branch_islands->addr + existing->output_offset;
                    int64_t island_delta;
                    if (!ldd_branch26_delta(island, place, &island_delta)) {
                        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                        "ARM64 branch island is out of range at offset 0x%x in '%s'%s%s",
                                        relocation.address, object->file->path,
                                        object->member_name ? " member " : "",
                                        object->member_name ? object->member_name : "");
                    }
                    continue;
                }

                uint64_t target = ldd_branch_target(ctx, object, &relocation);
                uint64_t adjusted_target;
                if (!ldd_add_signed_u64(target, branch_addend, &adjusted_target)) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                    "ARM64 branch relocation addend overflow in '%s'", object->file->path);
                }
                int64_t direct_delta;
                if (ldd_branch26_delta(adjusted_target, place, &direct_delta)) continue;
                if ((adjusted_target & 3U) != 0 || adjusted_target > (uint64_t) INT64_MAX) {
                    return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                    "ARM64 branch target is invalid at offset 0x%x in '%s'",
                                    relocation.address, object->file->path);
                }
                if (!ctx->branch_islands) {
                    ctx->branch_islands = ldd_get_output(
                            ctx, "__TEXT", "__branch_islands",
                            LDD_S_ATTR_PURE_INSTRUCTIONS | LDD_S_ATTR_SOME_INSTRUCTIONS, 2,
                            false);
                    if (!ctx->branch_islands) {
                        return ctx->error ? ctx->error
                                          : ldd_fail(ctx, LDD_IO_ERROR,
                                                     "out of memory creating branch islands");
                    }
                }
                ldd_branch_thunk_t thunk = {
                        .object = object,
                        .section_index = (uint32_t) section_index,
                        .relocation_index = relocation_index,
                        .output_offset = ctx->branch_islands->size,
                        .addend = branch_addend,
                };
                if (ldd_branch_thunk_push(&ctx->branch_thunks, thunk) != LDD_OK ||
                    ctx->branch_islands->size > UINT64_MAX - 12U) {
                    return ldd_fail(ctx, LDD_IO_ERROR, "out of memory recording branch island");
                }
                ctx->branch_islands->size += 12U;
                *added = true;
            }
        }
    }
    if (*added && ldd_output_reserve(ctx->branch_islands, ctx->branch_islands->size) != LDD_OK) {
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory allocating branch islands");
    }
    return LDD_OK;
}

static int ldd_write_branch_thunks(ldd_context_t *ctx) {
    for (size_t i = 0; i < ctx->branch_thunks.count; i++) {
        const ldd_branch_thunk_t *thunk = &ctx->branch_thunks.items[i];
        ldd_input_section_t *section = &thunk->object->sections[thunk->section_index];
        const uint8_t *raw = section->relocations + (size_t) thunk->relocation_index * 8U;
        uint32_t second = ldd_read_u32(raw + 4);
        ldd_relocation_t relocation = {
                .address = ldd_read_u32(raw),
                .symbolnum = second & 0x00ffffffU,
                .pcrel = (uint8_t) ((second >> 24U) & 1U),
                .length = (uint8_t) ((second >> 25U) & 3U),
                .external = (uint8_t) ((second >> 27U) & 1U),
                .type = (uint8_t) ((second >> 28U) & 0xfU),
        };
        uint64_t target = ldd_branch_target(ctx, thunk->object, &relocation);
        if (!ldd_add_signed_u64(target, thunk->addend, &target)) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "ARM64 branch island addend overflows in '%s'", thunk->object->file->path);
        }
        uint64_t island = ctx->branch_islands->addr + thunk->output_offset;
        if ((target & 3U) != 0 || target > (uint64_t) INT64_MAX || island > (uint64_t) INT64_MAX) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "ARM64 branch island target is invalid in '%s'", thunk->object->file->path);
        }
        int64_t page_delta = (int64_t) (target & ~0xfffULL) -
                             (int64_t) (island & ~0xfffULL);
        if (page_delta < -(1LL << 32) || page_delta >= (1LL << 32)) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "ARM64 branch island target is outside ADRP range in '%s'",
                            thunk->object->file->path);
        }
        uint8_t *code = ctx->branch_islands->data + thunk->output_offset;
        uint32_t adrp = ldd_patch_adrp(0x90000010U, page_delta);
        uint32_t add = 0x91000210U | (uint32_t) ((target & 0xfffULL) << 10U);
        ldd_write_u32(code, adrp);
        ldd_write_u32(code + 4U, add);
        ldd_write_u32(code + 8U, 0xd61f0200U);
    }
    return LDD_OK;
}

int ldd_link_macho(ldd_context_t *ctx) {
    int result = LDD_OK;
    for (size_t i = 0; i < ctx->objects.count; i++) {
        if (ctx->objects.items[i]->selected) {
            result = ldd_register_object_symbols(ctx, ctx->objects.items[i]);
            if (result != LDD_OK) {
                return result;
            }
        }
    }
    result = ldd_resolve_aliases(ctx);
    if (result != LDD_OK) {
        return result;
    }

    const char *requested_entry = ctx->options->entry_symbol ? ctx->options->entry_symbol : "runtime_main";
    int entry_length;
    if (strcmp(requested_entry, "runtime_main") == 0) {
        entry_length = snprintf(ctx->entry_name, sizeof(ctx->entry_name), "_main");
    } else if (requested_entry[0] == '_') {
        entry_length = snprintf(ctx->entry_name, sizeof(ctx->entry_name), "%s", requested_entry);
    } else {
        entry_length = snprintf(ctx->entry_name, sizeof(ctx->entry_name), "_%s", requested_entry);
    }
    if (entry_length < 0 || (size_t) entry_length >= sizeof(ctx->entry_name)) {
        return ldd_fail(ctx, LDD_INVALID_ARGUMENT, "entry symbol name is too long");
    }

    result = ldd_require_symbol(ctx, ctx->entry_name);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_resolve_archives(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_resolve_aliases(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_resolve_reexport_libraries(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_prepare_boundary_symbols(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_define_special(ctx, "__mh_execute_header", true, false);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_define_special(ctx, "___dso_handle", false, true);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_finalize_symbols(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_collect_sections(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_resolve_aliases(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_unwind_prepare(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_scan_relocations(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_layout_sections(ctx);
    if (result != LDD_OK) {
        return result;
    }
    bool added_branch_thunks;
    do {
        result = ldd_scan_branch_thunks(ctx, &added_branch_thunks);
        if (result != LDD_OK) {
            return result;
        }
        if (added_branch_thunks) {
            result = ldd_layout_sections(ctx);
            if (result != LDD_OK) {
                return result;
            }
        }
    } while (added_branch_thunks);
    result = ldd_write_branch_thunks(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_unwind_emit(ctx);
    if (result != LDD_OK) {
        return result;
    }
    result = ldd_apply_relocations(ctx);
    if (result != LDD_OK) {
        return result;
    }
    return ldd_emit_image(ctx);
}

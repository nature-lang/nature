#include "module_index.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "src/build/config.h"
#include "src/error.h"
#include "src/package.h"
#include "utils/helper.h"

/**
 * package_dir -> package_unit_t*
 */
static table_t *package_unit_table = NULL;

typedef struct {
    char *path; // absolute path
    source_variant_kind_t kind;
    uint8_t os; // 0 when SOURCE_VARIANT_PLAIN
    uint8_t arch; // 0 unless SOURCE_VARIANT_OS_ARCH
    char *mod_ident; // NULL means there is no mod declaration
} scan_variant_t;

typedef struct {
    char *slot_key; // absolute path with the target suffix stripped
    char *rel_dir; // directory path relative to the package, "" for the root
    char *base; // file name without the target suffix and .n
    slice_t *variants; // scan_variant_t*
} scan_slot_t;

static bool ident_is_valid(char *ident) {
    if (!ident || strlen(ident) == 0) {
        return false;
    }

    for (size_t i = 0; i < strlen(ident); ++i) {
        char c = ident[i];
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        bool number = c >= '0' && c <= '9';

        if (i == 0 && !alpha) {
            return false;
        }

        if (!alpha && !number) {
            return false;
        }
    }

    return true;
}

char *module_ident_join(char *package_name, char *module_path) {
    if (!module_path || strlen(module_path) == 0) {
        return package_name;
    }

    return str_connect_by(package_name, module_path, ".");
}

char *module_source_rel_path(char *package_dir, char *source_path) {
    char *rel = str_replace(source_path, package_dir, "");
    rel = ltrim(rel, "/");
    str_replace_char(rel, '\\', '/');
    return rel;
}

bool module_unit_has_source_named(module_unit_t *unit, char *name) {
    char *expect = str_connect(name, ".n");

    for (int i = 0; i < unit->sources->count; ++i) {
        char *source_path = unit->sources->take[i];
        char *basename = strrchr(source_path, '/');
        basename = basename ? basename + 1 : source_path;

        if (str_equal(basename, expect)) {
            return true;
        }
    }

    return false;
}

char *module_source_diag_path(char *package_dir, char *source_path) {
    // package dir maybe eqs /root
    char *parent = path_dir(package_dir);
    if (str_equal(parent, "") || str_equal(parent, "/")) {
        free(parent);
        return source_path;
    }

    char *rel = str_replace(source_path, parent, "");
    free(parent);
    rel = ltrim(rel, "/");
    str_replace_char(rel, '\\', '/');
    return rel;
}

/**
 * Read the leading mod declaration of a source file. Skips the UTF-8 BOM, blank lines and comments.
 * mod and the identifier after it must sit on the same line, matching parser_is_mod_decl in the parser.
 */
char *module_source_read_mod(char *source_path) {
    char *source = file_read(source_path);
    if (!source) {
        return NULL;
    }

    char *p = source;

    // UTF-8 BOM
    if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) {
        p += 3;
    }

    while (*p) {
        // whitespace and newlines
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
            continue;
        }

        // line comment
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n') {
                p++;
            }
            continue;
        }

        // block comment
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) {
                p++;
            }
            if (*p) {
                p += 2;
            }
            continue;
        }

        break;
    }

    if (strncmp(p, MOD_DECL_IDENT, strlen(MOD_DECL_IDENT)) != 0) {
        return NULL;
    }

    char *cursor = p + strlen(MOD_DECL_IDENT);

    // mod must be followed by a space or tab, otherwise this is just an identifier starting with mod
    if (*cursor != ' ' && *cursor != '\t') {
        return NULL;
    }

    // skip spaces and inline block comments; the scanner drops comments, so this must behave the same
    while (*cursor) {
        if (*cursor == ' ' || *cursor == '\t') {
            cursor++;
            continue;
        }

        if (cursor[0] == '/' && cursor[1] == '*') {
            char *end = strstr(cursor + 2, "*/");
            if (!end) {
                return NULL;
            }

            // a block comment spanning lines puts mod and ident on different lines, so this is not a module declaration
            for (char *scan = cursor; scan < end; ++scan) {
                if (*scan == '\n') {
                    return NULL;
                }
            }

            cursor = end + 2;
            continue;
        }

        break;
    }

    char *ident_start = cursor;
    while (*cursor && ((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
                       (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
        cursor++;
    }

    if (cursor == ident_start) {
        return NULL;
    }

    size_t length = (size_t) (cursor - ident_start);
    char *ident = mallocz(length + 1);
    memcpy(ident, ident_start, length);

    return ident;
}

/**
 * Parse the target suffix in a file name.
 * name.<os>_<arch>.n / name.<os>.n / name.n
 * base_out receives the file name without the suffix and .n
 */
static void source_parse_variant(char *filename, char **base_out, source_variant_kind_t *kind_out, uint8_t *os_out,
                                 uint8_t *arch_out) {
    assert(ends_with(filename, ".n"));

    char *stem = strdup(filename);
    stem[strlen(stem) - 2] = '\0'; // strip .n

    *kind_out = SOURCE_VARIANT_PLAIN;
    *os_out = 0;
    *arch_out = 0;

    char *last_dot = strrchr(stem, '.');
    if (last_dot != NULL) {
        char *suffix = last_dot + 1;

        char *underscore = strchr(suffix, '_');
        if (underscore != NULL) {
            size_t os_length = (size_t) (underscore - suffix);
            char *os_str = mallocz(os_length + 1);
            memcpy(os_str, suffix, os_length);

            uint8_t os = os_to_uint8(os_str);
            uint8_t arch = arch_to_uint8(underscore + 1);
            if (os != 0 && arch != 0) {
                *kind_out = SOURCE_VARIANT_OS_ARCH;
                *os_out = os;
                *arch_out = arch;
                *last_dot = '\0';
                *base_out = stem;
                return;
            }
        } else {
            uint8_t os = os_to_uint8(suffix);
            if (os != 0) {
                *kind_out = SOURCE_VARIANT_OS;
                *os_out = os;
                *last_dot = '\0';
                *base_out = stem;
                return;
            }
        }
    }

    *base_out = stem;
}

char *module_source_slot_key(char *source_path) {
    char *dir = path_dir(source_path);
    char *filename = strrchr(source_path, '/');
    filename = filename ? filename + 1 : source_path;

    if (!ends_with(filename, ".n")) {
        return NULL;
    }

    char *base = NULL;
    source_variant_kind_t kind;
    uint8_t os, arch;
    source_parse_variant(filename, &base, &kind, &os, &arch);

    char *key = path_join(strdup(dir), str_connect(base, ".n"));
    free(dir);
    return key;
}

static int scan_source_compare(const void *a, const void *b) {
    return strcmp(*(char **) a, *(char **) b);
}

static int scan_slot_compare(const void *a, const void *b) {
    scan_slot_t *left = *(scan_slot_t **) a;
    scan_slot_t *right = *(scan_slot_t **) b;
    return strcmp(left->slot_key, right->slot_key);
}

static int scan_unit_compare(const void *a, const void *b) {
    module_unit_t *left = *(module_unit_t **) a;
    module_unit_t *right = *(module_unit_t **) b;
    return strcmp(left->module_path, right->module_path);
}

/**
 * Recursively scan the package directory, collecting every .n file into logical source slots
 * stops at a nested package.toml, since a nested package owns a separate PackageInstanceId
 */
static void package_scan_dir(package_unit_t *pu, table_t *slot_table, slice_t *slots, char *dir, char *rel_dir) {
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }

    slice_t *entries = slice_new();
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (str_equal(entry->d_name, ".") || str_equal(entry->d_name, "..")) {
            continue;
        }

        // hidden directories and files are not scanned
        if (entry->d_name[0] == '.') {
            continue;
        }

        slice_push(entries, strdup(entry->d_name));
    }
    closedir(d);

    // directory enumeration order must not affect the result, so sort before processing
    qsort(entries->take, (size_t) entries->count, sizeof(entries->take[0]), scan_source_compare);

    for (int i = 0; i < entries->count; ++i) {
        char *name = entries->take[i];
        char *full_path = path_join(strdup(dir), name);

        if (dir_exists(full_path)) {
            char *nested_conf = path_join(strdup(full_path), PACKAGE_TOML);
            bool nested_package = file_exists(nested_conf);
            free(nested_conf);

            if (nested_package) {
                continue;
            }

            char *child_rel = strlen(rel_dir) == 0 ? strdup(name) : path_join(strdup(rel_dir), name);
            package_scan_dir(pu, slot_table, slots, full_path, child_rel);
            continue;
        }

        if (!ends_with(name, ".n")) {
            continue;
        }

        char *base = NULL;
        source_variant_kind_t kind;
        uint8_t os, arch;
        source_parse_variant(name, &base, &kind, &os, &arch);

        char *slot_key = path_join(strdup(dir), str_connect(base, ".n"));

        scan_slot_t *slot = table_get(slot_table, slot_key);
        if (!slot) {
            slot = NEW(scan_slot_t);
            slot->slot_key = slot_key;
            slot->rel_dir = rel_dir;
            slot->base = base;
            slot->variants = slice_new();
            table_set(slot_table, slot_key, slot);
            slice_push(slots, slot);
        }

        scan_variant_t *variant = NEW(scan_variant_t);
        variant->path = full_path;
        variant->kind = kind;
        variant->os = os;
        variant->arch = arch;
        variant->mod_ident = module_source_read_mod(full_path);

        slice_push(slot->variants, variant);
    }
}

/**
 * Check that a single variant's mod declaration matches its expected name
 */
static void package_check_mod_ident(package_unit_t *pu, scan_slot_t *slot, scan_variant_t *variant) {
    if (!variant->mod_ident) {
        return;
    }

    char *rel_path = module_source_diag_path(pu->package_dir, variant->path);

    if (!ident_is_valid(variant->mod_ident)) {
        dump_global_errorf(rel_path, 1, 1, "'mod %s' is not a valid identifier", variant->mod_ident);
        return;
    }

    if (strlen(slot->rel_dir) == 0) {
        // package root, checked against package.toml name, independent of the physical directory name
        if (!str_equal(variant->mod_ident, pu->package_name)) {
            dump_global_errorf(rel_path, 1, 1,
                               "'mod %s' does not match package name '%s', use 'mod %s'",
                               variant->mod_ident, pu->package_name, pu->package_name);
        }
        return;
    }

    // a normal subdirectory, checked against the current directory basename
    char *dir_name = strrchr(slot->rel_dir, '/');
    dir_name = dir_name ? dir_name + 1 : slot->rel_dir;

    if (!str_equal(variant->mod_ident, dir_name)) {
        dump_global_errorf(rel_path, 1, 1, "'mod %s' does not match directory '%s', use 'mod %s'",
                           variant->mod_ident, dir_name, dir_name);
    }
}

/**
 * All variants of one logical source slot must belong to the same module
 */
static void package_check_slot_membership(package_unit_t *pu, scan_slot_t *slot) {
    scan_variant_t *first = slot->variants->take[0];

    for (int i = 1; i < slot->variants->count; ++i) {
        scan_variant_t *variant = slot->variants->take[i];

        bool same = (first->mod_ident == NULL && variant->mod_ident == NULL) ||
                    (first->mod_ident != NULL && variant->mod_ident != NULL &&
                     str_equal(first->mod_ident, variant->mod_ident));
        if (same) {
            continue;
        }

        char *rel_path = module_source_diag_path(pu->package_dir, variant->path);
        char *first_rel_path = module_source_diag_path(pu->package_dir, first->path);

        dump_global_errorf(rel_path, 1, 1,
                           "target variants of '%s' must belong to the same module, but %s declares %s%s%s and %s declares %s%s%s",
                           slot->base, first_rel_path, first->mod_ident ? "'mod " : "no ",
                           first->mod_ident ? first->mod_ident : "mod", first->mod_ident ? "'" : "",
                           rel_path, variant->mod_ident ? "'mod " : "no ",
                           variant->mod_ident ? variant->mod_ident : "mod", variant->mod_ident ? "'" : "");
    }
}

/**
 * Select the active variant for the current target, NULL means the slot is inactive for it
 */
static scan_variant_t *package_select_variant(scan_slot_t *slot) {
    scan_variant_t *result = NULL;

    for (int i = 0; i < slot->variants->count; ++i) {
        scan_variant_t *variant = slot->variants->take[i];

        if (variant->kind == SOURCE_VARIANT_OS_ARCH) {
            if (variant->os != BUILD_OS || variant->arch != BUILD_ARCH) {
                continue;
            }
        } else if (variant->kind == SOURCE_VARIANT_OS) {
            if (variant->os != BUILD_OS) {
                continue;
            }
        }

        if (!result || variant->kind > result->kind) {
            result = variant;
        }
    }

    return result;
}

static char *module_unit_layout_desc(package_unit_t *pu, module_unit_t *unit) {
    char *first = unit->sources->count > 0 ? module_source_diag_path(pu->package_dir, unit->sources->take[0]) : "?";
    return dsprintf("%s (%s)", first, unit->is_dir_module ? "part of directory module" : "standalone file module");
}

static void package_unit_build(package_unit_t *pu) {
    table_t *slot_table = table_new();
    slice_t *slots = slice_new();

    package_scan_dir(pu, slot_table, slots, pu->package_dir, "");

    // slots are processed in slot key order so the result is independent of directory enumeration order
    qsort(slots->take, (size_t) slots->count, sizeof(slots->take[0]), scan_slot_compare);

    for (int i = 0; i < slots->count; ++i) {
        scan_slot_t *slot = slots->take[i];

        table_set(pu->slot_all, slot->slot_key, (void *) 1);

        // an inactive variant's body is not built, but its mod header still has to be checked
        for (int j = 0; j < slot->variants->count; ++j) {
            package_check_mod_ident(pu, slot, slot->variants->take[j]);
        }

        package_check_slot_membership(pu, slot);

        scan_variant_t *active = package_select_variant(slot);
        if (!active) {
            continue;
        }

        table_set(pu->slot_active, slot->slot_key, active->path);

        char *module_path;
        bool is_dir_module;
        if (active->mod_ident) {
            module_path = str_replace(strdup(slot->rel_dir), "/", ".");
            is_dir_module = true;
        } else {
            module_path = strlen(slot->rel_dir) == 0
                                  ? strdup(slot->base)
                                  : str_connect_by(str_replace(strdup(slot->rel_dir), "/", "."), slot->base, ".");
            is_dir_module = false;
        }

        module_unit_t *unit = table_get(pu->module_index, module_path);
        if (unit) {
            // only an identical full ModuleId is a conflict; a file module and a directory module sharing a name is one
            if (!unit->is_dir_module || !is_dir_module) {
                module_unit_t *conflict = NEW(module_unit_t);
                conflict->module_path = module_path;
                conflict->module_ident = module_ident_join(pu->package_name, module_path);
                conflict->is_dir_module = is_dir_module;
                conflict->sources = slice_new();
                slice_push(conflict->sources, active->path);

                char *rel_path = module_source_diag_path(pu->package_dir, active->path);
                dump_global_errorf(rel_path, 1, 1, "module %s is defined by two layouts: %s and %s",
                                   conflict->module_ident, module_unit_layout_desc(pu, unit),
                                   module_unit_layout_desc(pu, conflict));
                continue;
            }

            slice_push(unit->sources, active->path);
            table_set(pu->slot_index, slot->slot_key, unit);
            continue;
        }

        unit = NEW(module_unit_t);
        unit->module_path = module_path;
        unit->module_ident = module_ident_join(pu->package_name, module_path);
        unit->is_dir_module = is_dir_module;
        unit->sources = slice_new();
        unit->package_dir = pu->package_dir;
        unit->package_conf = pu->package_conf;
        slice_push(unit->sources, active->path);

        table_set(pu->module_index, module_path, unit);
        table_set(pu->slot_index, slot->slot_key, unit);
        slice_push(pu->units, unit);
    }

    // source parts within a module are sorted by in-package relative path in UTF-8 byte order
    for (int i = 0; i < pu->units->count; ++i) {
        module_unit_t *unit = pu->units->take[i];
        qsort(unit->sources->take, (size_t) unit->sources->count, sizeof(unit->sources->take[0]),
              scan_source_compare);
    }

    qsort(pu->units->take, (size_t) pu->units->count, sizeof(pu->units->take[0]), scan_unit_compare);
}

void package_unit_reset() {
    package_unit_table = NULL;
}

package_unit_t *package_unit_load(char *package_dir, toml_table_t *package_conf) {
    assert(package_dir);

    if (!package_unit_table) {
        package_unit_table = table_new();
    }

    package_unit_t *pu = table_get(package_unit_table, package_dir);
    if (pu) {
        return pu;
    }

    assertf(package_conf, "package.toml not found in '%s'", package_dir);

    toml_datum_t name = toml_string_in(package_conf, "name");
    assertf(name.ok, "package.toml in '%s' must have a 'name' field", package_dir);

    // package.toml.entry has been removed, there is no replacement field
    toml_datum_t entry = toml_string_in(package_conf, "entry");
    if (entry.ok) {
        char *conf_path = module_source_diag_path(package_dir, path_join(strdup(package_dir), PACKAGE_TOML));
        dump_global_errorf(conf_path, 1, 1,
                           "package.toml field 'entry' is not supported, remove it and pass the source path to 'nature build'");
    }

    pu = NEW(package_unit_t);
    pu->package_dir = package_dir;
    pu->package_name = name.u.s;
    pu->package_conf = package_conf;
    pu->module_index = table_new();
    pu->slot_index = table_new();
    pu->slot_active = table_new();
    pu->slot_all = table_new();
    pu->units = slice_new();

    // register before building to avoid recursively loading the same package
    table_set(package_unit_table, package_dir, pu);

    package_unit_build(pu);

    return pu;
}

module_unit_t *package_unit_find_module(package_unit_t *pu, char *module_path) {
    return table_get(pu->module_index, module_path);
}

module_unit_t *package_unit_find_source(package_unit_t *pu, char *source_path) {
    char *slot_key = module_source_slot_key(source_path);
    if (!slot_key) {
        return NULL;
    }

    return table_get(pu->slot_index, slot_key);
}

char *package_unit_slot_active(package_unit_t *pu, char *source_path) {
    char *slot_key = module_source_slot_key(source_path);
    if (!slot_key) {
        return NULL;
    }

    return table_get(pu->slot_active, slot_key);
}

bool package_unit_slot_exists(package_unit_t *pu, char *source_path) {
    char *slot_key = module_source_slot_key(source_path);
    if (!slot_key) {
        return false;
    }

    return table_exist(pu->slot_all, slot_key);
}

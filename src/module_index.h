#ifndef NATURE_MODULE_INDEX_H
#define NATURE_MODULE_INDEX_H

#include "utils/slice.h"
#include "utils/table.h"
#include "utils/toml.h"

// mod is a contextual keyword, only a file-leading `mod <ident>` is a module declaration
#define MOD_DECL_IDENT "mod"

/**
 * ModuleUnit: one logical module, made up of one or more source parts
 * an empty module_path means the package root module
 */
typedef struct module_unit_t {
    char *module_path; // "" | "utils" | "net.http"
    char *module_ident; // <package_name>[.<module_path>], user-facing logical name
    char *module_key; // package-instance-qualified symbol table prefix
    char *mod_ident; // expected leading mod, NULL only for standalone and single-file modules
    char *canonical_path; // same-named source of a directory module
    bool is_dir_module; // true means the module is named by its directory
    slice_t *sources; // char*, absolute paths of active sources, sorted by in-package relative path (UTF-8 byte order)
    char *package_dir;
    toml_table_t *package_conf;
} module_unit_t;

/**
 * PackageUnit: the module index of one package instance
 */
typedef struct {
    char *package_dir; // absolute path, without trailing /
    char *package_name; // package.toml name
    toml_table_t *package_conf;
    table_t *module_index; // module_path -> module_unit_t*
    table_t *slot_index; // slot key (absolute path with the target suffix stripped) -> module_unit_t*
    table_t *slot_active; // slot key -> char*, absolute path of the source active for the current target
    table_t *slot_all; // slot key -> (void*) 1, includes slots inactive for the current target
    slice_t *units; // module_unit_t*
} package_unit_t;

/**
 * Load (cached) the package module index, package_dir must contain a package.toml
 */
package_unit_t *package_unit_load(char *package_dir, toml_table_t *package_conf);

void package_unit_reset();

module_unit_t *package_unit_find_module(package_unit_t *pu, char *module_path);

/**
 * Map any source path to the module owning its logical source slot
 * source_path may be a plain path or carry a target suffix
 * returns NULL when the path belongs to no slot of the current package
 */
module_unit_t *package_unit_find_source(package_unit_t *pu, char *source_path);

/**
 * Return the source path active for the current target, or NULL when the slot is missing or inactive
 */
char *package_unit_slot_active(package_unit_t *pu, char *source_path);

/**
 * Whether source_path belongs to any logical source slot of the current package (including slots inactive for the current target)
 */
bool package_unit_slot_exists(package_unit_t *pu, char *source_path);

/**
 * Compute the slot key: the canonical path with the target suffix stripped
 */
char *module_source_slot_key(char *source_path);

/**
 * The path of source_path relative to package_dir, always using /
 */
char *module_source_rel_path(char *package_dir, char *source_path);

/**
 * Whether unit contains a source part whose basename is <name>.n
 */
// nature source extensions, .n compiles in normal mode and .x in x mode
#define SOURCE_EXT_N ".n"
#define SOURCE_EXT_X ".x"

// returns SOURCE_EXT_N / SOURCE_EXT_X for a nature source file, NULL otherwise
char *module_source_ext(char *filename);

bool module_unit_has_source_named(module_unit_t *unit, char *name);

/**
 * Render module_path in import form: "" -> P, "net.http" -> P.net.http
 */
char *module_ident_join(char *package_name, char *module_path);

char *module_key_join(char *package_dir, char *module_path);

/** Replace internal package-qualified keys in a diagnostic with logical module names. */
char *module_keys_display(char *message);

#endif // NATURE_MODULE_INDEX_H

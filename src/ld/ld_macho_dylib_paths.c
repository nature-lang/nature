#include "ld_macho_dylib_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __WINDOWS
#include <direct.h>
#endif

static bool ld_path_has_prefix(const char *path, const char *prefix) {
    size_t length = strlen(prefix);
    if (strncmp(path, prefix, length) != 0) return false;
    return path[length] == '\0' || path[length] == '/' ||
           path[length] == '\\';
}

static bool ld_path_is_separator(char value) {
    return value == '/' || value == '\\';
}

static int ld_checked_path_push(ld_macho_checked_paths_t *paths,
                                const char *path) {
    for (size_t i = 0; i < paths->count; i++) {
        if (strcmp(paths->items[i], path) == 0) return LD_OK;
    }
    if (paths->count == paths->capacity) {
        if (paths->capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = paths->capacity ? paths->capacity * 2U : 16U;
        char **items = ld_realloc_array(paths->items, paths->capacity, next,
                                        sizeof(*items));
        if (!items) return LD_IO_ERROR;
        paths->items = items;
        paths->capacity = next;
    }
    paths->items[paths->count] = strdup(path);
    if (!paths->items[paths->count]) return LD_IO_ERROR;
    paths->count++;
    return LD_OK;
}

void ld_macho_checked_paths_deinit(ld_macho_checked_paths_t *paths) {
    if (!paths) return;
    for (size_t i = 0; i < paths->count; i++) free(paths->items[i]);
    free(paths->items);
    memset(paths, 0, sizeof(*paths));
}

static int ld_path_join(const char *directory, const char *suffix,
                        char output[PATH_MAX]) {
    if (!directory || !*directory || !suffix) return LD_INVALID_ARGUMENT;
    size_t directory_length = strlen(directory);
    size_t suffix_offset = 0;
    bool directory_has_slash =
            ld_path_is_separator(directory[directory_length - 1U]);
    bool suffix_has_slash = ld_path_is_separator(suffix[0]);
    if (directory_has_slash && suffix_has_slash) suffix_offset = 1U;
    bool need_slash = !directory_has_slash && !suffix_has_slash && *suffix;
    size_t suffix_length = strlen(suffix + suffix_offset);
    size_t separator_length = need_slash ? 1U : 0U;
    if (directory_length > PATH_MAX - 1U ||
        separator_length > PATH_MAX - 1U - directory_length ||
        suffix_length >
                PATH_MAX - 1U - directory_length - separator_length) {
        return LD_INVALID_INPUT;
    }
    memcpy(output, directory, directory_length);
    size_t offset = directory_length;
    if (need_slash) output[offset++] = '/';
    memcpy(output + offset, suffix + suffix_offset, suffix_length + 1U);
    return LD_OK;
}

static int ld_path_dirname(const char *path, char output[PATH_MAX]) {
    if (!path || !*path) return LD_INVALID_ARGUMENT;
    size_t length = strlen(path);
    if (length >= PATH_MAX) return LD_INVALID_INPUT;
    while (length > 1U && ld_path_is_separator(path[length - 1U])) length--;
    const char *slash = NULL;
    for (size_t i = length; i > 0; i--) {
        if (ld_path_is_separator(path[i - 1U])) {
            slash = path + i - 1U;
            break;
        }
    }
    if (!slash) {
        memcpy(output, ".", 2U);
        return LD_OK;
    }
    size_t directory_length = (size_t) (slash - path);
    if (directory_length == 0U) directory_length = 1U;
    memcpy(output, path, directory_length);
    output[directory_length] = '\0';
    return LD_OK;
}

static int ld_path_stem(const char *path, char stem[PATH_MAX]) {
    const char *basename = path;
    for (const char *cursor = path; *cursor; ++cursor) {
        if (ld_path_is_separator(*cursor)) basename = cursor + 1U;
    }
    if (!*basename) return LD_INVALID_INPUT;
    const char *dot = strrchr(basename, '.');
    size_t length = dot && dot != basename ? (size_t) (dot - basename)
                                           : strlen(basename);
    if (length == 0U || length >= PATH_MAX) return LD_INVALID_INPUT;
    memcpy(stem, basename, length);
    stem[length] = '\0';
    return LD_OK;
}

static int ld_record_and_access(ld_macho_checked_paths_t *checked,
                                const char *candidate,
                                char resolved[PATH_MAX], bool *found) {
    int result = ld_checked_path_push(checked, candidate);
    if (result != LD_OK) return result;
    if (access(candidate, R_OK) != 0) return LD_OK;
    char canonical[PATH_MAX];
#ifdef __WINDOWS
    const char *value = candidate;
    if (_fullpath(canonical, candidate, PATH_MAX)) {
        for (char *cursor = canonical; *cursor; ++cursor) {
            if (*cursor == '\\') *cursor = '/';
        }
        value = canonical;
    }
#else
    const char *value = realpath(candidate, canonical) ? canonical : candidate;
#endif
    size_t length = strlen(value);
    if (length >= PATH_MAX) return LD_INVALID_INPUT;
    memcpy(resolved, value, length + 1U);
    *found = true;
    return LD_OK;
}

static int ld_try_path_variants(ld_macho_checked_paths_t *checked,
                                const char *path, char resolved[PATH_MAX],
                                bool *found) {
    static const char *const extensions[] = {".tbd", ".dylib", ""};
    char base[PATH_MAX];
    size_t length = strlen(path);
    if (length >= sizeof(base)) return LD_INVALID_INPUT;
    memcpy(base, path, length + 1U);
    char *basename = base;
    for (char *cursor = base; *cursor; ++cursor) {
        if (ld_path_is_separator(*cursor)) basename = cursor + 1U;
    }
    char *dot = strrchr(basename, '.');
    if (dot && dot != basename) *dot = '\0';

    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        char candidate[PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%s%s", base,
                               extensions[i]);
        if (written < 0 || (size_t) written >= sizeof(candidate)) {
            return LD_INVALID_INPUT;
        }
        int result = ld_record_and_access(checked, candidate, resolved, found);
        if (result != LD_OK || *found) return result;
    }
    return LD_OK;
}

static int ld_try_framework_directory(ld_macho_checked_paths_t *checked,
                                      const char *directory,
                                      const char *stem,
                                      char resolved[PATH_MAX], bool *found) {
    static const char *const layouts[] = {
            "%s/%s.framework/%s",
            "%s/%s.framework/Versions/A/%s",
            "%s/%s.framework/Versions/C/%s",
    };
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        char candidate[PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), layouts[i],
                               directory, stem, stem);
        if (written < 0 || (size_t) written >= sizeof(candidate)) {
            return LD_INVALID_INPUT;
        }
        int result = ld_try_path_variants(checked, candidate, resolved, found);
        if (result != LD_OK || *found) return result;
    }
    return LD_OK;
}

static int ld_try_library_directory(ld_macho_checked_paths_t *checked,
                                    const char *directory, const char *stem,
                                    char resolved[PATH_MAX], bool *found) {
    const char *name = strncmp(stem, "lib", 3U) == 0 && stem[3] ? stem + 3U
                                                                : stem;
    char candidate[PATH_MAX];
    int written = snprintf(candidate, sizeof(candidate), "%s/lib%s", directory,
                           name);
    if (written < 0 || (size_t) written >= sizeof(candidate)) {
        return LD_INVALID_INPUT;
    }
    return ld_try_path_variants(checked, candidate, resolved, found);
}

static int ld_try_sdk_search_root(ld_macho_checked_paths_t *checked,
                                  const char *directory,
                                  const char *root_marker,
                                  const char *install_name,
                                  char resolved[PATH_MAX], bool *found) {
    if (!directory || !*directory) return LD_OK;
    size_t directory_length = strlen(directory);
    while (directory_length > 1U &&
           ld_path_is_separator(directory[directory_length - 1U])) {
        directory_length--;
    }
    size_t marker_length = strlen(root_marker);
    if (directory_length < marker_length ||
        memcmp(directory + directory_length - marker_length, root_marker,
               marker_length) != 0) {
        return LD_OK;
    }
    size_t root_length = directory_length - marker_length;
    size_t install_length = strlen(install_name);
    if (root_length > PATH_MAX - 1U ||
        install_length > PATH_MAX - 1U - root_length) {
        return LD_INVALID_INPUT;
    }
    char candidate[PATH_MAX];
    memcpy(candidate, directory, root_length);
    memcpy(candidate + root_length, install_name, install_length + 1U);
    return ld_try_path_variants(checked, candidate, resolved, found);
}

static int ld_output_directory(const ld_context_t *ctx,
                               char directory[PATH_MAX]) {
    const char *output = ctx->options->output_path;
    if (!output || !*output) output = ".";
    return ld_path_dirname(output, directory);
}

static int ld_expand_rpath(const ld_context_t *ctx,
                           const ld_dylib_input_t *parent,
                           const char *rpath, bool executable_rpath,
                           char expanded[PATH_MAX], bool *sysrootable) {
    static const char loader[] = "@loader_path";
    static const char executable[] = "@executable_path";
    *sysrootable = false;
    const char *suffix = NULL;
    char directory[PATH_MAX];
    int result;
    if (strncmp(rpath, loader, sizeof(loader) - 1U) == 0 &&
        (rpath[sizeof(loader) - 1U] == '\0' ||
         rpath[sizeof(loader) - 1U] == '/')) {
        suffix = rpath + sizeof(loader) - 1U;
        result = executable_rpath
                         ? ld_output_directory(ctx, directory)
                         : ld_path_dirname(parent->path, directory);
        if (result != LD_OK) return result;
        return ld_path_join(directory, suffix, expanded);
    }
    if (strncmp(rpath, executable, sizeof(executable) - 1U) == 0 &&
        (rpath[sizeof(executable) - 1U] == '\0' ||
         rpath[sizeof(executable) - 1U] == '/')) {
        suffix = rpath + sizeof(executable) - 1U;
        result = ld_output_directory(ctx, directory);
        if (result != LD_OK) return result;
        return ld_path_join(directory, suffix, expanded);
    }
    if (strncmp(rpath, "@rpath", sizeof("@rpath") - 1U) == 0 &&
        (rpath[sizeof("@rpath") - 1U] == '\0' ||
         rpath[sizeof("@rpath") - 1U] == '/')) {
        return LD_UNSUPPORTED;
    }
    size_t length = strlen(rpath);
    if (length >= PATH_MAX) return LD_INVALID_INPUT;
    memcpy(expanded, rpath, length + 1U);
    *sysrootable = rpath[0] == '/';
    return LD_OK;
}

static int ld_try_one_rpath(ld_context_t *ctx,
                            const ld_dylib_input_t *parent,
                            const char *rpath, const char *suffix,
                            bool executable_rpath,
                            ld_macho_checked_paths_t *checked,
                            char resolved[PATH_MAX], bool *found) {
    char expanded[PATH_MAX];
    bool sysrootable = false;
    int result = ld_expand_rpath(ctx, parent, rpath, executable_rpath,
                                 expanded, &sysrootable);
    if (result == LD_UNSUPPORTED) return LD_OK;
    if (result != LD_OK) return result;
    char candidate[PATH_MAX];
    result = ld_path_join(expanded, suffix, candidate);
    if (result != LD_OK) return result;

    const char *sysroot = ctx->options->sysroot;
    if (sysrootable && sysroot && *sysroot &&
        !ld_path_has_prefix(candidate, sysroot)) {
        char rooted[PATH_MAX];
        result = ld_path_join(sysroot, candidate, rooted);
        if (result != LD_OK) return result;
        result = ld_try_path_variants(checked, rooted, resolved, found);
        if (result != LD_OK || *found) return result;
    }
    return ld_try_path_variants(checked, candidate, resolved, found);
}

int ld_macho_resolve_reexport_path(
        ld_context_t *ctx, const ld_dylib_input_t *parent,
        const char *install_name, char resolved[PATH_MAX], bool *found,
        ld_macho_checked_paths_t *checked) {
    if (!ctx || !ctx->options || !parent || !parent->path || !install_name ||
        !*install_name || !resolved || !found || !checked) {
        return LD_INVALID_ARGUMENT;
    }
    *found = false;
    resolved[0] = '\0';

    char stem[PATH_MAX];
    int result = ld_path_stem(install_name, stem);
    if (result != LD_OK) return result;
    for (size_t i = 0; i < ctx->options->framework_paths.count; i++) {
        result = ld_try_framework_directory(
                checked, ctx->options->framework_paths.items[i], stem,
                resolved, found);
        if (result != LD_OK || *found) return result;
    }
    for (size_t i = 0; i < ctx->options->library_paths.count; i++) {
        result = ld_try_library_directory(
                checked, ctx->options->library_paths.items[i], stem, resolved,
                found);
        if (result != LD_OK || *found) return result;
    }

    if (install_name[0] == '/') {
        static const char framework_root[] =
                "/System/Library/Frameworks";
        static const char library_root[] = "/usr/lib";
        for (size_t i = 0; i < ctx->options->framework_paths.count; i++) {
            result = ld_try_sdk_search_root(
                    checked, ctx->options->framework_paths.items[i],
                    framework_root, install_name, resolved, found);
            if (result != LD_OK || *found) return result;
        }
        for (size_t i = 0; i < ctx->options->library_paths.count; i++) {
            result = ld_try_sdk_search_root(
                    checked, ctx->options->library_paths.items[i],
                    library_root, install_name, resolved, found);
            if (result != LD_OK || *found) return result;
        }
        const char *sysroot = ctx->options->sysroot;
        if (sysroot && *sysroot) {
            char rooted[PATH_MAX];
            result = ld_path_join(sysroot, install_name, rooted);
            if (result != LD_OK) return result;
            result = ld_try_path_variants(checked, rooted, resolved, found);
        } else {
            result = ld_try_path_variants(checked, install_name, resolved,
                                          found);
        }
        return result;
    }

    static const char rpath_prefix[] = "@rpath/";
    static const char loader_prefix[] = "@loader_path";
    static const char executable_prefix[] = "@executable_path";
    if (strncmp(install_name, rpath_prefix, sizeof(rpath_prefix) - 1U) == 0) {
        const char *suffix = install_name + sizeof(rpath_prefix) - 1U;
        const ld_dylib_input_t *umbrella = parent;
        if (parent->has_reexport_owner &&
            parent->reexport_owner < ctx->dylibs.count) {
            umbrella = &ctx->dylibs.items[parent->reexport_owner];
        }
        for (size_t i = 0; i < umbrella->rpath_count; i++) {
            result = ld_try_one_rpath(ctx, parent, umbrella->rpaths[i], suffix,
                                      false, checked, resolved, found);
            if (result != LD_OK || *found) return result;
        }
        if (umbrella != parent) {
            for (size_t i = 0; i < parent->rpath_count; i++) {
                result = ld_try_one_rpath(
                        ctx, parent, parent->rpaths[i], suffix, false, checked,
                        resolved, found);
                if (result != LD_OK || *found) return result;
            }
        }
        for (size_t i = 0; i < ctx->options->rpaths.count; i++) {
            result = ld_try_one_rpath(
                    ctx, parent, ctx->options->rpaths.items[i], suffix, true,
                    checked, resolved, found);
            if (result != LD_OK || *found) return result;
        }
    } else if (strncmp(install_name, loader_prefix,
                       sizeof(loader_prefix) - 1U) == 0 &&
               (install_name[sizeof(loader_prefix) - 1U] == '\0' ||
                install_name[sizeof(loader_prefix) - 1U] == '/')) {
        char directory[PATH_MAX];
        result = ld_path_dirname(parent->path, directory);
        if (result != LD_OK) return result;
        result = ld_path_join(directory,
                              install_name + sizeof(loader_prefix) - 1U,
                              stem);
        if (result != LD_OK) return result;
        result = ld_try_path_variants(checked, stem, resolved, found);
        if (result != LD_OK || *found) return result;
    } else if (strncmp(install_name, executable_prefix,
                       sizeof(executable_prefix) - 1U) == 0 &&
               (install_name[sizeof(executable_prefix) - 1U] == '\0' ||
                install_name[sizeof(executable_prefix) - 1U] == '/')) {
        char directory[PATH_MAX];
        result = ld_output_directory(ctx, directory);
        if (result != LD_OK) return result;
        result = ld_path_join(directory,
                              install_name + sizeof(executable_prefix) - 1U,
                              stem);
        if (result != LD_OK) return result;
        result = ld_try_path_variants(checked, stem, resolved, found);
        if (result != LD_OK || *found) return result;
    }

    if (!*found) {
        result = ld_try_path_variants(checked, install_name, resolved, found);
    }
    return result;
}

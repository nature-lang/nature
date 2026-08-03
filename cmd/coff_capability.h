#ifndef NATURE_CMD_COFF_CAPABILITY_H
#define NATURE_CMD_COFF_CAPABILITY_H

#include "src/ld/ld_coff_capability.h"
#include "src/ld/ld_output.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} coff_capability_paths_t;

static int coff_capability_path_compare(const void *left_value,
                                        const void *right_value) {
    const char *left = *(const char *const *) left_value;
    const char *right = *(const char *const *) right_value;
    return strcmp(left, right);
}

static bool coff_capability_input_name(const char *name) {
    size_t length = strlen(name);
    static const char *suffixes[] = {".obj", ".a", ".lib"};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(*suffixes); i++) {
        size_t suffix_length = strlen(suffixes[i]);
        if (length >= suffix_length &&
            strcmp(name + length - suffix_length, suffixes[i]) == 0)
            return true;
    }
    return false;
}

static bool coff_capability_path_push(coff_capability_paths_t *paths,
                                      const char *name) {
    if (paths->count == paths->capacity) {
        size_t next = paths->capacity ? paths->capacity * 2U : 32U;
        if (next < paths->capacity || next > SIZE_MAX / sizeof(*paths->items))
            return false;
        char **items = realloc(paths->items, next * sizeof(*items));
        if (!items) return false;
        paths->items = items;
        paths->capacity = next;
    }
    paths->items[paths->count] = strdup(name);
    if (!paths->items[paths->count]) return false;
    paths->count++;
    return true;
}

static void coff_capability_paths_deinit(coff_capability_paths_t *paths) {
    for (size_t i = 0; i < paths->count; i++) free(paths->items[i]);
    free(paths->items);
    memset(paths, 0, sizeof(*paths));
}

static bool coff_capability_path_is_absolute(const char *path) {
    if (!path || path[0] == '\0') return false;
    if (path[0] == '/') return true;
#ifdef __WINDOWS
    if (path[0] == '\\') return true;
    bool drive_letter = (path[0] >= 'A' && path[0] <= 'Z') ||
                        (path[0] >= 'a' && path[0] <= 'z');
    return drive_letter && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
#else
    return false;
#endif
}

static bool coff_capability_absolute_path(const char *cwd, const char *path,
                                          char result[PATH_MAX]) {
    if (coff_capability_path_is_absolute(path)) {
        int length = snprintf(result, PATH_MAX, "%s", path);
        return length > 0 && length < PATH_MAX;
    }
    int length = snprintf(result, PATH_MAX, "%s/%s", cwd, path);
    return length > 0 && length < PATH_MAX;
}

static int cmd_coff_capabilities(int argc, char **argv) {
    const char *output = NULL;
    const char *sysroot = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 ||
            strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "coff-capabilities: missing output path\n");
                return 2;
            }
            output = argv[i];
            continue;
        }
        if (sysroot) {
            fprintf(stderr,
                    "coff-capabilities: expected one sysroot directory\n");
            return 2;
        }
        sysroot = argv[i];
    }
    if (!sysroot) {
        fprintf(stderr,
                "usage: nature coff-capabilities [-o manifest.json] "
                "<windows-sysroot>\n");
        return 2;
    }

    char original_cwd[PATH_MAX];
    if (!getcwd(original_cwd, sizeof(original_cwd))) {
        fprintf(stderr,
                "coff-capabilities: cannot read current directory: %s\n",
                strerror(errno));
        return 1;
    }
    char sysroot_path[PATH_MAX];
    char output_path[PATH_MAX];
    if (!coff_capability_absolute_path(original_cwd, sysroot, sysroot_path) ||
        (output && !coff_capability_absolute_path(original_cwd, output,
                                                  output_path))) {
        fprintf(stderr, "coff-capabilities: path is too long\n");
        return 1;
    }

    DIR *directory = opendir(sysroot_path);
    if (!directory) {
        fprintf(stderr, "coff-capabilities: cannot open '%s': %s\n", sysroot,
                strerror(errno));
        return 1;
    }
    coff_capability_paths_t paths = {0};
    struct dirent *entry;
    int status = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (!coff_capability_input_name(entry->d_name)) continue;
        if (!coff_capability_path_push(&paths, entry->d_name)) {
            fprintf(stderr, "coff-capabilities: out of memory\n");
            status = 1;
            break;
        }
    }
    if (closedir(directory) != 0 && status == 0) {
        fprintf(stderr, "coff-capabilities: cannot close '%s': %s\n", sysroot,
                strerror(errno));
        status = 1;
    }
    if (status != 0) {
        coff_capability_paths_deinit(&paths);
        return status;
    }
    if (paths.count == 0U) {
        fprintf(stderr, "coff-capabilities: '%s' has no COFF inputs\n",
                sysroot);
        coff_capability_paths_deinit(&paths);
        return 1;
    }
    qsort(paths.items, paths.count, sizeof(*paths.items),
          coff_capability_path_compare);

    if (chdir(sysroot_path) != 0) {
        fprintf(stderr, "coff-capabilities: cannot enter '%s': %s\n", sysroot,
                strerror(errno));
        coff_capability_paths_deinit(&paths);
        return 1;
    }
    char error[4096] = {0};
    ld_coff_capability_manifest_t manifest = {0};
    int scan_status = ld_coff_capability_scan_json(
            (const char *const *) paths.items, paths.count, &manifest, error,
            sizeof(error));
    int restore_status = chdir(original_cwd);
    coff_capability_paths_deinit(&paths);
    if (restore_status != 0) {
        fprintf(stderr,
                "coff-capabilities: cannot restore current directory: %s\n",
                strerror(errno));
        ld_coff_capability_manifest_deinit(&manifest);
        return 1;
    }
    if (scan_status != LD_OK) {
        fprintf(stderr, "coff-capabilities: %s\n",
                error[0] ? error : "scan failed");
        ld_coff_capability_manifest_deinit(&manifest);
        return 1;
    }

    if (output) {
        ld_options_t options;
        ld_options_init(&options);
        int write_status = ld_write_file_atomic(
                &options, output_path, (const uint8_t *) manifest.json,
                manifest.size, false);
        ld_options_deinit(&options);
        if (write_status != LD_OK) status = 1;
    } else if (fwrite(manifest.json, 1U, manifest.size, stdout) !=
               manifest.size) {
        fprintf(stderr, "coff-capabilities: cannot write manifest\n");
        status = 1;
    }
    ld_coff_capability_manifest_deinit(&manifest);
    return status;
}

#endif

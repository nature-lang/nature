#ifndef NATURE_LD_COFF_CAPABILITY_H
#define NATURE_LD_COFF_CAPABILITY_H

/*
 * Deterministic inventory for the prebuilt windows_amd64 sysroot.
 *
 * The scanner accepts direct COFF/BigObj/short-import objects and archives.
 * It returns a newly allocated, NUL-terminated JSON manifest. `size` excludes
 * the terminating NUL. The caller releases it with
 * ld_coff_capability_manifest_deinit().
 *
 * Example:
 *
 *     const char *inputs[] = {"crt2.obj", "kernel32.lib"};
 *     ld_coff_capability_manifest_t manifest = {0};
 *     char error[512];
 *     int status = ld_coff_capability_scan_json(
 *             inputs, 2, &manifest, error, sizeof(error));
 *     if (status == LD_OK)
 *         fwrite(manifest.json, 1, manifest.size, stdout);
 *     ld_coff_capability_manifest_deinit(&manifest);
 *
 * Source semantics:
 *   llvm/lld/COFF/InputFiles.cpp
 *   llvm/lld/COFF/MinGW.cpp
 *   llvm/include/llvm/BinaryFormat/COFF.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "ld.h"

#include <stddef.h>

typedef struct {
    char *json;
    size_t size;
} ld_coff_capability_manifest_t;

int ld_coff_capability_scan_json(
        const char *const *input_paths, size_t input_count,
        ld_coff_capability_manifest_t *manifest, char *error,
        size_t error_capacity);

void ld_coff_capability_manifest_deinit(
        ld_coff_capability_manifest_t *manifest);

#endif

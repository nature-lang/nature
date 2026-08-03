#ifndef NATURE_BINARY_COFF_AMD64_H
#define NATURE_BINARY_COFF_AMD64_H

/*
 * Nature AMD64 instruction-to-COFF adapter.
 *
 * The relocation choices follow LLVM's AMD64 COFF object writer semantics.
 *
 * Sources:
 *   llvm/lib/MC/WinCOFFObjectWriter.cpp
 *   llvm/lib/Target/X86/MCTargetDesc/X86WinCOFFObjectWriter.cpp
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "coff_writer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct module_t module_t;

/*
 * Encodes one Nature module into an existing AMD64 COFF object. Global data
 * is appended to .data and instructions to .text. Symbolic control transfers
 * use IMAGE_REL_AMD64_REL32; RIP-relative operands use REL32 or REL32_1..5
 * according to the bytes following the displacement field.
 *
 * Compiler-private globals named `__nature_unwind_plan$<function>` are
 * consumed rather than copied to .data. Each plan produces version-1 AMD64
 * UNWIND_INFO in `.xdata$<function>` and a three-field RUNTIME_FUNCTION in
 * `.pdata$<function>`. The three fields use IMAGE_REL_AMD64_ADDR32NB; xdata is
 * an associative COMDAT child of the per-function pdata COMDAT.
 *
 * This is a one-shot lowering operation: as with the existing ELF/Mach-O
 * AMD64 paths, symbolic operands are rewritten in place for machine encoding.
 */
coff_writer_status_t coff_encode_amd64_module(coff_object_t *object,
                                              module_t *module);

/* Same operation, with a caller-owned diagnostic buffer. */
coff_writer_status_t coff_encode_amd64_module_ex(
        coff_object_t *object, module_t *module, char *error,
        size_t error_capacity);

/*
 * Creates a standard AMD64 object, encodes module, writes output_path, and on
 * success stores a copied output path in module->object_file.
 */
coff_writer_status_t coff_assembler_module(module_t *module,
                                           const char *output_path,
                                           char *error,
                                           size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif

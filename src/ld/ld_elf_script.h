#ifndef NATURE_LD_ELF_SCRIPT_H
#define NATURE_LD_ELF_SCRIPT_H

#include "ld.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD_ELF_SCRIPT_MAX_SIZE (16U * 1024U * 1024U)

typedef enum {
    LD_ELF_SCRIPT_OK = 0,
    LD_ELF_SCRIPT_INVALID,
    LD_ELF_SCRIPT_UNSUPPORTED_ARCH,
    LD_ELF_SCRIPT_OUT_OF_MEMORY,
} ld_elf_script_result_t;

typedef struct {
    char *path;
    bool as_needed;
} ld_elf_script_input_t;

typedef struct {
    ld_elf_script_input_t *inputs;
    size_t input_count;
    size_t input_capacity;
    char **search_dirs;
    size_t search_dir_count;
    size_t search_dir_capacity;
} ld_elf_script_t;

typedef struct {
    size_t offset;
    const char *message;
} ld_elf_script_error_t;

void ld_elf_script_init(ld_elf_script_t *script);
void ld_elf_script_deinit(ld_elf_script_t *script);

/*
 * Parse the library-wrapper subset used by GNU libc linker scripts:
 * OUTPUT_FORMAT, OUTPUT_ARCH, INPUT, GROUP, AS_NEEDED, and SEARCH_DIR.
 * The parser owns copies of every returned path and never reads host state.
 *
 * Zig commit 738d2be9 provides the INPUT/GROUP/AS_NEEDED parsing model in
 * src/link/LdScript.zig. SEARCH_DIR, quoting, cycle-safe recursive loading,
 * and RISC-V OUTPUT_FORMAT validation are filled in from GNU ld behavior.
 */
ld_elf_script_result_t ld_elf_script_parse(
        const uint8_t *data, size_t size, ld_arch_t arch,
        ld_elf_script_t *script, ld_elf_script_error_t *error);

const char *ld_elf_script_result_string(ld_elf_script_result_t result);

#endif

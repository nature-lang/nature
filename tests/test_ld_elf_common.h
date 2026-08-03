#ifndef NATURE_TEST_LD_ELF_COMMON_H
#define NATURE_TEST_LD_ELF_COMMON_H

#include "src/ld/ld.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef struct {
    unsigned count;
    char message[4096];
} diagnostic_capture_t;

uint64_t test_elf_add_signed_u32(uint64_t base, uint32_t encoded);
void capture_diagnostic(void *context, ld_diag_level_t level,
                        const char *message);
void write_fixture(char path[], const void *bytes, size_t size);
void test_elf_write_u16(uint8_t *bytes, uint16_t value);
void test_elf_write_u32(uint8_t *bytes, uint32_t value);
void test_elf_write_u64(uint8_t *bytes, uint64_t value);
uint16_t test_elf_read_u16(const uint8_t *bytes);
uint32_t test_elf_read_u32(const uint8_t *bytes);
uint64_t test_elf_read_u64(const uint8_t *bytes);
const uint8_t *test_elf_find_output_section(const uint8_t *image,
                                            size_t image_size,
                                            const char *name);
const uint8_t *test_elf_find_program_header(const uint8_t *image,
                                            size_t image_size,
                                            uint32_t type,
                                            size_t *match_count);
size_t test_elf_align(size_t value, size_t alignment);
uint32_t test_elf_append_name(char *table, size_t capacity, size_t *cursor,
                              const char *name);
void test_elf_write_section(uint8_t *bytes, uint32_t name, uint32_t type,
                            uint64_t flags, uint64_t offset, uint64_t size,
                            uint32_t link, uint32_t info, uint64_t alignment,
                            uint64_t entry_size);
uint8_t *read_test_fixture(const char *path, size_t *result_size,
                           mode_t *result_mode);
void test_elf_assert_executable_mode(mode_t mode);
uint32_t read_test_elf_entry_word(const char *path);
bool test_elf_file_contains_u32(const char *path, uint32_t value);
int link_test_elf_inputs(const char *output_path,
                         const char *const *input_paths, size_t input_count,
                         diagnostic_capture_t *capture);
int link_test_elf_inputs_configured(
        const char *output_path, const char *const *input_paths,
        size_t input_count, ld_arch_t arch, bool pie,
        ld_debug_mode_t debug_mode, bool remove_existing_output,
        diagnostic_capture_t *capture);
void test_archive_append(uint8_t *archive, size_t capacity, size_t *cursor,
                         const char *name, const uint8_t *payload,
                         size_t payload_size);
uint8_t *make_test_elf_object(uint16_t machine, bool with_sht_rel,
                              bool executable_stack, size_t *result_size);
uint8_t *make_test_elf_weak_dynamic_object(size_t *result_size);

#endif

#ifndef NATURE_TEST_LD_MACHO_COMMON_H
#define NATURE_TEST_LD_MACHO_COMMON_H

#include "src/ld/ld.h"
#include "src/ld/ld_internal.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef struct {
    unsigned count;
    char message[4096];
} test_ld_diagnostic_capture_t;

void test_ld_capture_diagnostic(void *context, ld_diag_level_t level,
                                const char *message);
void test_ld_write_fixture(char path[], const void *bytes, size_t size);
void test_ld_write_named_fixture(const char *path, const void *bytes,
                                 size_t size);
void test_ld_expect_invalid_input(const char *path, int expected,
                                  const char *message_fragment);
bool test_ld_dylib_has_symbol(char *const *symbols, size_t count,
                              const char *name);
void test_ld_parsed_context_deinit(ld_context_t *ctx);

#endif

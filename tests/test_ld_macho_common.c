#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_ld_capture_diagnostic(void *context, ld_diag_level_t level,
                                const char *message) {
    (void) level;
    test_ld_diagnostic_capture_t *capture = context;
    capture->count++;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

void test_ld_write_fixture(char path[], const void *bytes, size_t size) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, bytes, size) == (ssize_t) size);
    assert(close(fd) == 0);
}

void test_ld_write_named_fixture(const char *path, const void *bytes,
                                 size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, bytes, size) == (ssize_t) size);
    assert(close(fd) == 0);
}

void test_ld_expect_invalid_input(const char *path, int expected,
                                  const char *message_fragment) {
    static const char *output = "/tmp/nature-ld-invalid-output";
    unlink(output);
    test_ld_diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output;
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, path) == LD_OK);
    assert(ld_link(&options) == expected);
    assert(capture.count > 0);
    assert(strstr(capture.message, message_fragment) != NULL);
    assert(access(output, F_OK) != 0);
    ld_options_deinit(&options);
}

bool test_ld_dylib_has_symbol(char *const *symbols, size_t count,
                              const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(symbols[i], name) == 0) return true;
    }
    return false;
}

void test_ld_parsed_context_deinit(ld_context_t *ctx) {
    for (size_t i = 0; i < ctx->files.count; i++) {
        free(ctx->files.items[i]->bytes);
        free(ctx->files.items[i]->path);
        free(ctx->files.items[i]);
    }
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        ld_dylib_input_t *dylib = &ctx->dylibs.items[i];
        free(dylib->path);
        free(dylib->install_name);
        ld_string_set_deinit(&dylib->export_set);
        for (size_t j = 0; j < dylib->export_count; j++) {
            free(dylib->exports[j]);
        }
        free(dylib->exports);
        ld_string_set_deinit(&dylib->weak_export_set);
        for (size_t j = 0; j < dylib->weak_export_count; j++) {
            free(dylib->weak_exports[j]);
        }
        free(dylib->weak_exports);
        ld_string_set_deinit(&dylib->reexport_set);
        for (size_t j = 0; j < dylib->reexport_count; j++) {
            free(dylib->reexports[j]);
        }
        free(dylib->reexports);
    }
    free(ctx->files.items);
    free(ctx->dylibs.items);
}

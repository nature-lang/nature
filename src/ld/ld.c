#include "ld_internal.h"
#include "ld_coff.h"
#include "ld_macho_symbols.h"
#include "ld_output.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __WINDOWS
#include <io.h>
#include <windows.h>
#endif

void *ld_realloc_array(void *old, size_t old_count, size_t new_count, size_t element_size) {
    if (element_size == 0 || new_count > SIZE_MAX / element_size) {
        return NULL;
    }
    void *value = realloc(old, new_count * element_size);
    if (value && new_count > old_count) {
        memset((uint8_t *) value + old_count * element_size, 0, (new_count - old_count) * element_size);
    }
    return value;
}

void ld_string_set_deinit(ld_string_set_entry_t **set) {
    if (!set) return;
    ld_string_set_entry_t *entry, *temporary;
    HASH_ITER(hh, *set, entry, temporary) {
        HASH_DEL(*set, entry);
        free(entry);
    }
}

static int ld_string_push(ld_string_list_t *list, const char *value) {
    if (!value) {
        return LD_INVALID_ARGUMENT;
    }
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2U) {
            return LD_IO_ERROR;
        }
        size_t next = list->capacity ? list->capacity * 2U : 8U;
        char **items = ld_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count] = strdup(value);
    if (!list->items[list->count]) {
        return LD_IO_ERROR;
    }
    list->count++;
    return LD_OK;
}

static int ld_string_push_unique(ld_string_list_t *list, const char *value) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], value) == 0) return LD_OK;
    }
    return ld_string_push(list, value);
}

static void ld_string_free(ld_string_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}


int ld_fail(ld_context_t *ctx, int code, const char *format, ...) {
    if (ctx) {
        ctx->error = code;
        if (ctx->options && ctx->options->diagnostic) {
            char message[4096];
            va_list args;
            va_start(args, format);
            vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            ctx->options->diagnostic(ctx->options->diagnostic_context, LD_DIAG_ERROR, message);
        }
    }
    return code;
}

static int ld_report_option_error(const ld_options_t *options, int code, const char *format, ...) {
    if (options && options->diagnostic) {
        char message[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        options->diagnostic(options->diagnostic_context, LD_DIAG_ERROR, message);
    }
    return code;
}

int ld_write_file_atomic(const ld_options_t *options, const char *path,
                         const uint8_t *data, size_t size, bool executable) {
    if (!options || !path || !*path || (!data && size != 0U)) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT,
                                      "invalid output file");
    }

    static const char temporary_suffix[] = ".tmp.XXXXXX";
    const char *output = path;
    size_t output_length = strlen(output);
    if (output_length > SIZE_MAX - sizeof(temporary_suffix)) {
        return ld_report_option_error(options, LD_OUTPUT_ERROR,
                                      "output path is too long");
    }
    size_t temporary_size = output_length + sizeof(temporary_suffix);
    char *temporary = malloc(temporary_size);
    if (!temporary) {
        return ld_report_option_error(
                options, LD_IO_ERROR,
                "out of memory creating temporary output path");
    }
    snprintf(temporary, temporary_size, "%s%s", output, temporary_suffix);

    int fd = mkstemp(temporary);
    if (fd < 0) {
        int saved = errno;
        free(temporary);
        return ld_report_option_error(
                options, LD_OUTPUT_ERROR,
                "cannot create temporary output for '%s': %s", output,
                strerror(saved));
    }
#ifdef __WINDOWS
    if (_setmode(fd, _O_BINARY) == -1) {
        int saved = errno;
        close(fd);
        unlink(temporary);
        free(temporary);
        return ld_report_option_error(
                options, LD_OUTPUT_ERROR,
                "cannot set binary mode for output '%s': %s", output,
                strerror(saved));
    }
#endif

    int status = LD_OK;
    size_t written = 0U;
    while (written < size) {
        ssize_t count = write(fd, data + written, size - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            int saved = errno;
            if (count == 0) {
                status = ld_report_option_error(
                        options, LD_OUTPUT_ERROR,
                        "cannot write output '%s': short write", output);
            } else {
                status = ld_report_option_error(
                        options, LD_OUTPUT_ERROR,
                        "cannot write output '%s': %s", output,
                        strerror(saved));
            }
            break;
        }
        written += (size_t) count;
    }
#ifndef __WINDOWS
    mode_t mode = executable ? 0755 : 0644;
    if (status == LD_OK && fchmod(fd, mode) != 0) {
        int saved = errno;
        status = ld_report_option_error(options, LD_OUTPUT_ERROR,
                                        "cannot chmod output '%s': %s", output,
                                        strerror(saved));
    }
#else
    (void) executable;
#endif
#ifdef __WINDOWS
    if (status == LD_OK && _commit(fd) != 0) {
#else
    if (status == LD_OK && fsync(fd) != 0) {
#endif
        int saved = errno;
        status = ld_report_option_error(options, LD_OUTPUT_ERROR,
                                        "cannot flush output '%s': %s", output,
                                        strerror(saved));
    }
    if (close(fd) != 0 && status == LD_OK) {
        int saved = errno;
        status = ld_report_option_error(options, LD_OUTPUT_ERROR,
                                        "cannot close output '%s': %s", output,
                                        strerror(saved));
    }
#ifdef __WINDOWS
    if (status == LD_OK &&
        !MoveFileExA(temporary, output,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD saved = GetLastError();
        status = ld_report_option_error(
                options, LD_OUTPUT_ERROR,
                "cannot replace output '%s': Windows error %lu", output,
                (unsigned long) saved);
    }
#else
    if (status == LD_OK && rename(temporary, output) != 0) {
        int saved = errno;
        status = ld_report_option_error(options, LD_OUTPUT_ERROR,
                                        "cannot replace output '%s': %s", output,
                                        strerror(saved));
    }
#endif
    if (status != LD_OK) {
        unlink(temporary);
    }
    free(temporary);
    return status;
}

int ld_write_output_atomic(const ld_options_t *options, const uint8_t *image,
                           size_t size) {
    return ld_write_file_atomic(options, options ? options->output_path : NULL,
                                image, size, true);
}

static void ld_default_diagnostic(void *context, ld_diag_level_t level, const char *message) {
    (void) context;
    FILE *stream = level == LD_DIAG_ERROR ? stderr : stdout;
    fprintf(stream, "ld: %s%s\n", level == LD_DIAG_WARNING ? "warning: " : "", message);
}

void ld_options_init(ld_options_t *options) {
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->os = LD_OS_DARWIN;
    options->arch = LD_ARCH_ARM64;
    options->entry_symbol = "runtime_main";
    options->min_os_version = ld_macos_version(11, 0, 0);
    options->sdk_version = ld_macos_version(14, 0, 0);
    options->debug_mode = LD_DEBUG_NONE;
    options->pie = true;
    options->adhoc_codesign = true;
    options->diagnostic = ld_default_diagnostic;
}

void ld_options_deinit(ld_options_t *options) {
    if (!options) {
        return;
    }
    ld_string_free(&options->inputs);
    ld_string_free(&options->library_paths);
    ld_string_free(&options->framework_paths);
    ld_string_free(&options->rpaths);
    ld_string_free(&options->libraries);
    ld_string_free(&options->frameworks);
}

int ld_add_input(ld_options_t *options, const char *path) {
    if (!options) {
        return LD_INVALID_ARGUMENT;
    }
    if (!path || !*path) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT, "linker input path is empty");
    }
    int result = ld_string_push(&options->inputs, path);
    return result == LD_OK
                   ? LD_OK
                   : ld_report_option_error(options, result, "cannot record linker input '%s'", path);
}

int ld_add_library_path(ld_options_t *options, const char *path) {
    if (!options) {
        return LD_INVALID_ARGUMENT;
    }
    if (!path || !*path) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT, "library search path is empty");
    }
    int result = ld_string_push(&options->library_paths, path);
    return result == LD_OK
                   ? LD_OK
                   : ld_report_option_error(options, result, "cannot record library search path '%s'", path);
}

int ld_add_rpath(ld_options_t *options, const char *path) {
    if (!options) {
        return LD_INVALID_ARGUMENT;
    }
    if (!path || !*path) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT,
                                      "runtime search path is empty");
    }
    int result = ld_string_push_unique(&options->rpaths, path);
    return result == LD_OK
                   ? LD_OK
                   : ld_report_option_error(
                             options, result,
                             "cannot record runtime search path '%s'", path);
}

static int ld_parse_flag_token(ld_options_t *options, const char *token, const char *next, bool *consumed) {
    *consumed = false;
    if (strcmp(token, "-ObjC") == 0) {
        if (options->os != LD_OS_DARWIN) {
            return LD_UNSUPPORTED;
        }
        options->objc_load = true;
        return LD_OK;
    }
    if (strcmp(token, "-framework") == 0) {
        if (!next || !*next) {
            return LD_INVALID_ARGUMENT;
        }
        *consumed = true;
        return ld_string_push(&options->frameworks, next);
    }
    if (strcmp(token, "-rpath") == 0) {
        if (!next || !*next) {
            return LD_INVALID_ARGUMENT;
        }
        *consumed = true;
        return ld_string_push_unique(&options->rpaths, next);
    }
    if (strcmp(token, "-F") == 0 || strcmp(token, "-L") == 0 || strcmp(token, "-l") == 0) {
        if (!next || !*next) {
            return LD_INVALID_ARGUMENT;
        }
        *consumed = true;
        if (token[1] == 'F') {
            return ld_string_push(&options->framework_paths, next);
        }
        if (token[1] == 'L') {
            return ld_string_push(&options->library_paths, next);
        }
        return ld_string_push(&options->libraries, next);
    }
    if (strncmp(token, "-framework", 10) == 0 && token[10]) {
        return ld_string_push(&options->frameworks, token + 10);
    }
    if (token[0] == '-' && token[1] == 'F' && token[2]) {
        return ld_string_push(&options->framework_paths, token + 2);
    }
    if (token[0] == '-' && token[1] == 'L' && token[2]) {
        return ld_string_push(&options->library_paths, token + 2);
    }
    if (token[0] == '-' && token[1] == 'l' && token[2]) {
        return ld_string_push(&options->libraries, token + 2);
    }
    if (token[0] == '-' && token[1] == '-') {
        return LD_UNSUPPORTED;
    }
    if (token[0] == '-' && token[1]) {
        return LD_UNSUPPORTED;
    }
    return ld_add_input(options, token);
}

static int ld_shell_token(const char *flags, size_t length, size_t *position,
                          char *token, size_t token_size, bool *present) {
    size_t pos = *position;
    while (pos < length && isspace((unsigned char) flags[pos])) pos++;
    if (pos == length) {
        *position = pos;
        *present = false;
        return LD_OK;
    }
    size_t count = 0;
    char quote = 0;
    bool had_character = false;
    while (pos < length) {
        char c = flags[pos++];
        if (quote) {
            if (c == quote) {
                quote = 0;
                had_character = true;
                continue;
            }
            if (quote == '"' && c == '\\') {
                if (pos == length) return LD_INVALID_ARGUMENT;
                char escaped = flags[pos];
                if (escaped == '$' || escaped == '`' || escaped == '"' || escaped == '\\' || escaped == '\n') {
                    pos++;
                    if (escaped == '\n') continue;
                    c = escaped;
                }
            }
        } else {
            if (c == '\'' || c == '"') {
                quote = c;
                had_character = true;
                continue;
            }
            if (c == '\\') {
                if (pos == length) return LD_INVALID_ARGUMENT;
                c = flags[pos++];
                if (c == '\n') continue;
            } else if (isspace((unsigned char) c)) {
                break;
            }
        }
        had_character = true;
        if (count + 1U >= token_size) return LD_INVALID_ARGUMENT;
        token[count++] = c;
    }
    if (quote || !had_character) return LD_INVALID_ARGUMENT;
    token[count] = '\0';
    *position = pos;
    *present = true;
    return LD_OK;
}

int ld_parse_flags(ld_options_t *options, const char *flags) {
    if (!options || !flags) return LD_INVALID_ARGUMENT;
    size_t length = strlen(flags), position = 0;
    while (true) {
        char token[4096];
        bool present = false;
        int result = ld_shell_token(flags, length, &position, token, sizeof(token), &present);
        if (result != LD_OK) {
            return ld_report_option_error(options, result, "malformed linker flags near byte %zu", position);
        }
        if (!present) break;
        char next[4096];
        const char *next_ptr = NULL;
        if (strcmp(token, "-framework") == 0 || strcmp(token, "-F") == 0 ||
            strcmp(token, "-L") == 0 || strcmp(token, "-l") == 0 ||
            strcmp(token, "-rpath") == 0) {
            bool next_present = false;
            result = ld_shell_token(flags, length, &position, next, sizeof(next), &next_present);
            if (result != LD_OK) {
                return ld_report_option_error(options, result, "malformed argument for linker flag '%s'", token);
            }
            if (!next_present || !next[0]) {
                return ld_report_option_error(options, LD_INVALID_ARGUMENT,
                                              "linker flag '%s' requires a non-empty argument", token);
            }
            next_ptr = next;
        }
        bool consumed = false;
        result = ld_parse_flag_token(options, token, next_ptr, &consumed);
        if (result == LD_UNSUPPORTED) {
            return ld_report_option_error(options, result, "unsupported linker flag '%s'", token);
        }
        if (result != LD_OK) {
            return ld_report_option_error(options, result, "cannot process linker token '%s'", token);
        }
    }
    return LD_OK;
}

static void ld_context_deinit(ld_context_t *ctx) {
    ld_symbol_t *symbol, *tmp;
    HASH_ITER(hh, ctx->symbols, symbol, tmp) {
        HASH_DEL(ctx->symbols, symbol);
        free(symbol->name);
        free(symbol);
    }
    for (size_t i = 0; i < ctx->objects.count; i++) {
        ld_object_t *object = ctx->objects.items[i];
        free(object->member_name);
        free(object->sections);
        free(object->symbols);
        free(object);
    }
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
        ld_string_set_deinit(&dylib->rpath_set);
        for (size_t j = 0; j < dylib->rpath_count; j++) {
            free(dylib->rpaths[j]);
        }
        free(dylib->rpaths);
        ld_macho_dylib_symbols_deinit(dylib);
    }
    for (size_t i = 0; i < ctx->outputs.count; i++) {
        free(ctx->outputs.items[i]->data);
        free(ctx->outputs.items[i]);
    }
    free(ctx->objects.items);
    free(ctx->files.items);
    free(ctx->dylibs.items);
    free(ctx->outputs.items);
    free(ctx->dynamic_symbols.items);
    free(ctx->got_refs.items);
    free(ctx->tlv_ptr_refs.items);
    free(ctx->rebases.items);
    free(ctx->binds.items);
    free(ctx->branch_thunks.items);
    free(ctx->unwind.records);
}

int ld_link(const ld_options_t *options) {
    if (!options) {
        return LD_INVALID_ARGUMENT;
    }
    if (options->debug_mode != LD_DEBUG_NONE &&
        options->debug_mode != LD_DEBUG_DWARF) {
        return ld_report_option_error(
                options, LD_INVALID_ARGUMENT,
                "invalid linker debug mode value %d",
                (int) options->debug_mode);
    }
    if (!options->output_path || !*options->output_path) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT, "linker output path is empty");
    }
    if (options->inputs.count == 0) {
        return ld_report_option_error(options, LD_INVALID_ARGUMENT, "no linker input files were provided");
    }
    if (options->os == LD_OS_LINUX) {
        return ld_link_elf(options);
    }
    if (options->os == LD_OS_WINDOWS) {
        if (options->arch == LD_ARCH_AMD64) {
            return ld_link_coff(options);
        }
        return ld_report_option_error(
                options, LD_UNSUPPORTED,
                "the COFF linker currently supports windows/amd64 only");
    }
    if (options->os != LD_OS_DARWIN || options->arch != LD_ARCH_ARM64) {
        if (options->diagnostic) {
            options->diagnostic(options->diagnostic_context, LD_DIAG_ERROR,
                                "the native ld implementation currently supports Darwin arm64 only");
        }
        return LD_UNSUPPORTED;
    }

    ld_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = options;
    ctx.min_version = options->min_os_version ? options->min_os_version : ld_macos_version(11, 0, 0);
    ctx.sdk_version = options->sdk_version ? options->sdk_version : ld_macos_version(14, 0, 0);

    int result = LD_OK;
    for (size_t i = 0; i < options->inputs.count; i++) {
        result = ld_parse_input_file(&ctx, options->inputs.items[i]);
        if (result != LD_OK) {
            goto done;
        }
    }
    result = ld_resolve_requested_libraries(&ctx);
    if (result != LD_OK) {
        goto done;
    }
    result = ld_link_macho(&ctx);

done:
    if (result != LD_OK && options->diagnostic == NULL) {
        fprintf(stderr, "ld: link failed (%d)\n", result);
    }
    ld_context_deinit(&ctx);
    return result;
}

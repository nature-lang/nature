#include "ldd_internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *ldd_realloc_array(void *old, size_t old_count, size_t new_count, size_t element_size) {
    if (element_size == 0 || new_count > SIZE_MAX / element_size) {
        return NULL;
    }
    void *value = realloc(old, new_count * element_size);
    if (value && new_count > old_count) {
        memset((uint8_t *) value + old_count * element_size, 0, (new_count - old_count) * element_size);
    }
    return value;
}

void ldd_string_set_deinit(ldd_string_set_entry_t **set) {
    if (!set) return;
    ldd_string_set_entry_t *entry, *temporary;
    HASH_ITER(hh, *set, entry, temporary) {
        HASH_DEL(*set, entry);
        free(entry);
    }
}

static int ldd_string_push(ldd_string_list_t *list, const char *value) {
    if (!value) {
        return LDD_INVALID_ARGUMENT;
    }
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2U) {
            return LDD_IO_ERROR;
        }
        size_t next = list->capacity ? list->capacity * 2U : 8U;
        char **items = ldd_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count] = strdup(value);
    if (!list->items[list->count]) {
        return LDD_IO_ERROR;
    }
    list->count++;
    return LDD_OK;
}

static void ldd_string_free(ldd_string_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}


int ldd_fail(ldd_context_t *ctx, int code, const char *format, ...) {
    if (ctx) {
        ctx->error = code;
        if (ctx->options && ctx->options->diagnostic) {
            char message[4096];
            va_list args;
            va_start(args, format);
            vsnprintf(message, sizeof(message), format, args);
            va_end(args);
            ctx->options->diagnostic(ctx->options->diagnostic_context, LDD_DIAG_ERROR, message);
        }
    }
    return code;
}

static int ldd_report_option_error(const ldd_options_t *options, int code, const char *format, ...) {
    if (options && options->diagnostic) {
        char message[4096];
        va_list args;
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        options->diagnostic(options->diagnostic_context, LDD_DIAG_ERROR, message);
    }
    return code;
}


static void ldd_default_diagnostic(void *context, ldd_diag_level_t level, const char *message) {
    (void) context;
    FILE *stream = level == LDD_DIAG_ERROR ? stderr : stdout;
    fprintf(stream, "ldd: %s%s\n", level == LDD_DIAG_WARNING ? "warning: " : "", message);
}

void ldd_options_init(ldd_options_t *options) {
    if (!options) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->os = LDD_OS_DARWIN;
    options->arch = LDD_ARCH_ARM64;
    options->entry_symbol = "runtime_main";
    options->min_os_version = ldd_macos_version(11, 0, 0);
    options->sdk_version = ldd_macos_version(14, 0, 0);
    options->pie = true;
    options->adhoc_codesign = true;
    options->diagnostic = ldd_default_diagnostic;
}

void ldd_options_deinit(ldd_options_t *options) {
    if (!options) {
        return;
    }
    ldd_string_free(&options->inputs);
    ldd_string_free(&options->library_paths);
    ldd_string_free(&options->framework_paths);
    ldd_string_free(&options->libraries);
    ldd_string_free(&options->frameworks);
}

int ldd_add_input(ldd_options_t *options, const char *path) {
    if (!options) {
        return LDD_INVALID_ARGUMENT;
    }
    if (!path || !*path) {
        return ldd_report_option_error(options, LDD_INVALID_ARGUMENT, "linker input path is empty");
    }
    int result = ldd_string_push(&options->inputs, path);
    return result == LDD_OK
                   ? LDD_OK
                   : ldd_report_option_error(options, result, "cannot record linker input '%s'", path);
}

int ldd_add_library_path(ldd_options_t *options, const char *path) {
    if (!options) {
        return LDD_INVALID_ARGUMENT;
    }
    if (!path || !*path) {
        return ldd_report_option_error(options, LDD_INVALID_ARGUMENT, "library search path is empty");
    }
    int result = ldd_string_push(&options->library_paths, path);
    return result == LDD_OK
                   ? LDD_OK
                   : ldd_report_option_error(options, result, "cannot record library search path '%s'", path);
}

static int ldd_parse_flag_token(ldd_options_t *options, const char *token, const char *next, bool *consumed) {
    *consumed = false;
    if (strcmp(token, "-framework") == 0) {
        if (!next || !*next) {
            return LDD_INVALID_ARGUMENT;
        }
        *consumed = true;
        return ldd_string_push(&options->frameworks, next);
    }
    if (strcmp(token, "-F") == 0 || strcmp(token, "-L") == 0 || strcmp(token, "-l") == 0) {
        if (!next || !*next) {
            return LDD_INVALID_ARGUMENT;
        }
        *consumed = true;
        if (token[1] == 'F') {
            return ldd_string_push(&options->framework_paths, next);
        }
        if (token[1] == 'L') {
            return ldd_string_push(&options->library_paths, next);
        }
        return ldd_string_push(&options->libraries, next);
    }
    if (strncmp(token, "-framework", 10) == 0 && token[10]) {
        return ldd_string_push(&options->frameworks, token + 10);
    }
    if (token[0] == '-' && token[1] == 'F' && token[2]) {
        return ldd_string_push(&options->framework_paths, token + 2);
    }
    if (token[0] == '-' && token[1] == 'L' && token[2]) {
        return ldd_string_push(&options->library_paths, token + 2);
    }
    if (token[0] == '-' && token[1] == 'l' && token[2]) {
        return ldd_string_push(&options->libraries, token + 2);
    }
    if (token[0] == '-' && token[1] == '-') {
        return LDD_UNSUPPORTED;
    }
    if (token[0] == '-' && token[1]) {
        return LDD_UNSUPPORTED;
    }
    return ldd_add_input(options, token);
}

static int ldd_shell_token(const char *flags, size_t length, size_t *position,
                           char *token, size_t token_size, bool *present) {
    size_t pos = *position;
    while (pos < length && isspace((unsigned char) flags[pos])) pos++;
    if (pos == length) {
        *position = pos;
        *present = false;
        return LDD_OK;
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
                if (pos == length) return LDD_INVALID_ARGUMENT;
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
                if (pos == length) return LDD_INVALID_ARGUMENT;
                c = flags[pos++];
                if (c == '\n') continue;
            } else if (isspace((unsigned char) c)) {
                break;
            }
        }
        had_character = true;
        if (count + 1U >= token_size) return LDD_INVALID_ARGUMENT;
        token[count++] = c;
    }
    if (quote || !had_character) return LDD_INVALID_ARGUMENT;
    token[count] = '\0';
    *position = pos;
    *present = true;
    return LDD_OK;
}

int ldd_parse_flags(ldd_options_t *options, const char *flags) {
    if (!options || !flags) return LDD_INVALID_ARGUMENT;
    size_t length = strlen(flags), position = 0;
    while (true) {
        char token[4096];
        bool present = false;
        int result = ldd_shell_token(flags, length, &position, token, sizeof(token), &present);
        if (result != LDD_OK) {
            return ldd_report_option_error(options, result, "malformed Darwin linker flags near byte %zu", position);
        }
        if (!present) break;
        char next[4096];
        const char *next_ptr = NULL;
        if (strcmp(token, "-framework") == 0 || strcmp(token, "-F") == 0 ||
            strcmp(token, "-L") == 0 || strcmp(token, "-l") == 0) {
            bool next_present = false;
            result = ldd_shell_token(flags, length, &position, next, sizeof(next), &next_present);
            if (result != LDD_OK) {
                return ldd_report_option_error(options, result, "malformed argument for linker flag '%s'", token);
            }
            if (!next_present || !next[0]) {
                return ldd_report_option_error(options, LDD_INVALID_ARGUMENT,
                                               "linker flag '%s' requires a non-empty argument", token);
            }
            next_ptr = next;
        }
        bool consumed = false;
        result = ldd_parse_flag_token(options, token, next_ptr, &consumed);
        if (result == LDD_UNSUPPORTED) {
            return ldd_report_option_error(options, result, "unsupported Darwin linker flag '%s'", token);
        }
        if (result != LDD_OK) {
            return ldd_report_option_error(options, result, "cannot process Darwin linker token '%s'", token);
        }
    }
    return LDD_OK;
}

static void ldd_context_deinit(ldd_context_t *ctx) {
    ldd_symbol_t *symbol, *tmp;
    HASH_ITER(hh, ctx->symbols, symbol, tmp) {
        HASH_DEL(ctx->symbols, symbol);
        free(symbol->name);
        free(symbol);
    }
    for (size_t i = 0; i < ctx->objects.count; i++) {
        ldd_object_t *object = ctx->objects.items[i];
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
        ldd_dylib_input_t *dylib = &ctx->dylibs.items[i];
        free(dylib->path);
        free(dylib->install_name);
        ldd_string_set_deinit(&dylib->export_set);
        for (size_t j = 0; j < dylib->export_count; j++) {
            free(dylib->exports[j]);
        }
        free(dylib->exports);
        ldd_string_set_deinit(&dylib->weak_export_set);
        for (size_t j = 0; j < dylib->weak_export_count; j++) {
            free(dylib->weak_exports[j]);
        }
        free(dylib->weak_exports);
        ldd_string_set_deinit(&dylib->reexport_set);
        for (size_t j = 0; j < dylib->reexport_count; j++) {
            free(dylib->reexports[j]);
        }
        free(dylib->reexports);
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
    free(ctx->rebases.items);
    free(ctx->binds.items);
    free(ctx->branch_thunks.items);
    free(ctx->unwind.records);
}

int ldd_link(const ldd_options_t *options) {
    if (!options) {
        return LDD_INVALID_ARGUMENT;
    }
    if (!options->output_path || !*options->output_path) {
        return ldd_report_option_error(options, LDD_INVALID_ARGUMENT, "linker output path is empty");
    }
    if (options->inputs.count == 0) {
        return ldd_report_option_error(options, LDD_INVALID_ARGUMENT, "no linker input files were provided");
    }
    if (options->os != LDD_OS_DARWIN || options->arch != LDD_ARCH_ARM64) {
        if (options->diagnostic) {
            options->diagnostic(options->diagnostic_context, LDD_DIAG_ERROR,
                                "the native ldd implementation currently supports Darwin arm64 only");
        }
        return LDD_UNSUPPORTED;
    }

    ldd_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = options;
    ctx.min_version = options->min_os_version ? options->min_os_version : ldd_macos_version(11, 0, 0);
    ctx.sdk_version = options->sdk_version ? options->sdk_version : ldd_macos_version(14, 0, 0);

    int result = LDD_OK;
    for (size_t i = 0; i < options->inputs.count; i++) {
        result = ldd_parse_input_file(&ctx, options->inputs.items[i]);
        if (result != LDD_OK) {
            goto done;
        }
    }
    result = ldd_resolve_requested_libraries(&ctx);
    if (result != LDD_OK) {
        goto done;
    }
    result = ldd_link_macho(&ctx);

done:
    if (result != LDD_OK && options->diagnostic == NULL) {
        fprintf(stderr, "ldd: link failed (%d)\n", result);
    }
    ldd_context_deinit(&ctx);
    return result;
}

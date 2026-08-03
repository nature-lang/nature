#include "ld_elf_script.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define LD_ELF_SCRIPT_PARSE_DEPTH 32U

typedef enum {
    LD_ELF_SCRIPT_TOKEN_EOF = 0,
    LD_ELF_SCRIPT_TOKEN_WORD,
    LD_ELF_SCRIPT_TOKEN_LPAREN,
    LD_ELF_SCRIPT_TOKEN_RPAREN,
    LD_ELF_SCRIPT_TOKEN_COMMA,
    LD_ELF_SCRIPT_TOKEN_SEMICOLON,
    LD_ELF_SCRIPT_TOKEN_INVALID,
} ld_elf_script_token_type_t;

typedef struct {
    ld_elf_script_token_type_t type;
    size_t offset;
    size_t start;
    size_t end;
    bool quoted;
} ld_elf_script_token_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t cursor;
    size_t invalid_offset;
    const char *invalid_message;
} ld_elf_script_lexer_t;

typedef struct {
    ld_elf_script_lexer_t lexer;
    ld_elf_script_token_t token;
    ld_arch_t arch;
    ld_elf_script_t *script;
    ld_elf_script_error_t *error;
    bool saw_command;
} ld_elf_script_parser_t;

static void ld_elf_script_set_error(ld_elf_script_error_t *error,
                                    size_t offset, const char *message) {
    if (!error || error->message) return;
    error->offset = offset;
    error->message = message;
}

void ld_elf_script_init(ld_elf_script_t *script) {
    if (script) memset(script, 0, sizeof(*script));
}

void ld_elf_script_deinit(ld_elf_script_t *script) {
    if (!script) return;
    for (size_t i = 0; i < script->input_count; i++)
        free(script->inputs[i].path);
    for (size_t i = 0; i < script->search_dir_count; i++)
        free(script->search_dirs[i]);
    free(script->inputs);
    free(script->search_dirs);
    memset(script, 0, sizeof(*script));
}

static bool ld_elf_script_is_space(uint8_t value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\f' || value == '\v';
}

static bool ld_elf_script_word_delimiter(uint8_t value) {
    return ld_elf_script_is_space(value) || value == '(' || value == ')' ||
           value == ',' || value == ';';
}

static ld_elf_script_token_t ld_elf_script_invalid_token(
        ld_elf_script_lexer_t *lexer, size_t offset, const char *message) {
    lexer->invalid_offset = offset;
    lexer->invalid_message = message;
    lexer->cursor = lexer->size;
    return (ld_elf_script_token_t) {
            .type = LD_ELF_SCRIPT_TOKEN_INVALID,
            .offset = offset,
            .start = offset,
            .end = offset,
    };
}

static ld_elf_script_token_t ld_elf_script_lexer_next(
        ld_elf_script_lexer_t *lexer) {
    while (lexer->cursor < lexer->size) {
        uint8_t value = lexer->data[lexer->cursor];
        if (ld_elf_script_is_space(value)) {
            lexer->cursor++;
            continue;
        }
        if (value == '#') {
            while (lexer->cursor < lexer->size &&
                   lexer->data[lexer->cursor] != '\n') {
                lexer->cursor++;
            }
            continue;
        }
        if (value == '/' && lexer->cursor + 1U < lexer->size &&
            lexer->data[lexer->cursor + 1U] == '*') {
            size_t comment_offset = lexer->cursor;
            lexer->cursor += 2U;
            bool closed = false;
            while (lexer->cursor + 1U < lexer->size) {
                if (lexer->data[lexer->cursor] == '*' &&
                    lexer->data[lexer->cursor + 1U] == '/') {
                    lexer->cursor += 2U;
                    closed = true;
                    break;
                }
                lexer->cursor++;
            }
            if (!closed) {
                return ld_elf_script_invalid_token(
                        lexer, comment_offset,
                        "unterminated block comment");
            }
            continue;
        }
        break;
    }

    if (lexer->cursor == lexer->size) {
        return (ld_elf_script_token_t) {
                .type = LD_ELF_SCRIPT_TOKEN_EOF,
                .offset = lexer->cursor,
                .start = lexer->cursor,
                .end = lexer->cursor,
        };
    }

    size_t offset = lexer->cursor;
    uint8_t value = lexer->data[lexer->cursor++];
    switch (value) {
        case '(':
            return (ld_elf_script_token_t) {
                    .type = LD_ELF_SCRIPT_TOKEN_LPAREN,
                    .offset = offset,
                    .start = offset,
                    .end = lexer->cursor,
            };
        case ')':
            return (ld_elf_script_token_t) {
                    .type = LD_ELF_SCRIPT_TOKEN_RPAREN,
                    .offset = offset,
                    .start = offset,
                    .end = lexer->cursor,
            };
        case ',':
            return (ld_elf_script_token_t) {
                    .type = LD_ELF_SCRIPT_TOKEN_COMMA,
                    .offset = offset,
                    .start = offset,
                    .end = lexer->cursor,
            };
        case ';':
            return (ld_elf_script_token_t) {
                    .type = LD_ELF_SCRIPT_TOKEN_SEMICOLON,
                    .offset = offset,
                    .start = offset,
                    .end = lexer->cursor,
            };
        case '\0':
            return ld_elf_script_invalid_token(
                    lexer, offset, "NUL byte in linker script");
        default:
            break;
    }

    if (value == '\'' || value == '"') {
        uint8_t quote = value;
        size_t start = lexer->cursor;
        bool escaped = false;
        while (lexer->cursor < lexer->size) {
            value = lexer->data[lexer->cursor];
            if (value == '\0') {
                return ld_elf_script_invalid_token(
                        lexer, lexer->cursor,
                        "NUL byte in quoted linker-script token");
            }
            if (!escaped && value == quote) {
                size_t end = lexer->cursor++;
                return (ld_elf_script_token_t) {
                        .type = LD_ELF_SCRIPT_TOKEN_WORD,
                        .offset = offset,
                        .start = start,
                        .end = end,
                        .quoted = true,
                };
            }
            if (!escaped && value == '\\') {
                escaped = true;
            } else {
                escaped = false;
            }
            lexer->cursor++;
        }
        return ld_elf_script_invalid_token(
                lexer, offset, "unterminated quoted token");
    }

    if (iscntrl((unsigned char) value)) {
        return ld_elf_script_invalid_token(
                lexer, offset, "invalid control byte in linker script");
    }

    bool escaped = value == '\\';
    while (lexer->cursor < lexer->size) {
        value = lexer->data[lexer->cursor];
        if (!escaped && ld_elf_script_word_delimiter(value)) break;
        if (value == '\0') {
            return ld_elf_script_invalid_token(
                    lexer, lexer->cursor, "NUL byte in linker script");
        }
        if (!escaped && value == '\\') {
            escaped = true;
        } else {
            escaped = false;
        }
        lexer->cursor++;
    }
    if (escaped) {
        return ld_elf_script_invalid_token(
                lexer, lexer->cursor - 1U,
                "trailing backslash in linker-script token");
    }
    return (ld_elf_script_token_t) {
            .type = LD_ELF_SCRIPT_TOKEN_WORD,
            .offset = offset,
            .start = offset,
            .end = lexer->cursor,
    };
}

static void ld_elf_script_parser_next(ld_elf_script_parser_t *parser) {
    parser->token = ld_elf_script_lexer_next(&parser->lexer);
    if (parser->token.type == LD_ELF_SCRIPT_TOKEN_INVALID) {
        ld_elf_script_set_error(parser->error, parser->lexer.invalid_offset,
                                parser->lexer.invalid_message);
    }
}

static bool ld_elf_script_token_equal(const ld_elf_script_parser_t *parser,
                                      const char *value) {
    if (parser->token.type != LD_ELF_SCRIPT_TOKEN_WORD ||
        parser->token.quoted) {
        return false;
    }
    size_t length = parser->token.end - parser->token.start;
    if (strlen(value) != length) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char left = parser->lexer.data[parser->token.start + i];
        unsigned char right = (unsigned char) value[i];
        if (toupper(left) != toupper(right)) return false;
    }
    return true;
}

static char *ld_elf_script_copy_token(
        const ld_elf_script_parser_t *parser) {
    size_t length = parser->token.end - parser->token.start;
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    size_t out = 0U;
    for (size_t i = parser->token.start; i < parser->token.end; i++) {
        uint8_t value = parser->lexer.data[i];
        if (value == '\\' && i + 1U < parser->token.end) {
            value = parser->lexer.data[++i];
        }
        copy[out++] = (char) value;
    }
    copy[out] = '\0';
    return copy;
}

static bool ld_elf_script_grow(void **items, size_t *capacity,
                               size_t count, size_t element_size) {
    if (count < *capacity) return true;
    if (*capacity > SIZE_MAX / 2U) return false;
    size_t next = *capacity ? *capacity * 2U : 8U;
    if (next > SIZE_MAX / element_size) return false;
    void *grown = realloc(*items, next * element_size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

static ld_elf_script_result_t ld_elf_script_append_input(
        ld_elf_script_parser_t *parser, bool as_needed) {
    ld_elf_script_t *script = parser->script;
    if (!ld_elf_script_grow((void **) &script->inputs,
                            &script->input_capacity, script->input_count,
                            sizeof(*script->inputs))) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "out of memory recording script input");
        return LD_ELF_SCRIPT_OUT_OF_MEMORY;
    }
    char *path = ld_elf_script_copy_token(parser);
    if (!path) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "out of memory copying script input");
        return LD_ELF_SCRIPT_OUT_OF_MEMORY;
    }
    if (!*path) {
        free(path);
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "empty linker-script input path");
        return LD_ELF_SCRIPT_INVALID;
    }
    script->inputs[script->input_count++] =
            (ld_elf_script_input_t) {.path = path, .as_needed = as_needed};
    return LD_ELF_SCRIPT_OK;
}

static ld_elf_script_result_t ld_elf_script_append_search_dir(
        ld_elf_script_parser_t *parser) {
    ld_elf_script_t *script = parser->script;
    if (!ld_elf_script_grow((void **) &script->search_dirs,
                            &script->search_dir_capacity,
                            script->search_dir_count,
                            sizeof(*script->search_dirs))) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "out of memory recording SEARCH_DIR");
        return LD_ELF_SCRIPT_OUT_OF_MEMORY;
    }
    char *path = ld_elf_script_copy_token(parser);
    if (!path) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "out of memory copying SEARCH_DIR");
        return LD_ELF_SCRIPT_OUT_OF_MEMORY;
    }
    if (!*path) {
        free(path);
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "empty SEARCH_DIR path");
        return LD_ELF_SCRIPT_INVALID;
    }
    script->search_dirs[script->search_dir_count++] = path;
    return LD_ELF_SCRIPT_OK;
}

static bool ld_elf_script_format_matches(const char *format,
                                         ld_arch_t arch) {
    if (arch == LD_ARCH_AMD64)
        return strcmp(format, "elf64-x86-64") == 0;
    if (arch == LD_ARCH_ARM64)
        return strcmp(format, "elf64-littleaarch64") == 0;
    if (arch == LD_ARCH_RISCV64)
        return strcmp(format, "elf64-littleriscv") == 0;
    return false;
}

static bool ld_elf_script_arch_matches(const char *name, ld_arch_t arch) {
    if (arch == LD_ARCH_AMD64)
        return strcmp(name, "i386:x86-64") == 0 ||
               strcmp(name, "x86_64") == 0;
    if (arch == LD_ARCH_ARM64)
        return strcmp(name, "aarch64") == 0;
    if (arch == LD_ARCH_RISCV64)
        return strcmp(name, "riscv") == 0 ||
               strcmp(name, "riscv:rv64") == 0;
    return false;
}

static ld_elf_script_result_t ld_elf_script_require_lparen(
        ld_elf_script_parser_t *parser, size_t command_offset) {
    if (parser->token.type != LD_ELF_SCRIPT_TOKEN_LPAREN) {
        ld_elf_script_set_error(parser->error, command_offset,
                                "expected '(' after linker-script command");
        return LD_ELF_SCRIPT_INVALID;
    }
    ld_elf_script_parser_next(parser);
    return LD_ELF_SCRIPT_OK;
}

static ld_elf_script_result_t ld_elf_script_parse_group(
        ld_elf_script_parser_t *parser, bool as_needed, size_t depth) {
    if (depth >= LD_ELF_SCRIPT_PARSE_DEPTH) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "linker-script nesting is too deep");
        return LD_ELF_SCRIPT_INVALID;
    }
    while (parser->token.type != LD_ELF_SCRIPT_TOKEN_RPAREN) {
        if (parser->token.type == LD_ELF_SCRIPT_TOKEN_INVALID)
            return LD_ELF_SCRIPT_INVALID;
        if (parser->token.type == LD_ELF_SCRIPT_TOKEN_EOF) {
            ld_elf_script_set_error(parser->error, parser->token.offset,
                                    "unterminated INPUT or GROUP command");
            return LD_ELF_SCRIPT_INVALID;
        }
        if (parser->token.type == LD_ELF_SCRIPT_TOKEN_COMMA ||
            parser->token.type == LD_ELF_SCRIPT_TOKEN_SEMICOLON) {
            ld_elf_script_parser_next(parser);
            continue;
        }
        if (ld_elf_script_token_equal(parser, "AS_NEEDED")) {
            size_t command_offset = parser->token.offset;
            ld_elf_script_parser_next(parser);
            ld_elf_script_result_t result =
                    ld_elf_script_require_lparen(parser, command_offset);
            if (result != LD_ELF_SCRIPT_OK) return result;
            result = ld_elf_script_parse_group(parser, true, depth + 1U);
            if (result != LD_ELF_SCRIPT_OK) return result;
            continue;
        }
        if (parser->token.type != LD_ELF_SCRIPT_TOKEN_WORD) {
            ld_elf_script_set_error(parser->error, parser->token.offset,
                                    "expected input path in INPUT or GROUP");
            return LD_ELF_SCRIPT_INVALID;
        }
        ld_elf_script_result_t result =
                ld_elf_script_append_input(parser, as_needed);
        if (result != LD_ELF_SCRIPT_OK) return result;
        ld_elf_script_parser_next(parser);
    }
    ld_elf_script_parser_next(parser);
    return LD_ELF_SCRIPT_OK;
}

static ld_elf_script_result_t ld_elf_script_parse_output(
        ld_elf_script_parser_t *parser, bool output_arch,
        size_t command_offset) {
    ld_elf_script_result_t result =
            ld_elf_script_require_lparen(parser, command_offset);
    if (result != LD_ELF_SCRIPT_OK) return result;
    if (parser->token.type != LD_ELF_SCRIPT_TOKEN_WORD) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "expected architecture in output command");
        return LD_ELF_SCRIPT_INVALID;
    }
    char *value = ld_elf_script_copy_token(parser);
    if (!value) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "out of memory copying output architecture");
        return LD_ELF_SCRIPT_OUT_OF_MEMORY;
    }
    bool matches = output_arch
                           ? ld_elf_script_arch_matches(value, parser->arch)
                           : ld_elf_script_format_matches(value, parser->arch);
    free(value);
    if (!matches) {
        ld_elf_script_set_error(
                parser->error, parser->token.offset,
                output_arch ? "OUTPUT_ARCH does not match link target"
                            : "OUTPUT_FORMAT does not match link target");
        return LD_ELF_SCRIPT_UNSUPPORTED_ARCH;
    }
    ld_elf_script_parser_next(parser);
    while (parser->token.type == LD_ELF_SCRIPT_TOKEN_COMMA) {
        ld_elf_script_parser_next(parser);
        if (parser->token.type != LD_ELF_SCRIPT_TOKEN_WORD) {
            ld_elf_script_set_error(
                    parser->error, parser->token.offset,
                    "expected output format after comma");
            return LD_ELF_SCRIPT_INVALID;
        }
        ld_elf_script_parser_next(parser);
    }
    if (parser->token.type != LD_ELF_SCRIPT_TOKEN_RPAREN) {
        ld_elf_script_set_error(parser->error, parser->token.offset,
                                "expected ')' after output command");
        return LD_ELF_SCRIPT_INVALID;
    }
    ld_elf_script_parser_next(parser);
    return LD_ELF_SCRIPT_OK;
}

static ld_elf_script_result_t ld_elf_script_parse_search_dir(
        ld_elf_script_parser_t *parser, size_t command_offset) {
    ld_elf_script_result_t result =
            ld_elf_script_require_lparen(parser, command_offset);
    if (result != LD_ELF_SCRIPT_OK) return result;
    bool saw_path = false;
    while (parser->token.type != LD_ELF_SCRIPT_TOKEN_RPAREN) {
        if (parser->token.type == LD_ELF_SCRIPT_TOKEN_COMMA ||
            parser->token.type == LD_ELF_SCRIPT_TOKEN_SEMICOLON) {
            ld_elf_script_parser_next(parser);
            continue;
        }
        if (parser->token.type != LD_ELF_SCRIPT_TOKEN_WORD) {
            ld_elf_script_set_error(parser->error, parser->token.offset,
                                    "expected path in SEARCH_DIR");
            return LD_ELF_SCRIPT_INVALID;
        }
        result = ld_elf_script_append_search_dir(parser);
        if (result != LD_ELF_SCRIPT_OK) return result;
        saw_path = true;
        ld_elf_script_parser_next(parser);
    }
    if (!saw_path) {
        ld_elf_script_set_error(parser->error, command_offset,
                                "SEARCH_DIR requires a path");
        return LD_ELF_SCRIPT_INVALID;
    }
    ld_elf_script_parser_next(parser);
    return LD_ELF_SCRIPT_OK;
}

ld_elf_script_result_t ld_elf_script_parse(
        const uint8_t *data, size_t size, ld_arch_t arch,
        ld_elf_script_t *script, ld_elf_script_error_t *error) {
    if (error) memset(error, 0, sizeof(*error));
    if (!script || (!data && size != 0U)) {
        ld_elf_script_set_error(error, 0U,
                                "missing linker-script parse argument");
        return LD_ELF_SCRIPT_INVALID;
    }
    ld_elf_script_init(script);
    if (size > LD_ELF_SCRIPT_MAX_SIZE) {
        ld_elf_script_set_error(error, 0U, "linker script is too large");
        return LD_ELF_SCRIPT_INVALID;
    }

    ld_elf_script_parser_t parser = {
            .lexer = {.data = data, .size = size},
            .arch = arch,
            .script = script,
            .error = error,
    };
    ld_elf_script_parser_next(&parser);
    ld_elf_script_result_t result = LD_ELF_SCRIPT_OK;
    while (parser.token.type != LD_ELF_SCRIPT_TOKEN_EOF) {
        if (parser.token.type == LD_ELF_SCRIPT_TOKEN_INVALID) {
            result = LD_ELF_SCRIPT_INVALID;
            break;
        }
        if (parser.token.type == LD_ELF_SCRIPT_TOKEN_SEMICOLON) {
            ld_elf_script_parser_next(&parser);
            continue;
        }
        if (parser.token.type != LD_ELF_SCRIPT_TOKEN_WORD ||
            parser.token.quoted) {
            ld_elf_script_set_error(error, parser.token.offset,
                                    "expected linker-script command");
            result = LD_ELF_SCRIPT_INVALID;
            break;
        }
        size_t command_offset = parser.token.offset;
        bool output_format = ld_elf_script_token_equal(
                &parser, "OUTPUT_FORMAT");
        bool output_arch = ld_elf_script_token_equal(&parser, "OUTPUT_ARCH");
        bool input = ld_elf_script_token_equal(&parser, "INPUT");
        bool group = ld_elf_script_token_equal(&parser, "GROUP");
        bool search_dir = ld_elf_script_token_equal(&parser, "SEARCH_DIR");
        parser.saw_command = true;
        ld_elf_script_parser_next(&parser);
        if (output_format || output_arch) {
            result = ld_elf_script_parse_output(
                    &parser, output_arch, command_offset);
        } else if (input || group) {
            result = ld_elf_script_require_lparen(&parser, command_offset);
            if (result == LD_ELF_SCRIPT_OK)
                result = ld_elf_script_parse_group(&parser, false, 0U);
        } else if (search_dir) {
            result = ld_elf_script_parse_search_dir(&parser, command_offset);
        } else {
            ld_elf_script_set_error(error, command_offset,
                                    "unsupported linker-script command");
            result = LD_ELF_SCRIPT_INVALID;
        }
        if (result != LD_ELF_SCRIPT_OK) break;
    }
    if (result == LD_ELF_SCRIPT_OK && !parser.saw_command) {
        ld_elf_script_set_error(error, 0U, "empty linker script");
        result = LD_ELF_SCRIPT_INVALID;
    }
    if (result != LD_ELF_SCRIPT_OK) ld_elf_script_deinit(script);
    return result;
}

const char *ld_elf_script_result_string(ld_elf_script_result_t result) {
    switch (result) {
        case LD_ELF_SCRIPT_OK:
            return "valid GNU linker script";
        case LD_ELF_SCRIPT_INVALID:
            return "invalid GNU linker script";
        case LD_ELF_SCRIPT_UNSUPPORTED_ARCH:
            return "GNU linker script architecture mismatch";
        case LD_ELF_SCRIPT_OUT_OF_MEMORY:
            return "out of memory parsing GNU linker script";
        default:
            return "unknown GNU linker script error";
    }
}

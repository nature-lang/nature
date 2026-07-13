#include "ld_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int ld_object_push(ld_object_list_t *list, ld_object_t *object) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 32U;
        ld_object_t **items = ld_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = object;
    return LD_OK;
}

static int ld_file_push(ld_file_list_t *list, ld_file_t *file) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 16U;
        ld_file_t **items = ld_realloc_array(list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return LD_IO_ERROR;
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = file;
    return LD_OK;
}

static int ld_string_array_push(char ***items, size_t *count, size_t *capacity,
                                 ld_string_set_entry_t **set,
                                 const char *value, size_t length, size_t max_length) {
    if (!value || length == 0 || length > max_length || length == SIZE_MAX) {
        return LD_INVALID_INPUT;
    }
    ld_string_set_entry_t *existing = NULL;
    HASH_FIND(hh, *set, value, length, existing);
    if (existing) return LD_OK;
    if (*count == *capacity) {
        if (*capacity > SIZE_MAX / 2U) {
            return LD_IO_ERROR;
        }
        size_t next = *capacity ? *capacity * 2U : 128U;
        char **new_items = ld_realloc_array(*items, *capacity, next, sizeof(*new_items));
        if (!new_items) return LD_IO_ERROR;
        *items = new_items;
        *capacity = next;
    }
    char *copy = malloc(length + 1U);
    if (!copy) {
        return LD_IO_ERROR;
    }
    memcpy(copy, value, length);
    copy[length] = '\0';
    ld_string_set_entry_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        free(copy);
        return LD_IO_ERROR;
    }
    (*items)[(*count)++] = copy;
    HASH_ADD_KEYPTR(hh, *set, copy, length, entry);
    return LD_OK;
}

static int ld_dylib_export_push_raw(ld_dylib_input_t *dylib, const char *name, size_t length) {
    return ld_string_array_push(&dylib->exports, &dylib->export_count, &dylib->export_capacity,
                                 &dylib->export_set, name, length, 4096U);
}

static int ld_dylib_weak_export_push_raw(ld_dylib_input_t *dylib, const char *name, size_t length) {
    return ld_string_array_push(&dylib->weak_exports, &dylib->weak_export_count,
                                 &dylib->weak_export_capacity, &dylib->weak_export_set,
                                 name, length, 4096U);
}

/* TAPI keeps compatibility/version directives in the same symbol arrays as
   ordinary exports.  They are linker instructions, not literal symbols. */
static int ld_dylib_export_push(ld_dylib_input_t *dylib, const char *name, size_t length) {
    static const char add_prefix[] = "$ld$add$os";
    static const char weak_prefix[] = "$ld$weak$os";
    static const char hide_prefix[] = "$ld$hide$";
    const char *symbol = name;
    size_t symbol_length = length;
    bool weak = false;

    if (length >= sizeof(hide_prefix) - 1U && memcmp(name, hide_prefix, sizeof(hide_prefix) - 1U) == 0) {
        return LD_OK;
    }
    if (length >= sizeof(add_prefix) - 1U && memcmp(name, add_prefix, sizeof(add_prefix) - 1U) == 0) {
        const char *separator = memchr(name + sizeof(add_prefix) - 1U, '$',
                                       length - (sizeof(add_prefix) - 1U));
        if (separator && (size_t) (separator + 1U - name) < length) {
            symbol = separator + 1U;
            symbol_length = length - (size_t) (symbol - name);
        }
    } else if (length >= sizeof(weak_prefix) - 1U &&
               memcmp(name, weak_prefix, sizeof(weak_prefix) - 1U) == 0) {
        const char *separator = memchr(name + sizeof(weak_prefix) - 1U, '$',
                                       length - (sizeof(weak_prefix) - 1U));
        if (separator && (size_t) (separator + 1U - name) < length) {
            symbol = separator + 1U;
            symbol_length = length - (size_t) (symbol - name);
            weak = true;
        }
    }
    return weak ? ld_dylib_weak_export_push_raw(dylib, symbol, symbol_length)
                : ld_dylib_export_push_raw(dylib, symbol, symbol_length);
}

static int ld_dylib_weak_export_push(ld_dylib_input_t *dylib, const char *name, size_t length) {
    static const char hide_prefix[] = "$ld$hide$";
    static const char weak_prefix[] = "$ld$weak$os";
    if (length >= sizeof(hide_prefix) - 1U && memcmp(name, hide_prefix, sizeof(hide_prefix) - 1U) == 0) {
        return LD_OK;
    }
    if (length >= sizeof(weak_prefix) - 1U && memcmp(name, weak_prefix, sizeof(weak_prefix) - 1U) == 0) {
        return ld_dylib_export_push(dylib, name, length);
    }
    return ld_dylib_weak_export_push_raw(dylib, name, length);
}

static int ld_dylib_reexport_push(ld_dylib_input_t *dylib, const char *name, size_t length) {
    return ld_string_array_push(&dylib->reexports, &dylib->reexport_count,
                                 &dylib->reexport_capacity, &dylib->reexport_set,
                                 name, length, PATH_MAX - 1U);
}

static int ld_dylib_push(ld_context_t *ctx, const char *path, const char *install_name,
                          ld_dylib_input_t **result) {
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (strcmp(ctx->dylibs.items[i].path, path) == 0) {
            if (result) *result = &ctx->dylibs.items[i];
            return LD_OK;
        }
    }
    if (ctx->dylibs.count == ctx->dylibs.capacity) {
        size_t next = ctx->dylibs.capacity ? ctx->dylibs.capacity * 2U : 8U;
        ld_dylib_input_t *items = ld_realloc_array(ctx->dylibs.items, ctx->dylibs.capacity, next, sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR, "out of memory recording dynamic library '%s'", path);
        }
        ctx->dylibs.items = items;
        ctx->dylibs.capacity = next;
    }
    ld_dylib_input_t *dylib = &ctx->dylibs.items[ctx->dylibs.count++];
    memset(dylib, 0, sizeof(*dylib));
    dylib->current_version = LD_DEFAULT_DYLIB_VERSION;
    dylib->compatibility_version = LD_DEFAULT_DYLIB_VERSION;
    dylib->path = strdup(path);
    dylib->install_name = strdup(install_name && *install_name ? install_name : path);
    if (!dylib->path || !dylib->install_name) {
        free(dylib->path);
        free(dylib->install_name);
        memset(dylib, 0, sizeof(*dylib));
        ctx->dylibs.count--;
        return ld_fail(ctx, LD_IO_ERROR, "out of memory recording dynamic library '%s'", path);
    }
    if (result) *result = dylib;
    return LD_OK;
}


static uint32_t ld_read_u32(const uint8_t *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}


static uint32_t ld_read_be32(const uint8_t *p) {
    return ((uint32_t) p[0] << 24U) | ((uint32_t) p[1] << 16U) | ((uint32_t) p[2] << 8U) | p[3];
}

static uint64_t ld_read_be64(const uint8_t *p) {
    return ((uint64_t) ld_read_be32(p) << 32U) | ld_read_be32(p + 4);
}


static bool ld_range_ok(size_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= size - offset;
}

static char *ld_strndup0(const char *value, size_t size) {
    char *copy = malloc(size + 1U);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, size);
    copy[size] = '\0';
    return copy;
}


static int ld_read_file(ld_context_t *ctx, const char *path, ld_file_t **result) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return ld_fail(ctx, LD_IO_ERROR, "cannot open '%s': %s", path, strerror(errno));
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        int saved = errno;
        close(fd);
        return ld_fail(ctx, LD_IO_ERROR, "cannot stat '%s': %s", path, strerror(saved));
    }
    if ((uintmax_t) st.st_size > SIZE_MAX) {
        close(fd);
        return ld_fail(ctx, LD_IO_ERROR, "input '%s' is too large", path);
    }
    size_t size = (size_t) st.st_size;
    uint8_t *bytes = size ? malloc(size) : NULL;
    if (size && !bytes) {
        close(fd);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory reading '%s'", path);
    }
    size_t done = 0;
    while (done < size) {
        ssize_t got = read(fd, bytes + done, size - done);
        if (got <= 0) {
            int saved = errno;
            free(bytes);
            close(fd);
            return ld_fail(ctx, LD_IO_ERROR, "cannot read '%s': %s", path, got == 0 ? "unexpected EOF" : strerror(saved));
        }
        done += (size_t) got;
    }
    close(fd);
    ld_file_t *file = calloc(1, sizeof(*file));
    if (!file) {
        free(bytes);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory reading '%s'", path);
    }
    file->bytes = bytes;
    file->size = size;
    file->path = strdup(path);
    if (!file->path || ld_file_push(&ctx->files, file) != LD_OK) {
        free(file->path);
        free(file->bytes);
        free(file);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory reading '%s'", path);
    }
    *result = file;
    return LD_OK;
}


static bool ld_section_is_debug(const ld_section_64_t *section) {
    if ((section->flags & LD_S_ATTR_DEBUG) != 0) {
        return true;
    }
    return strncmp(section->segname, "__DWARF", 16) == 0 ||
           strncmp(section->segname, "__LLVM", 16) == 0 ||
           (strncmp(section->segname, "__LD", 16) == 0 &&
            strncmp(section->sectname, "__compact_unwind", 16) == 0);
}

static int ld_parse_object(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size,
                            const char *member_name, bool archive_member, ld_object_t **result) {
    char input_name[PATH_MAX];
    if (member_name) {
        snprintf(input_name, sizeof(input_name), "%s(%s)", file->path, member_name);
    } else {
        snprintf(input_name, sizeof(input_name), "%s", file->path);
    }
    if (size < sizeof(ld_mach_header_64_t)) {
        return ld_fail(ctx, LD_INVALID_INPUT, "'%s' is truncated", input_name);
    }
    ld_mach_header_64_t header;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != LD_MH_MAGIC_64 || header.filetype != LD_MH_OBJECT || header.cputype != LD_CPU_TYPE_ARM64) {
        return ld_fail(ctx, LD_UNSUPPORTED, "unsupported Mach-O object '%s'", input_name);
    }
    if (header.ncmds > 4096 || header.sizeofcmds > size - sizeof(header)) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid load commands in '%s'", input_name);
    }
    ld_object_t *object = calloc(1, sizeof(*object));
    if (!object) {
        return ld_fail(ctx, LD_IO_ERROR, "out of memory parsing '%s'", input_name);
    }
    object->file = file;
    object->bytes = bytes;
    object->size = size;
    object->archive_member = archive_member;
    object->selected = !archive_member;
    object->ncmds = header.ncmds;
    object->flags = header.flags;
    object->member_name = member_name ? strdup(member_name) : NULL;
    if (member_name && !object->member_name) {
        free(object);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory parsing '%s'", input_name);
    }

    size_t command_offset = sizeof(header);
    ld_symtab_command_t symtab = {0};
    bool have_symtab = false;
    for (uint32_t command_index = 0; command_index < header.ncmds; command_index++) {
        if (command_offset > size || sizeof(ld_load_command_t) > size - command_offset) {
            free(object->member_name);
            free(object);
            return ld_fail(ctx, LD_INVALID_INPUT, "truncated load command in '%s'", input_name);
        }
        ld_load_command_t command;
        memcpy(&command, bytes + command_offset, sizeof(command));
        if (command_offset - sizeof(header) > header.sizeofcmds ||
            command.cmdsize < sizeof(command) ||
            command.cmdsize > header.sizeofcmds - (command_offset - sizeof(header)) ||
            !ld_range_ok(size, command_offset, command.cmdsize)) {
            free(object->member_name);
            free(object);
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid load command size in '%s'", input_name);
        }
        if (command.cmd == LD_LC_SEGMENT_64) {
            if (command.cmdsize < sizeof(ld_segment_command_64_t)) {
                free(object->member_name);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid segment command in '%s'", input_name);
            }
            ld_segment_command_64_t segment;
            memcpy(&segment, bytes + command_offset, sizeof(segment));
            uint64_t section_bytes = (uint64_t) segment.nsects * sizeof(ld_section_64_t);
            if (segment.nsects > 255 || section_bytes > command.cmdsize - sizeof(segment)) {
                free(object->member_name);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid section list in '%s'", input_name);
            }
            size_t old_count = object->section_count;
            if ((size_t) segment.nsects > SIZE_MAX - old_count) {
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "section count overflows '%s'", input_name);
            }
            size_t new_count = old_count + segment.nsects;
            ld_input_section_t *sections = realloc(object->sections, new_count * sizeof(*sections));
            if (new_count && !sections) {
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_IO_ERROR, "out of memory parsing '%s'", input_name);
            }
            object->sections = sections;
            for (uint32_t i = 0; i < segment.nsects; i++) {
                ld_input_section_t *section = &object->sections[old_count + i];
                memset(section, 0, sizeof(*section));
                memcpy(&section->header,
                       bytes + command_offset + sizeof(segment) +
                               (size_t) i * sizeof(section->header),
                       sizeof(section->header));
                section->object = object;
                section->ignored = ld_section_is_debug(&section->header);
                if (section->header.align > 20U) {
                    uint32_t alignment = section->header.align;
                    free(object->member_name);
                    free(object->sections);
                    free(object);
                    return ld_fail(ctx, LD_UNSUPPORTED,
                                    "section alignment exponent %u is unsupported in '%s'",
                                    alignment, input_name);
                }
                if ((section->header.flags & LD_SECTION_TYPE) != LD_S_ZEROFILL &&
                    (section->header.flags & LD_SECTION_TYPE) != LD_S_GB_ZEROFILL &&
                    (section->header.flags & LD_SECTION_TYPE) != LD_S_THREAD_LOCAL_ZEROFILL) {
                    if (!ld_range_ok(size, section->header.offset, section->header.size)) {
                        free(object->member_name);
                        free(object->sections);
                        free(object);
                        return ld_fail(ctx, LD_INVALID_INPUT, "section outside object '%s'", input_name);
                    }
                    section->data = bytes + section->header.offset;
                }
                if (section->header.nreloc) {
                    uint32_t section_type = section->header.flags & LD_SECTION_TYPE;
                    if (section_type == LD_S_ZEROFILL || section_type == LD_S_GB_ZEROFILL ||
                        section_type == LD_S_THREAD_LOCAL_ZEROFILL) {
                        free(object->member_name);
                        free(object->sections);
                        free(object);
                        return ld_fail(ctx, LD_INVALID_INPUT, "zerofill section has relocations in '%s'", input_name);
                    }
                    uint64_t relocation_size = (uint64_t) section->header.nreloc * 8U;
                    if (!ld_range_ok(size, section->header.reloff, relocation_size)) {
                        free(object->member_name);
                        free(object->sections);
                        free(object);
                        return ld_fail(ctx, LD_INVALID_INPUT, "relocations outside object '%s'", input_name);
                    }
                    section->relocations = bytes + section->header.reloff;
                }
            }
            object->section_count = new_count;
        } else if (command.cmd == LD_LC_SYMTAB) {
            if (command.cmdsize < sizeof(ld_symtab_command_t)) {
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid symbol table command in '%s'", input_name);
            }
            memcpy(&symtab, bytes + command_offset, sizeof(symtab));
            have_symtab = true;
        } else if (command.cmd == LD_LC_DATA_IN_CODE) {
            if (command.cmdsize < sizeof(ld_linkedit_data_command_t)) {
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid data-in-code command in '%s'", input_name);
            }
            ld_linkedit_data_command_t data_in_code;
            memcpy(&data_in_code, bytes + command_offset, sizeof(data_in_code));
            if (data_in_code.datasize % sizeof(ld_data_in_code_entry_t) != 0 ||
                !ld_range_ok(size, data_in_code.dataoff, data_in_code.datasize)) {
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "data-in-code table outside '%s'", input_name);
            }
            object->data_in_code = bytes + data_in_code.dataoff;
            object->data_in_code_count = data_in_code.datasize / sizeof(ld_data_in_code_entry_t);
        }
        command_offset += command.cmdsize;
    }
    if (!have_symtab ||
        !ld_range_ok(size, symtab.symoff, (uint64_t) symtab.nsyms * sizeof(ld_nlist_64_t)) ||
        !ld_range_ok(size, symtab.stroff, symtab.strsize) || symtab.strsize == 0) {
        free(object->member_name);
        free(object->sections);
        free(object);
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid symbol table in '%s'", input_name);
    }
    object->symbol_count = symtab.nsyms;
    object->symbols = calloc(object->symbol_count ? object->symbol_count : 1U, sizeof(*object->symbols));
    if (!object->symbols) {
        free(object->member_name);
        free(object->sections);
        free(object);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory parsing symbols in '%s'", input_name);
    }
    object->strtab = (const char *) (bytes + symtab.stroff);
    object->strtab_size = symtab.strsize;
    for (uint32_t i = 0; i < symtab.nsyms; i++) {
        ld_nlist_64_t source_symbol;
        memcpy(&source_symbol, bytes + symtab.symoff + (size_t) i * sizeof(source_symbol),
               sizeof(source_symbol));
        object->symbols[i].entry = source_symbol;
        object->symbols[i].object = object;
        object->symbols[i].index = i;
        uint8_t symbol_type = source_symbol.n_type & LD_N_TYPE;
        if (symbol_type == LD_N_SECT &&
            (source_symbol.n_sect == 0 || source_symbol.n_sect > object->section_count)) {
            free(object->symbols);
            free(object->member_name);
            free(object->sections);
            free(object);
            return ld_fail(ctx, LD_INVALID_INPUT, "symbol has invalid section index in '%s'", input_name);
        }
        if (symbol_type == LD_N_SECT && (source_symbol.n_type & LD_N_STAB) == 0) {
            const ld_input_section_t *section = &object->sections[source_symbol.n_sect - 1U];
            if (section->header.addr > UINT64_MAX - section->header.size ||
                source_symbol.n_value < section->header.addr ||
                source_symbol.n_value - section->header.addr > section->header.size) {
                free(object->symbols);
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT, "symbol value is outside its section in '%s'", input_name);
            }
        }
        if (source_symbol.n_strx < symtab.strsize) {
            const char *name = object->strtab + source_symbol.n_strx;
            size_t max = symtab.strsize - source_symbol.n_strx;
            if (memchr(name, '\0', max)) {
                object->symbols[i].name = name;
            }
        }
        if (symbol_type == LD_N_INDR) {
            /* For an indirect symbol n_value is a string-table index, not an
               address.  Keep the target name in the same mapped string table
               so it remains valid for the lifetime of the input file. */
            if (source_symbol.n_value >= symtab.strsize) {
                free(object->symbols);
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "indirect symbol target is outside string table in '%s'", input_name);
            }
            const char *alias_name = object->strtab + source_symbol.n_value;
            size_t max = symtab.strsize - (size_t) source_symbol.n_value;
            const char *terminator = memchr(alias_name, '\0', max);
            if (!terminator || terminator == alias_name) {
                free(object->symbols);
                free(object->member_name);
                free(object->sections);
                free(object);
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "indirect symbol target has invalid name in '%s'", input_name);
            }
            object->symbols[i].alias_name = alias_name;
        }
        if ((source_symbol.n_type & LD_N_EXT) != 0 && source_symbol.n_strx != 0 &&
            !object->symbols[i].name) {
            free(object->symbols);
            free(object->member_name);
            free(object->sections);
            free(object);
            return ld_fail(ctx, LD_INVALID_INPUT, "symbol has invalid string index in '%s'", input_name);
        }
    }
    if (ld_object_push(&ctx->objects, object) != LD_OK) {
        free(object->symbols);
        free(object->member_name);
        free(object->sections);
        free(object);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory adding '%s'", input_name);
    }
    *result = object;
    return LD_OK;
}

static bool ld_archive_decimal(const uint8_t *value, size_t size, uint64_t *result) {
    uint64_t parsed = 0;
    bool saw_digit = false;
    bool saw_trailing_space = false;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = value[i];
        if (c == ' ') {
            if (saw_digit) saw_trailing_space = true;
            continue;
        }
        if (c < '0' || c > '9' || saw_trailing_space) {
            return false;
        }
        uint8_t digit = c - '0';
        if (parsed > (UINT64_MAX - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
        saw_digit = true;
    }
    if (!saw_digit) {
        return false;
    }
    *result = parsed;
    return true;
}

static bool ld_span_contains(const char *start, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0 || needle_length > length) {
        return false;
    }
    for (size_t i = 0; i <= length - needle_length; i++) {
        if (memcmp(start + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static const char *ld_span_find(const char *start, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0 || needle_length > length) {
        return NULL;
    }
    for (size_t i = 0; i <= length - needle_length; i++) {
        if (memcmp(start + i, needle, needle_length) == 0) {
            return start + i;
        }
    }
    return NULL;
}

static const char *ld_tbd_key_find(const char *start, size_t length, const char *key) {
    const char *match = start;
    size_t key_length = strlen(key);
    while ((match = ld_span_find(match, length - (size_t) (match - start), key)) != NULL) {
        size_t position = (size_t) (match - start);
        if ((position == 0 || isspace((unsigned char) start[position - 1U])) &&
            !(position > 0 && start[position - 1U] == '-')) {
            return match;
        }
        match += key_length;
        if ((size_t) (match - start) >= length) break;
    }
    return NULL;
}

static char *ld_tbd_scalar(const char *value, const char *line_end) {
    while (value < line_end && isspace((unsigned char) *value)) value++;
    while (line_end > value && isspace((unsigned char) line_end[-1])) line_end--;
    if (value == line_end) {
        return NULL;
    }
    char quote = 0;
    if (*value == '\'' || *value == '"') {
        quote = *value++;
        if (line_end > value && line_end[-1] == quote) line_end--;
    }
    (void) quote;
    return ld_strndup0(value, (size_t) (line_end - value));
}

static bool ld_tbd_version(const char *value, const char *line_end, uint32_t *version) {
    char *scalar = ld_tbd_scalar(value, line_end);
    if (!scalar) return false;

    uint32_t components[3] = {0};
    const char *cursor = scalar;
    bool valid = true;
    for (size_t component = 0; component < 3U; component++) {
        if (!isdigit((unsigned char) *cursor)) {
            valid = false;
            break;
        }
        uint32_t limit = component == 0 ? UINT16_MAX : UINT8_MAX;
        uint32_t parsed = 0;
        do {
            uint32_t digit = (uint32_t) (*cursor - '0');
            if (parsed > (limit - digit) / 10U) {
                valid = false;
                break;
            }
            parsed = parsed * 10U + digit;
            cursor++;
        } while (isdigit((unsigned char) *cursor));
        if (!valid) break;
        components[component] = parsed;
        if (*cursor == '\0') break;
        if (*cursor != '.' || component == 2U) {
            valid = false;
            break;
        }
        cursor++;
    }
    if (valid && *cursor == '\0') {
        *version = (components[0] << 16U) | (components[1] << 8U) | components[2];
    } else {
        valid = false;
    }
    free(scalar);
    return valid;
}

static int ld_tbd_parse_version_key(const char *line_start, const char *line_end,
                                     const char *key, uint32_t *version) {
    const char *match = ld_tbd_key_find(line_start, (size_t) (line_end - line_start), key);
    if (!match || match >= line_end) return LD_OK;
    const char *value = match + strlen(key);
    return ld_tbd_version(value, line_end, version) ? LD_OK : LD_INVALID_INPUT;
}

static int ld_tbd_parse_symbol_list(ld_dylib_input_t *dylib, const char *start, const char *end) {
    const char *cursor = start;
    while (cursor < end) {
        while (cursor < end && (isspace((unsigned char) *cursor) || *cursor == ',' || *cursor == '[' ||
                                *cursor == ']')) {
            cursor++;
        }
        if (cursor == end) break;
        char quote = 0;
        if (*cursor == '\'' || *cursor == '"') {
            quote = *cursor++;
        }
        const char *name = cursor;
        if (quote) {
            while (cursor < end && *cursor != quote) cursor++;
        } else {
            while (cursor < end && *cursor != ',' && *cursor != ']' && !isspace((unsigned char) *cursor)) cursor++;
        }
        size_t length = (size_t) (cursor - name);
        if (length) {
            int result = ld_dylib_export_push(dylib, name, length);
            if (result != LD_OK) return result;
        }
        if (quote && cursor < end) cursor++;
    }
    return LD_OK;
}

static int ld_tbd_parse_string_key(ld_dylib_input_t *dylib, const char *line_start,
                                    const char *line_end, const char *document_end,
                                    const char *key, bool weak, bool reexport) {
    const char *match = ld_tbd_key_find(line_start, (size_t) (line_end - line_start), key);
    if (!match || match >= line_end) return LD_OK;
    const char *list_start = memchr(match, '[', (size_t) (line_end - match));
    if (!list_start) return LD_OK;
    const char *list_end = memchr(list_start, ']', (size_t) (document_end - list_start));
    if (!list_end) return LD_INVALID_INPUT;
    const char *cursor = list_start + 1U;
    while (cursor < list_end) {
        while (cursor < list_end && (isspace((unsigned char) *cursor) || *cursor == ',')) cursor++;
        if (cursor >= list_end) break;
        char quote = 0;
        if (*cursor == '\'' || *cursor == '"') quote = *cursor++;
        const char *value = cursor;
        if (quote) {
            while (cursor < list_end && *cursor != quote) cursor++;
        } else {
            while (cursor < list_end && *cursor != ',' && !isspace((unsigned char) *cursor)) cursor++;
        }
        size_t length = (size_t) (cursor - value);
        if (length) {
            int result = reexport ? ld_dylib_reexport_push(dylib, value, length)
                         : weak   ? ld_dylib_weak_export_push(dylib, value, length)
                                  : ld_dylib_export_push(dylib, value, length);
            if (result != LD_OK) return result;
        }
        if (quote && cursor < list_end) cursor++;
    }
    return LD_OK;
}

typedef enum {
    LD_TBD_OBJC_CLASS,
    LD_TBD_OBJC_EH_TYPE,
    LD_TBD_OBJC_IVAR,
} ld_tbd_objc_kind_t;

static int ld_tbd_push_objc_symbol(ld_dylib_input_t *dylib, const char *prefix,
                                    const char *name, size_t name_length, bool weak) {
    size_t prefix_length = strlen(prefix);
    if (name_length > 4096U - prefix_length) return LD_INVALID_INPUT;
    size_t symbol_length = prefix_length + name_length;
    char *symbol = malloc(symbol_length + 1U);
    if (!symbol) return LD_IO_ERROR;
    memcpy(symbol, prefix, prefix_length);
    memcpy(symbol + prefix_length, name, name_length);
    symbol[symbol_length] = '\0';
    int result = weak ? ld_dylib_weak_export_push(dylib, symbol, symbol_length)
                      : ld_dylib_export_push(dylib, symbol, symbol_length);
    free(symbol);
    return result;
}

static int ld_tbd_push_objc_export(ld_dylib_input_t *dylib, ld_tbd_objc_kind_t kind,
                                    const char *name, size_t name_length, bool weak) {
    if (kind == LD_TBD_OBJC_CLASS) {
        int result = ld_tbd_push_objc_symbol(dylib, "_OBJC_CLASS_$_", name, name_length, weak);
        if (result != LD_OK) return result;
        return ld_tbd_push_objc_symbol(dylib, "_OBJC_METACLASS_$_", name, name_length, weak);
    }
    const char *prefix = kind == LD_TBD_OBJC_EH_TYPE ? "_OBJC_EHTYPE_$_" : "_OBJC_IVAR_$_";
    return ld_tbd_push_objc_symbol(dylib, prefix, name, name_length, weak);
}

static int ld_tbd_parse_objc_key(ld_dylib_input_t *dylib, const char *line_start,
                                  const char *line_end, const char *document_end,
                                  const char *key, ld_tbd_objc_kind_t kind, bool weak) {
    const char *match = ld_tbd_key_find(line_start, (size_t) (line_end - line_start), key);
    if (!match || match >= line_end) return LD_OK;
    const char *list_start = memchr(match, '[', (size_t) (line_end - match));
    if (!list_start) return LD_INVALID_INPUT;
    const char *list_end = memchr(list_start, ']', (size_t) (document_end - list_start));
    if (!list_end) return LD_INVALID_INPUT;

    const char *cursor = list_start + 1U;
    while (cursor < list_end) {
        while (cursor < list_end && (isspace((unsigned char) *cursor) || *cursor == ',')) cursor++;
        if (cursor == list_end) break;
        char quote = 0;
        if (*cursor == '\'' || *cursor == '"') quote = *cursor++;
        const char *name = cursor;
        if (quote) {
            while (cursor < list_end && *cursor != quote) cursor++;
            if (cursor == list_end) return LD_INVALID_INPUT;
        } else {
            while (cursor < list_end && *cursor != ',' && !isspace((unsigned char) *cursor)) cursor++;
        }
        size_t name_length = (size_t) (cursor - name);
        if (name_length) {
            int result = ld_tbd_push_objc_export(dylib, kind, name, name_length, weak);
            if (result != LD_OK) return result;
        }
        if (quote) cursor++;
    }
    return LD_OK;
}

static void ld_dylib_input_deinit(ld_dylib_input_t *dylib) {
    free(dylib->path);
    free(dylib->install_name);
    ld_string_set_deinit(&dylib->export_set);
    for (size_t i = 0; i < dylib->export_count; i++) free(dylib->exports[i]);
    free(dylib->exports);
    ld_string_set_deinit(&dylib->weak_export_set);
    for (size_t i = 0; i < dylib->weak_export_count; i++) free(dylib->weak_exports[i]);
    free(dylib->weak_exports);
    ld_string_set_deinit(&dylib->reexport_set);
    for (size_t i = 0; i < dylib->reexport_count; i++) free(dylib->reexports[i]);
    free(dylib->reexports);
    memset(dylib, 0, sizeof(*dylib));
}

static int ld_parse_tbd(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size) {
    if (size == 0 || memchr(bytes, '\0', size)) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid text-based stub '%s'", file->path);
    }
    const char *text = (const char *) bytes;
    const char *end = text + size;
    if (!ld_span_contains(text, size < 512U ? size : 512U, "tapi-tbd") &&
        !ld_span_contains(text, size < 512U ? size : 512U, "tbd-version:")) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid text-based stub '%s'", file->path);
    }
    ld_dylib_input_t parsed = {0};
    parsed.current_version = LD_DEFAULT_DYLIB_VERSION;
    parsed.compatibility_version = LD_DEFAULT_DYLIB_VERSION;
    parsed.path = strdup(file->path);
    bool arm64_target = true;
    bool have_target_key = false;
    bool in_reexport_libraries = false;
    bool primary_document = true;
    bool saw_document_start = false;
    const char *cursor = text;
    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t) (end - cursor));
        if (!line_end) line_end = end;
        const char *trimmed = cursor;
        while (trimmed < line_end && isspace((unsigned char) *trimmed)) trimmed++;
        if (ld_span_find(trimmed, (size_t) (line_end - trimmed), "---") == trimmed) {
            if (saw_document_start || parsed.install_name) {
                primary_document = false;
            } else {
                saw_document_start = true;
            }
            arm64_target = true;
            have_target_key = false;
            in_reexport_libraries = false;
        }
        if (ld_span_find(trimmed, (size_t) (line_end - trimmed), "reexported-libraries:") == trimmed) {
            in_reexport_libraries = true;
        } else if (ld_span_find(trimmed, (size_t) (line_end - trimmed), "exports:") == trimmed ||
                   ld_span_find(trimmed, (size_t) (line_end - trimmed), "reexports:") == trimmed ||
                   ld_span_find(trimmed, (size_t) (line_end - trimmed), "re-exports:") == trimmed) {
            in_reexport_libraries = false;
        }
        const char *targets = ld_tbd_key_find(trimmed, (size_t) (line_end - trimmed), "targets:");
        if (targets && targets < line_end) {
            const char *list_end = memchr(targets, ']', (size_t) (end - targets));
            if (!list_end) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "unterminated targets list in '%s'", file->path);
            }
            arm64_target = ld_span_contains(targets, (size_t) (list_end - targets), "arm64-macos");
            have_target_key = true;
        }
        const char *archs = ld_tbd_key_find(trimmed, (size_t) (line_end - trimmed), "archs:");
        if (archs && archs < line_end) {
            const char *list_end = memchr(archs, ']', (size_t) (end - archs));
            if (!list_end) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "unterminated arch list in '%s'", file->path);
            }
            arm64_target = ld_span_contains(archs, (size_t) (list_end - archs), "arm64");
            have_target_key = true;
        }
        const char *platform = ld_tbd_key_find(trimmed, (size_t) (line_end - trimmed), "platform:");
        if (platform && platform < line_end && have_target_key &&
            !ld_span_contains(platform, (size_t) (line_end - platform), "macos")) {
            arm64_target = false;
        }
        static const char install_key[] = "install-name:";
        const char *install = ld_span_find(trimmed, (size_t) (line_end - trimmed), install_key);
        if (!parsed.install_name && install && install < line_end) {
            parsed.install_name = ld_tbd_scalar(install + sizeof(install_key) - 1U, line_end);
            if (!parsed.install_name) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid install-name in '%s'", file->path);
            }
        }
        if (primary_document) {
            int version_result = ld_tbd_parse_version_key(trimmed, line_end, "current-version:",
                                                           &parsed.current_version);
            if (version_result == LD_OK) {
                version_result = ld_tbd_parse_version_key(trimmed, line_end, "compatibility-version:",
                                                           &parsed.compatibility_version);
            }
            if (version_result != LD_OK) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid dylib version in '%s'", file->path);
            }
        }
        const char *symbols = ld_tbd_key_find(trimmed, (size_t) (line_end - trimmed), "symbols:");
        if (arm64_target && symbols && symbols < line_end) {
            const char *list_start = memchr(symbols, '[', (size_t) (end - symbols));
            if (!list_start) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid symbols list in '%s'", file->path);
            }
            const char *list_end = memchr(list_start, ']', (size_t) (end - list_start));
            if (!list_end) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "unterminated symbols list in '%s'", file->path);
            }
            int result = ld_tbd_parse_symbol_list(&parsed, list_start + 1, list_end);
            if (result != LD_OK) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, result, "cannot parse symbols in '%s'", file->path);
            }
        }
        if (arm64_target) {
            int weak_result = ld_tbd_parse_string_key(&parsed, trimmed, line_end, end,
                                                       "weak-symbols:", true, false);
            if (weak_result == LD_INVALID_INPUT) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid weak-symbols list in '%s'", file->path);
            }
            if (weak_result != LD_OK) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, weak_result, "cannot parse weak symbols in '%s'", file->path);
            }
            static const struct {
                const char *key;
                ld_tbd_objc_kind_t kind;
                bool weak;
            } objc_keys[] = {
                    {"objc-classes:", LD_TBD_OBJC_CLASS, false},
                    {"weak-objc-classes:", LD_TBD_OBJC_CLASS, true},
                    {"objc-eh-types:", LD_TBD_OBJC_EH_TYPE, false},
                    {"objc-ivars:", LD_TBD_OBJC_IVAR, false},
                    {"weak-objc-ivars:", LD_TBD_OBJC_IVAR, true},
            };
            for (size_t i = 0; i < sizeof(objc_keys) / sizeof(objc_keys[0]); i++) {
                int objc_result = ld_tbd_parse_objc_key(&parsed, trimmed, line_end, end,
                                                         objc_keys[i].key, objc_keys[i].kind,
                                                         objc_keys[i].weak);
                if (objc_result != LD_OK) {
                    ld_dylib_input_deinit(&parsed);
                    return ld_fail(ctx, objc_result, "cannot parse %s list in '%s'",
                                    objc_keys[i].key, file->path);
                }
            }
            int reexport_result = LD_OK;
            if (in_reexport_libraries) {
                reexport_result = ld_tbd_parse_string_key(&parsed, trimmed, line_end, end,
                                                           "libraries:", false, true);
            }
            if (ld_span_find(trimmed, (size_t) (line_end - trimmed), "reexported-libraries:") == trimmed) {
                reexport_result = ld_tbd_parse_string_key(&parsed, trimmed, line_end, end,
                                                           "reexported-libraries:", false, true);
            }
            if (reexport_result == LD_INVALID_INPUT) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid reexported-libraries list in '%s'", file->path);
            }
            if (reexport_result != LD_OK) {
                ld_dylib_input_deinit(&parsed);
                return ld_fail(ctx, reexport_result, "cannot parse reexported libraries in '%s'", file->path);
            }
        }
        cursor = line_end < end ? line_end + 1 : end;
    }
    if (!parsed.path || !parsed.install_name) {
        ld_dylib_input_deinit(&parsed);
        return ld_fail(ctx, LD_INVALID_INPUT, "text-based stub '%s' has no install-name", file->path);
    }
    ld_dylib_input_t *dylib = NULL;
    int result = ld_dylib_push(ctx, parsed.path, parsed.install_name, &dylib);
    if (result == LD_OK) {
        dylib->current_version = parsed.current_version;
        dylib->compatibility_version = parsed.compatibility_version;
        for (size_t i = 0; i < parsed.export_count; i++) {
            result = ld_dylib_export_push(dylib, parsed.exports[i], strlen(parsed.exports[i]));
            if (result != LD_OK) break;
        }
        for (size_t i = 0; result == LD_OK && i < parsed.weak_export_count; i++) {
            result = ld_dylib_weak_export_push(dylib, parsed.weak_exports[i], strlen(parsed.weak_exports[i]));
        }
        for (size_t i = 0; result == LD_OK && i < parsed.reexport_count; i++) {
            result = ld_dylib_reexport_push(dylib, parsed.reexports[i], strlen(parsed.reexports[i]));
        }
    }
    ld_dylib_input_deinit(&parsed);
    if (result != LD_OK) {
        return ld_fail(ctx, result, "cannot record text-based stub '%s'", file->path);
    }
    return LD_OK;
}

static bool ld_export_read_uleb(const uint8_t *data, size_t size, size_t *offset, uint64_t *value) {
    uint64_t result = 0;
    for (unsigned shift = 0; shift < 64U && *offset < size; shift += 7U) {
        uint8_t byte = data[(*offset)++];
        if (shift == 63U && (byte & 0x7eU)) return false;
        result |= (uint64_t) (byte & 0x7fU) << shift;
        if (!(byte & 0x80U)) {
            *value = result;
            return true;
        }
    }
    return false;
}

static int ld_parse_export_node(ld_context_t *ctx, const char *path, ld_dylib_input_t *dylib,
                                 const uint8_t *data, size_t size, uint64_t node_offset,
                                 char *name, size_t name_length, uint8_t *visited, unsigned depth) {
    if (node_offset >= size || name_length >= 4096U || depth > 256U) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie in '%s'", path);
    }
    if (visited[node_offset]) return LD_OK;
    visited[node_offset] = 1;
    size_t offset = (size_t) node_offset;
    uint64_t terminal_size;
    if (!ld_export_read_uleb(data, size, &offset, &terminal_size) || terminal_size > size - offset) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie terminal in '%s'", path);
    }
    size_t terminal_end = offset + (size_t) terminal_size;
    if (terminal_size && name_length) {
        uint64_t flags;
        if (!ld_export_read_uleb(data, terminal_end, &offset, &flags)) {
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie flags in '%s'", path);
        }
        if (flags & LD_EXPORT_SYMBOL_FLAGS_REEXPORT) {
            uint64_t ordinal;
            if (!ld_export_read_uleb(data, terminal_end, &offset, &ordinal) || offset >= terminal_end ||
                !memchr(data + offset, '\0', terminal_end - offset)) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie reexport in '%s'", path);
            }
            (void) ordinal;
            offset += strlen((const char *) data + offset) + 1U;
        } else if (flags & LD_EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) {
            uint64_t address, resolver;
            if (!ld_export_read_uleb(data, terminal_end, &offset, &address) ||
                !ld_export_read_uleb(data, terminal_end, &offset, &resolver)) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie resolver in '%s'", path);
            }
        } else {
            uint64_t address;
            if (!ld_export_read_uleb(data, terminal_end, &offset, &address)) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie address in '%s'", path);
            }
        }
        int result = (flags & LD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION)
                             ? ld_dylib_weak_export_push(dylib, name, name_length)
                             : ld_dylib_export_push(dylib, name, name_length);
        if (result != LD_OK) return ld_fail(ctx, result, "cannot record dylib export from '%s'", path);
    }
    offset = terminal_end;
    if (offset >= size) return LD_OK;
    uint8_t child_count = data[offset++];
    for (uint8_t child = 0; child < child_count; child++) {
        const char *edge = (const char *) data + offset;
        size_t remaining = size - offset;
        const char *terminator = memchr(edge, '\0', remaining);
        if (!terminator) return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie edge in '%s'", path);
        size_t edge_length = (size_t) (terminator - edge);
        offset += edge_length + 1U;
        uint64_t child_offset;
        if (!ld_export_read_uleb(data, size, &offset, &child_offset) ||
            edge_length > 4095U - name_length) {
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie child in '%s'", path);
        }
        memcpy(name + name_length, edge, edge_length);
        name[name_length + edge_length] = '\0';
        int result = ld_parse_export_node(ctx, path, dylib, data, size, child_offset, name,
                                           name_length + edge_length, visited, depth + 1U);
        if (result != LD_OK) return result;
    }
    return LD_OK;
}

static int ld_parse_dylib(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size) {
    if (size < sizeof(ld_mach_header_64_t)) {
        return ld_fail(ctx, LD_INVALID_INPUT, "truncated dylib '%s'", file->path);
    }
    ld_mach_header_64_t header;
    memcpy(&header, bytes, sizeof(header));
    if (header.magic != LD_MH_MAGIC_64 || header.filetype != LD_MH_DYLIB ||
        header.cputype != LD_CPU_TYPE_ARM64) {
        return ld_fail(ctx, LD_UNSUPPORTED, "unsupported dylib '%s'", file->path);
    }
    if (header.ncmds > 4096U || header.sizeofcmds > size - sizeof(header)) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid dylib load commands in '%s'", file->path);
    }
    ld_symtab_command_t symtab = {0};
    bool have_symtab = false;
    const uint8_t *export_trie = NULL;
    size_t export_trie_size = 0;
    char *install_name = NULL;
    uint32_t current_version = LD_DEFAULT_DYLIB_VERSION;
    uint32_t compatibility_version = LD_DEFAULT_DYLIB_VERSION;
    size_t command_offset = sizeof(header);
    for (uint32_t i = 0; i < header.ncmds; i++) {
        if (!ld_range_ok(size, command_offset, sizeof(ld_load_command_t))) {
            free(install_name);
            return ld_fail(ctx, LD_INVALID_INPUT, "truncated dylib load command in '%s'", file->path);
        }
        ld_load_command_t command;
        memcpy(&command, bytes + command_offset, sizeof(command));
        size_t consumed = command_offset - sizeof(header);
        if (consumed > header.sizeofcmds || command.cmdsize < sizeof(command) ||
            command.cmdsize > header.sizeofcmds - consumed || !ld_range_ok(size, command_offset, command.cmdsize)) {
            free(install_name);
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid dylib load command in '%s'", file->path);
        }
        if (command.cmd == LD_LC_ID_DYLIB) {
            if (command.cmdsize < sizeof(ld_dylib_command_t)) {
                free(install_name);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid LC_ID_DYLIB in '%s'", file->path);
            }
            ld_dylib_command_t id;
            memcpy(&id, bytes + command_offset, sizeof(id));
            if (id.name_offset >= command.cmdsize) {
                free(install_name);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid dylib install-name in '%s'", file->path);
            }
            const char *name = (const char *) bytes + command_offset + id.name_offset;
            size_t available = command.cmdsize - id.name_offset;
            const char *terminator = memchr(name, '\0', available);
            if (!terminator) {
                free(install_name);
                return ld_fail(ctx, LD_INVALID_INPUT, "unterminated dylib install-name in '%s'", file->path);
            }
            free(install_name);
            install_name = ld_strndup0(name, (size_t) (terminator - name));
            if (!install_name) {
                return ld_fail(ctx, LD_IO_ERROR, "out of memory reading dylib '%s'", file->path);
            }
            current_version = id.current_version;
            compatibility_version = id.compatibility_version;
        } else if (command.cmd == LD_LC_SYMTAB) {
            if (command.cmdsize < sizeof(ld_symtab_command_t)) {
                free(install_name);
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid dylib symbol table in '%s'", file->path);
            }
            memcpy(&symtab, bytes + command_offset, sizeof(symtab));
            have_symtab = true;
        } else if (command.cmd == LD_LC_DYLD_EXPORTS_TRIE || command.cmd == LD_LC_DYLD_INFO_ONLY) {
            if (command.cmd == LD_LC_DYLD_EXPORTS_TRIE) {
                if (command.cmdsize < sizeof(ld_linkedit_data_command_t)) {
                    free(install_name);
                    return ld_fail(ctx, LD_INVALID_INPUT, "invalid export trie command in '%s'", file->path);
                }
                ld_linkedit_data_command_t exports;
                memcpy(&exports, bytes + command_offset, sizeof(exports));
                if (!ld_range_ok(size, exports.dataoff, exports.datasize)) {
                    free(install_name);
                    return ld_fail(ctx, LD_INVALID_INPUT, "export trie outside '%s'", file->path);
                }
                export_trie = bytes + exports.dataoff;
                export_trie_size = exports.datasize;
            } else {
                if (command.cmdsize < sizeof(ld_dyld_info_command_t)) {
                    free(install_name);
                    return ld_fail(ctx, LD_INVALID_INPUT, "invalid dyld info command in '%s'", file->path);
                }
                ld_dyld_info_command_t info;
                memcpy(&info, bytes + command_offset, sizeof(info));
                if (info.export_size && !ld_range_ok(size, info.export_off, info.export_size)) {
                    free(install_name);
                    return ld_fail(ctx, LD_INVALID_INPUT, "export trie outside '%s'", file->path);
                }
                if (info.export_size) {
                    export_trie = bytes + info.export_off;
                    export_trie_size = info.export_size;
                }
            }
        }
        command_offset += command.cmdsize;
    }
    ld_dylib_input_t *dylib = NULL;
    int result = ld_dylib_push(ctx, file->path, install_name ? install_name : file->path, &dylib);
    free(install_name);
    if (result != LD_OK) return result;
    dylib->current_version = current_version;
    dylib->compatibility_version = compatibility_version;
    command_offset = sizeof(header);
    for (uint32_t i = 0; i < header.ncmds; i++) {
        ld_load_command_t command;
        memcpy(&command, bytes + command_offset, sizeof(command));
        /* A producer may set MH_NO_REEXPORTED_DYLIBS while retaining stale
           LC_REEXPORT_DYLIB commands for compatibility.  dyld and Zig ignore
           those commands in that mode; treating them as dependencies here
           would make an otherwise valid dylib contribute symbols it does not
           actually re-export. */
        if (command.cmd == LD_LC_REEXPORT_DYLIB &&
            (header.flags & LD_MH_NO_REEXPORTED_DYLIBS) == 0) {
            if (command.cmdsize < sizeof(ld_dylib_command_t)) {
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "invalid LC_REEXPORT_DYLIB in '%s'", file->path);
            }
            ld_dylib_command_t reexport;
            memcpy(&reexport, bytes + command_offset, sizeof(reexport));
            if (reexport.name_offset >= command.cmdsize) {
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "invalid reexport install-name in '%s'", file->path);
            }
            const char *name = (const char *) bytes + command_offset + reexport.name_offset;
            size_t available = command.cmdsize - reexport.name_offset;
            const char *terminator = memchr(name, '\0', available);
            if (!terminator || terminator == name) {
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "unterminated reexport install-name in '%s'", file->path);
            }
            result = ld_dylib_reexport_push(dylib, name, (size_t) (terminator - name));
            if (result != LD_OK) {
                return ld_fail(ctx, result,
                                "cannot record dylib reexport from '%s'", file->path);
            }
        }
        command_offset += command.cmdsize;
    }
    if (have_symtab) {
        uint64_t symbol_bytes = (uint64_t) symtab.nsyms * sizeof(ld_nlist_64_t);
        if (!ld_range_ok(size, symtab.symoff, symbol_bytes) || !ld_range_ok(size, symtab.stroff, symtab.strsize)) {
            return ld_fail(ctx, LD_INVALID_INPUT, "dylib symbol table outside '%s'", file->path);
        }
        const char *strings = (const char *) bytes + symtab.stroff;
        for (uint32_t i = 0; i < symtab.nsyms; i++) {
            ld_nlist_64_t symbol;
            memcpy(&symbol, bytes + symtab.symoff + (size_t) i * sizeof(symbol),
                   sizeof(symbol));
            uint8_t type = symbol.n_type;
            uint8_t base_type = type & LD_N_TYPE;
            if ((type & LD_N_STAB) != 0 || (type & LD_N_EXT) == 0 ||
                (base_type != LD_N_SECT && base_type != LD_N_ABS && base_type != LD_N_INDR) ||
                symbol.n_strx >= symtab.strsize) continue;
            const char *name = strings + symbol.n_strx;
            size_t available = symtab.strsize - symbol.n_strx;
            const char *terminator = memchr(name, '\0', available);
            if (!terminator || terminator == name) continue;
            result = (symbol.n_desc & LD_N_WEAK_DEF)
                             ? ld_dylib_weak_export_push(dylib, name, (size_t) (terminator - name))
                             : ld_dylib_export_push(dylib, name, (size_t) (terminator - name));
            if (result != LD_OK) return ld_fail(ctx, result, "out of memory reading dylib exports from '%s'", file->path);
        }
    }
    if (export_trie && export_trie_size) {
        uint8_t *visited = calloc(export_trie_size, 1);
        if (!visited) return ld_fail(ctx, LD_IO_ERROR, "out of memory reading export trie '%s'", file->path);
        char name[4096] = {0};
        result = ld_parse_export_node(ctx, file->path, dylib, export_trie, export_trie_size, 0, name, 0, visited, 0);
        free(visited);
        if (result != LD_OK) return result;
    }
    return LD_OK;
}

static int ld_parse_blob(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size,
                          const char *member_name, bool archive_member);

static int ld_parse_archive(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size) {
    static const uint8_t magic[] = "!<arch>\n";
    if (size < sizeof(magic) - 1U || memcmp(bytes, magic, sizeof(magic) - 1U) != 0) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid archive '%s'", file->path);
    }
    size_t offset = sizeof(magic) - 1U;
    const uint8_t *long_names = NULL;
    size_t long_names_size = 0;
    while (offset < size) {
        if (size - offset < 60U) {
            return ld_fail(ctx, LD_INVALID_INPUT, "truncated archive header in '%s'", file->path);
        }
        const uint8_t *header = bytes + offset;
        if (memcmp(header + 58, "`\n", 2) != 0) {
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid archive header in '%s'", file->path);
        }
        uint64_t member_size;
        if (!ld_archive_decimal(header + 48, 10, &member_size)) {
            return ld_fail(ctx, LD_INVALID_INPUT, "invalid archive member size in '%s'", file->path);
        }
        if (member_size > size - offset - 60U) {
            return ld_fail(ctx, LD_INVALID_INPUT, "archive member outside '%s'", file->path);
        }
        size_t payload = offset + 60U;
        size_t payload_size = (size_t) member_size;
        char raw_name[17] = {0};
        memcpy(raw_name, header, 16);
        size_t name_len = 16;
        while (name_len && raw_name[name_len - 1] == ' ') name_len--;
        if (name_len > 1U && raw_name[name_len - 1U] == '/' &&
            !(name_len == 2U && raw_name[0] == '/' && raw_name[1] == '/')) name_len--;
        char *name = NULL;
        if (strncmp(raw_name, "#1/", 3) == 0) {
            uint64_t extended;
            if (name_len <= 3U || !ld_archive_decimal((const uint8_t *) raw_name + 3, name_len - 3U, &extended)) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid extended archive name in '%s'", file->path);
            }
            if (extended > payload_size) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid extended archive name in '%s'", file->path);
            }
            size_t actual = 0;
            while (actual < (size_t) extended && bytes[payload + actual] != '\0') actual++;
            name = ld_strndup0((const char *) bytes + payload, actual);
            payload += (size_t) extended;
            payload_size -= (size_t) extended;
        } else if (raw_name[0] == '/' && isdigit((unsigned char) raw_name[1])) {
            uint64_t name_offset;
            if (name_len <= 1U ||
                !ld_archive_decimal((const uint8_t *) raw_name + 1, name_len - 1U, &name_offset) ||
                !long_names || name_offset >= long_names_size) {
                return ld_fail(ctx, LD_INVALID_INPUT, "invalid archive long-name reference in '%s'", file->path);
            }
            const char *long_name = (const char *) long_names + name_offset;
            size_t available = long_names_size - (size_t) name_offset;
            const char *terminator = memchr(long_name, '/', available);
            if (!terminator) terminator = memchr(long_name, '\n', available);
            if (!terminator) terminator = (const char *) long_names + long_names_size;
            name = ld_strndup0(long_name, (size_t) (terminator - long_name));
        } else {
            name = ld_strndup0(raw_name, name_len);
        }
        if (!name) {
            return ld_fail(ctx, LD_IO_ERROR, "out of memory reading archive '%s'", file->path);
        }
        if (strcmp(name, "//") == 0) {
            long_names = bytes + payload;
            long_names_size = payload_size;
        }
        bool symbol_table = strncmp(name, "__.SYMDEF", 9) == 0 || strcmp(name, "/") == 0 || strcmp(name, "//") == 0;
        if (!symbol_table && payload_size) {
            int result = ld_parse_blob(ctx, file, bytes + payload, payload_size, name, true);
            free(name);
            if (result != LD_OK) {
                return result;
            }
        } else {
            free(name);
        }
        offset += 60U + (size_t) member_size;
        if (offset & 1U) {
            if (offset >= size || bytes[offset] != '\n') {
                return ld_fail(ctx, LD_INVALID_INPUT,
                                "archive member is missing its alignment padding in '%s'",
                                file->path);
            }
            offset++;
        }
    }
    return LD_OK;
}

static int ld_parse_fat(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size,
                         const char *member_name, bool archive_member) {
    if (size < 8) {
        return ld_fail(ctx, LD_INVALID_INPUT, "truncated fat binary '%s'", file->path);
    }
    uint32_t magic = ld_read_be32(bytes);
    uint32_t count = ld_read_be32(bytes + 4);
    bool fat64 = magic == LD_FAT_MAGIC_64;
    size_t entry_size = fat64 ? sizeof(ld_fat_arch_64_t) : sizeof(ld_fat_arch_t);
    if (count > 128 || (uint64_t) count * entry_size > size - 8U) {
        return ld_fail(ctx, LD_INVALID_INPUT, "invalid fat header '%s'", file->path);
    }
    uint64_t selected_offset = 0, selected_size = 0;
    int selected_subtype_rank = INT_MAX;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = bytes + 8U + (size_t) i * entry_size;
        int32_t cputype = (int32_t) ld_read_be32(entry);
        int32_t cpusubtype = (int32_t) ld_read_be32(entry + 4);
        uint64_t offset = fat64 ? ld_read_be64(entry + 8) : ld_read_be32(entry + 8);
        uint64_t slice_size = fat64 ? ld_read_be64(entry + 16) : ld_read_be32(entry + 12);
        if (cputype == (int32_t) LD_CPU_TYPE_ARM64) {
            if (!ld_range_ok(size, offset, slice_size)) {
                return ld_fail(ctx, LD_INVALID_INPUT, "fat arm64 slice outside '%s'", file->path);
            }
            int subtype_rank = cpusubtype == LD_CPU_SUBTYPE_ARM64_ALL ? 0 : cpusubtype == LD_CPU_SUBTYPE_ARM64E ? 1
                                                                                                                  : 2;
            if (subtype_rank < selected_subtype_rank) {
                selected_subtype_rank = subtype_rank;
                selected_offset = offset;
                selected_size = slice_size;
            }
        }
    }
    if (selected_subtype_rank != INT_MAX) {
        return ld_parse_blob(ctx, file, bytes + selected_offset, (size_t) selected_size, member_name, archive_member);
    }
    return ld_fail(ctx, LD_UNSUPPORTED, "fat binary '%s' has no arm64 slice", file->path);
}

static int ld_parse_blob(ld_context_t *ctx, ld_file_t *file, const uint8_t *bytes, size_t size,
                          const char *member_name, bool archive_member) {
    if (size >= 4 && ld_read_be32(bytes) == LD_FAT_MAGIC) {
        return ld_parse_fat(ctx, file, bytes, size, member_name, archive_member);
    }
    if (size >= 4 && ld_read_be32(bytes) == LD_FAT_MAGIC_64) {
        return ld_parse_fat(ctx, file, bytes, size, member_name, archive_member);
    }
    if (size >= 8 && memcmp(bytes, "!<arch>\n", 8) == 0) {
        return ld_parse_archive(ctx, file, bytes, size);
    }
    if (size >= 4 && ld_read_u32(bytes) == LD_MH_MAGIC_64) {
        if (size >= sizeof(ld_mach_header_64_t)) {
            ld_mach_header_64_t header;
            memcpy(&header, bytes, sizeof(header));
            if (header.filetype == LD_MH_DYLIB) {
                return ld_parse_dylib(ctx, file, bytes, size);
            }
        }
        ld_object_t *object = NULL;
        return ld_parse_object(ctx, file, bytes, size, member_name, archive_member, &object);
    }
    if (size >= 12U && bytes[0] == '-' && bytes[1] == '-' && bytes[2] == '-') {
        return ld_parse_tbd(ctx, file, bytes, size);
    }
    return ld_fail(ctx, LD_UNSUPPORTED, "unsupported input '%s'%s%s", file->path,
                    member_name ? " member " : "", member_name ? member_name : "");
}

int ld_parse_input_file(ld_context_t *ctx, const char *path) {
    ld_file_t *file = NULL;
    int result = ld_read_file(ctx, path, &file);
    if (result != LD_OK) {
        return result;
    }
    return ld_parse_blob(ctx, file, file->bytes, file->size, NULL, false);
}

static int ld_try_library_candidate(ld_context_t *ctx, const char *directory, const char *name,
                                     const char *extension, bool *found) {
    char path[PATH_MAX];
    int length = snprintf(path, sizeof(path), "%s/lib%s%s", directory, name, extension);
    if (length < 0 || (size_t) length >= sizeof(path)) {
        return ld_fail(ctx, LD_INVALID_ARGUMENT, "library search path for '-l%s' is too long", name);
    }
    if (access(path, R_OK) != 0) {
        return LD_OK;
    }
    *found = true;
    return ld_parse_input_file(ctx, path);
}

static int ld_try_library_directory(ld_context_t *ctx, const char *directory, const char *name, bool *found) {
    static const char *const extensions[] = {".a", ".tbd", ".dylib"};
    if (!directory || !*directory || *found) return LD_OK;
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        int result = ld_try_library_candidate(ctx, directory, name, extensions[i], found);
        if (result != LD_OK || *found) return result;
    }
    return LD_OK;
}

static int ld_try_framework_directory(ld_context_t *ctx, const char *directory, const char *name, bool *found) {
    static const char *const suffixes[] = {
            "%s/%s.framework/%s.tbd",
            "%s/%s.framework/%s",
            "%s/%s.framework/Versions/A/%s.tbd",
            "%s/%s.framework/Versions/A/%s",
            "%s/%s.framework/Versions/C/%s.tbd",
            "%s/%s.framework/Versions/C/%s",
    };
    if (!directory || !*directory || *found) return LD_OK;
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char path[PATH_MAX];
        int length = snprintf(path, sizeof(path), suffixes[i], directory, name, name);
        if (length < 0 || (size_t) length >= sizeof(path)) {
            return ld_fail(ctx, LD_INVALID_ARGUMENT, "framework search path for '%s' is too long", name);
        }
        if (access(path, R_OK) != 0) continue;
        *found = true;
        return ld_parse_input_file(ctx, path);
    }
    return LD_OK;
}

static bool ld_reexport_try_candidate(const char *candidate, char path[PATH_MAX]) {
    size_t path_length = strlen(candidate);
    if (path_length >= PATH_MAX) return false;
    memcpy(path, candidate, path_length + 1U);
    if (access(path, R_OK) == 0) return true;

    static const char dylib_suffix[] = ".dylib";
    if (path_length >= sizeof(dylib_suffix) - 1U &&
        strcmp(path + path_length - (sizeof(dylib_suffix) - 1U), dylib_suffix) == 0) {
        path_length -= sizeof(dylib_suffix) - 1U;
        if (path_length + sizeof(".tbd") > PATH_MAX) return false;
        memcpy(path + path_length, ".tbd", sizeof(".tbd"));
    } else {
        if (path_length + sizeof(".tbd") > PATH_MAX) return false;
        memcpy(path + path_length, ".tbd", sizeof(".tbd"));
    }
    return access(path, R_OK) == 0;
}

static bool ld_reexport_join(const char *prefix, size_t prefix_length, const char *suffix,
                              char path[PATH_MAX]) {
    size_t suffix_length = strlen(suffix);
    if (prefix_length > PATH_MAX - 1U || suffix_length > PATH_MAX - 1U - prefix_length) {
        return false;
    }
    char candidate[PATH_MAX];
    memcpy(candidate, prefix, prefix_length);
    memcpy(candidate + prefix_length, suffix, suffix_length + 1U);
    return ld_reexport_try_candidate(candidate, path);
}

static bool ld_reexport_search_directory(const char *directory, const char *root_marker,
                                          const char *install_name, char path[PATH_MAX]) {
    if (!directory || !*directory) return false;
    size_t directory_length = strlen(directory);
    while (directory_length > 1U && directory[directory_length - 1U] == '/') directory_length--;
    size_t marker_length = strlen(root_marker);

    /* SDK-style -F/-L paths identify the sysroot even when the caller cannot
       set one directly, which is the normal Linux-to-Darwin cross-link case. */
    if (directory_length >= marker_length &&
        memcmp(directory + directory_length - marker_length, root_marker, marker_length) == 0) {
        size_t root_length = directory_length - marker_length;
        if (ld_reexport_join(directory, root_length, install_name, path)) return true;
    }

    if (strncmp(install_name, root_marker, marker_length) != 0 ||
        (install_name[marker_length] != '/' && install_name[marker_length] != '\0')) {
        return false;
    }
    return ld_reexport_join(directory, directory_length, install_name + marker_length, path);
}

static bool ld_reexport_file_path(const ld_context_t *ctx, const char *install_name,
                                   char path[PATH_MAX]) {
    const char *sysroot = ctx->options->sysroot && *ctx->options->sysroot ? ctx->options->sysroot : "";
    if (*sysroot) {
        if (ld_reexport_join(sysroot, strlen(sysroot), install_name, path)) return true;
    } else if (ld_reexport_try_candidate(install_name, path)) {
        return true;
    }

    static const char framework_root[] = "/System/Library/Frameworks";
    for (size_t i = 0; i < ctx->options->framework_paths.count; i++) {
        if (ld_reexport_search_directory(ctx->options->framework_paths.items[i], framework_root,
                                          install_name, path)) {
            return true;
        }
    }
    static const char library_root[] = "/usr/lib";
    for (size_t i = 0; i < ctx->options->library_paths.count; i++) {
        if (ld_reexport_search_directory(ctx->options->library_paths.items[i], library_root,
                                          install_name, path)) {
            return true;
        }
    }
    return false;
}

static bool ld_dylib_exports_unresolved(const ld_context_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        const ld_dylib_input_t *dylib = &ctx->dylibs.items[i];
        for (size_t j = 0; j < dylib->export_count; j++) {
            if (strcmp(dylib->exports[j], name) == 0) return true;
        }
        for (size_t j = 0; j < dylib->weak_export_count; j++) {
            if (strcmp(dylib->weak_exports[j], name) == 0) return true;
        }
    }
    return false;
}

static bool ld_has_unresolved_reexport_symbol(const ld_context_t *ctx) {
    for (const ld_symbol_t *symbol = ctx->symbols; symbol; symbol = symbol->hh.next) {
        if (symbol->kind == LD_SYMBOL_UNDEFINED && symbol->name &&
            !ld_dylib_exports_unresolved(ctx, symbol->name)) {
            return true;
        }
    }
    return false;
}

int ld_resolve_reexport_libraries(ld_context_t *ctx) {
    for (size_t dylib_index = 0; dylib_index < ctx->dylibs.count; dylib_index++) {
        if (!ld_has_unresolved_reexport_symbol(ctx)) return LD_OK;
        if (ctx->dylibs.items[dylib_index].reexports_scanned) continue;
        ctx->dylibs.items[dylib_index].reexports_scanned = true;
        size_t reexport_count = ctx->dylibs.items[dylib_index].reexport_count;
        for (size_t reexport_index = 0; reexport_index < reexport_count; reexport_index++) {
            char *install_name = strdup(ctx->dylibs.items[dylib_index].reexports[reexport_index]);
            if (!install_name) return ld_fail(ctx, LD_IO_ERROR, "out of memory resolving dylib reexports");
            char path[PATH_MAX];
            if (!ld_reexport_file_path(ctx, install_name, path)) {
                free(install_name);
                continue;
            }
            size_t previous_count = ctx->dylibs.count;
            int result = ld_parse_input_file(ctx, path);
            if (result != LD_OK) {
                free(install_name);
                return result;
            }
            ld_dylib_input_t *child = NULL;
            size_t child_index = SIZE_MAX;
            for (size_t i = 0; i < ctx->dylibs.count; i++) {
                if (strcmp(ctx->dylibs.items[i].install_name, install_name) == 0 ||
                    strcmp(ctx->dylibs.items[i].path, path) == 0) {
                    child = &ctx->dylibs.items[i];
                    child_index = i;
                    if (i >= previous_count) {
                        const ld_dylib_input_t *parent = &ctx->dylibs.items[dylib_index];
                        child->reexport_only = true;
                        child->has_reexport_owner = true;
                        child->reexport_owner = parent->has_reexport_owner
                                                        ? parent->reexport_owner
                                                        : dylib_index;
                    }
                    break;
                }
            }
            free(install_name);
            if (!child || child_index == dylib_index) continue;
            ld_dylib_input_t *parent = &ctx->dylibs.items[dylib_index];
            for (size_t i = 0; i < child->export_count; i++) {
                result = ld_dylib_export_push(parent, child->exports[i], strlen(child->exports[i]));
                if (result != LD_OK) return ld_fail(ctx, result, "cannot merge dylib reexports");
            }
            for (size_t i = 0; i < child->weak_export_count; i++) {
                result = ld_dylib_weak_export_push(parent, child->weak_exports[i], strlen(child->weak_exports[i]));
                if (result != LD_OK) return ld_fail(ctx, result, "cannot merge weak dylib reexports");
            }
        }
    }
    return LD_OK;
}

int ld_resolve_requested_libraries(ld_context_t *ctx) {
    /* libSystem is implicit for every Darwin executable.  Recording its TBD
       when an SDK is available gives symbol resolution and two-level ordinals
       the same view as ld64 while keeping the load command synthesized later. */
    bool system_recorded = false;
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        if (strcmp(ctx->dylibs.items[i].install_name, "/usr/lib/libSystem.B.dylib") == 0) {
            system_recorded = true;
            break;
        }
    }
    char implicit_system[PATH_MAX];
    const char *system_root = ctx->options->sysroot && *ctx->options->sysroot ? ctx->options->sysroot : "";
    int system_length = snprintf(implicit_system, sizeof(implicit_system), "%s%s/usr/lib/libSystem.tbd",
                                 system_root, *system_root ? "/" : "");
    if (!system_recorded && system_length >= 0 && (size_t) system_length < sizeof(implicit_system) &&
        access(implicit_system, R_OK) == 0) {
        int result = ld_parse_input_file(ctx, implicit_system);
        if (result != LD_OK) return result;
        system_recorded = true;
    }
    for (size_t i = 0; !system_recorded && i < ctx->options->library_paths.count; i++) {
        bool found = false;
        int result = ld_try_library_directory(ctx, ctx->options->library_paths.items[i], "System", &found);
        if (result != LD_OK) return result;
        system_recorded = found;
    }
    for (size_t library_index = 0; library_index < ctx->options->libraries.count; library_index++) {
        const char *name = ctx->options->libraries.items[library_index];
        if (strcmp(name, "System") == 0) continue;
        bool found = false;
        int result = LD_OK;
        for (size_t i = 0; i < ctx->options->library_paths.count && !found; i++) {
            result = ld_try_library_directory(ctx, ctx->options->library_paths.items[i], name, &found);
            if (result != LD_OK) return result;
        }
        if (!found && ctx->options->sysroot && *ctx->options->sysroot) {
            char directory[PATH_MAX];
            int length = snprintf(directory, sizeof(directory), "%s/usr/lib", ctx->options->sysroot);
            if (length < 0 || (size_t) length >= sizeof(directory)) {
                return ld_fail(ctx, LD_INVALID_ARGUMENT, "sysroot path is too long");
            }
            result = ld_try_library_directory(ctx, directory, name, &found);
            if (result != LD_OK) return result;
        }
        if (!found && (!ctx->options->sysroot || !*ctx->options->sysroot)) {
            result = ld_try_library_directory(ctx, "/usr/lib", name, &found);
            if (result != LD_OK) return result;
        }
        if (!found) {
            return ld_fail(ctx, LD_IO_ERROR, "library '-l%s' was not found in the configured search paths", name);
        }
    }

    for (size_t framework_index = 0; framework_index < ctx->options->frameworks.count; framework_index++) {
        const char *name = ctx->options->frameworks.items[framework_index];
        bool found = false;
        int result = LD_OK;
        for (size_t i = 0; i < ctx->options->framework_paths.count && !found; i++) {
            result = ld_try_framework_directory(ctx, ctx->options->framework_paths.items[i], name, &found);
            if (result != LD_OK) return result;
        }
        if (!found && ctx->options->sysroot && *ctx->options->sysroot) {
            char directory[PATH_MAX];
            int length = snprintf(directory, sizeof(directory), "%s/System/Library/Frameworks", ctx->options->sysroot);
            if (length < 0 || (size_t) length >= sizeof(directory)) {
                return ld_fail(ctx, LD_INVALID_ARGUMENT, "sysroot path is too long");
            }
            result = ld_try_framework_directory(ctx, directory, name, &found);
            if (result != LD_OK) return result;
        }
        if (!found && (!ctx->options->sysroot || !*ctx->options->sysroot)) {
            result = ld_try_framework_directory(ctx, "/System/Library/Frameworks", name, &found);
            if (result != LD_OK) return result;
        }
        if (!found) {
            return ld_fail(ctx, LD_IO_ERROR, "framework '%s' was not found in the configured search paths", name);
        }
    }
    return LD_OK;
}

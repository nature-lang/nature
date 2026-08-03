#include "ld_elf_archive.h"
#include "ld_elf_debug.h"
#include "ld_elf_internal.h"
#include "ld_elf_rel.h"
#include "ld_elf_script.h"
#include "ld_elf_thunk.h"
#include "ld_elf_zlib.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*
 * The checked-reader and lazy archive flow follow the structure of Zig's
 * Object.parseCommon, Object.initAtoms section-group parsing, and Archive.parse
 * at commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. The code here is an independent
 * C implementation and uses only the portable wire definitions in
 * elf_format.h. See ZIG-LICENSE.txt for Zig's MIT license.
 */

typedef struct {
    const uint8_t *bytes;
    size_t size;
} ld_elf_view_t;

static char *ld_elf_display_name(const char *path, const char *member_name);

static uint16_t ld_elf_read_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] |
                       (uint16_t) ((uint16_t) bytes[1] << 8U));
}

static uint32_t ld_elf_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) | ((uint32_t) bytes[3] << 24U);
}

static uint64_t ld_elf_read_u64(const uint8_t *bytes) {
    return (uint64_t) ld_elf_read_u32(bytes) |
           ((uint64_t) ld_elf_read_u32(bytes + 4U) << 32U);
}

static bool ld_elf_range_ok(size_t size, uint64_t offset, uint64_t length) {
    if (offset > SIZE_MAX || length > SIZE_MAX) return false;
    size_t off = (size_t) offset;
    size_t len = (size_t) length;
    return off <= size && len <= size - off;
}

static bool ld_elf_multiply_size(size_t left, size_t right, size_t *result) {
    if (left != 0U && right > SIZE_MAX / left) return false;
    *result = left * right;
    return true;
}

static bool ld_elf_is_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool ld_elf_skip_nonalloc_section(
        const ld_elf_context_t *ctx, const ld_elf_section_t *section) {
    if (!section ||
        (section->header.sh_flags & LD_ELF_SHF_ALLOC) != 0U) {
        return false;
    }
    bool keep_dwarf = ctx && ctx->options &&
                      ctx->options->debug_mode == LD_DEBUG_DWARF;
    return ld_elf_debug_classify_nonalloc_section(
                   section->name, section->header.sh_type,
                   section->header.sh_flags, keep_dwarf) ==
           LD_ELF_DEBUG_SECTION_SKIP;
}

static char *ld_elf_strndup(const char *value, size_t length) {
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

static void *ld_elf_grow_array(void *old, size_t old_capacity, size_t new_capacity,
                               size_t element_size) {
    size_t byte_count;
    if (new_capacity < old_capacity ||
        !ld_elf_multiply_size(new_capacity, element_size, &byte_count)) {
        return NULL;
    }
    void *result = realloc(old, byte_count);
    if (result && new_capacity > old_capacity) {
        size_t old_size = old_capacity * element_size;
        memset((uint8_t *) result + old_size, 0, byte_count - old_size);
    }
    return result;
}

static int ld_elf_file_push(ld_elf_context_t *ctx, ld_elf_file_t *file) {
    if (ctx->files.count == ctx->files.capacity) {
        if (ctx->files.capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = ctx->files.capacity ? ctx->files.capacity * 2U : 8U;
        ld_elf_file_t **items = ld_elf_grow_array(
                ctx->files.items, ctx->files.capacity, next, sizeof(*items));
        if (!items) return LD_IO_ERROR;
        ctx->files.items = items;
        ctx->files.capacity = next;
    }
    ctx->files.items[ctx->files.count++] = file;
    return LD_OK;
}

static int ld_elf_archive_push(ld_elf_context_t *ctx, ld_elf_archive_t *archive) {
    if (ctx->archives.count == ctx->archives.capacity) {
        if (ctx->archives.capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = ctx->archives.capacity ? ctx->archives.capacity * 2U : 4U;
        ld_elf_archive_t **items = ld_elf_grow_array(
                ctx->archives.items, ctx->archives.capacity, next, sizeof(*items));
        if (!items) return LD_IO_ERROR;
        ctx->archives.items = items;
        ctx->archives.capacity = next;
    }
    ctx->archives.items[ctx->archives.count++] = archive;
    return LD_OK;
}

static int ld_elf_object_push(ld_elf_context_t *ctx, ld_elf_object_t *object) {
    if (ctx->objects.count == ctx->objects.capacity) {
        if (ctx->objects.capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = ctx->objects.capacity ? ctx->objects.capacity * 2U : 32U;
        ld_elf_object_t **items = ld_elf_grow_array(
                ctx->objects.items, ctx->objects.capacity, next, sizeof(*items));
        if (!items) return LD_IO_ERROR;
        ctx->objects.items = items;
        ctx->objects.capacity = next;
    }
    ctx->objects.items[ctx->objects.count++] = object;
    return LD_OK;
}

static int ld_elf_archive_member_push(ld_elf_archive_t *archive,
                                      ld_elf_object_t *object) {
    if (archive->member_count == archive->member_capacity) {
        if (archive->member_capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = archive->member_capacity ? archive->member_capacity * 2U : 16U;
        ld_elf_object_t **items = ld_elf_grow_array(
                archive->members, archive->member_capacity, next, sizeof(*items));
        if (!items) return LD_IO_ERROR;
        archive->members = items;
        archive->member_capacity = next;
    }
    object->archive_member_index = (uint32_t) archive->member_count;
    archive->members[archive->member_count++] = object;
    return LD_OK;
}

int ld_elf_fail(ld_elf_context_t *ctx, int code, const char *format, ...) {
    if (!ctx) return code;
    ctx->error = code;
    if (ctx->options && ctx->options->diagnostic) {
        char message[4096];
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);
        ctx->options->diagnostic(ctx->options->diagnostic_context,
                                 LD_DIAG_ERROR, message);
    }
    return code;
}

static void ld_elf_object_destroy(ld_elf_object_t *object) {
    if (!object) return;
    for (size_t i = 0; i < object->section_count; i++) {
        free(object->sections[i].owned_data);
        free(object->sections[i].relocations);
        free(object->sections[i].eh_records);
        ld_elf_riscv_relax_plan_deinit(
                &object->sections[i].riscv_relax_plan);
    }
    for (size_t i = 0; i < object->group_count; i++) {
        free(object->groups[i].members);
    }
    free(object->groups);
    free(object->sections);
    free(object->symbols);
    free(object->member_name);
    free(object->display_name);
    free(object);
}

void ld_elf_context_init(ld_elf_context_t *ctx, const ld_options_t *options) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->options = options;
}

void ld_elf_context_deinit(ld_elf_context_t *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->objects.count; i++) {
        ld_elf_object_destroy(ctx->objects.items[i]);
    }
    for (size_t i = 0; i < ctx->archives.count; i++) {
        free(ctx->archives.items[i]->members);
        free(ctx->archives.items[i]);
    }
    for (size_t i = 0; i < ctx->files.count; i++) {
        free(ctx->files.items[i]->bytes);
        free(ctx->files.items[i]->path);
        free(ctx->files.items[i]);
    }
    free(ctx->objects.items);
    free(ctx->archives.items);
    free(ctx->files.items);
    memset(ctx, 0, sizeof(*ctx));
}

static int ld_elf_read_file(ld_elf_context_t *ctx, const char *path,
                            const char *diagnostic_name,
                            ld_elf_file_t **result) {
    *result = NULL;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) {
        return ld_elf_fail(ctx, LD_IO_ERROR, "cannot open ELF input '%s': %s",
                           diagnostic_name, strerror(errno));
    }

    struct stat status;
    if (fstat(fd, &status) != 0 || status.st_size < 0) {
        int saved = errno;
        close(fd);
        return ld_elf_fail(ctx, LD_IO_ERROR, "cannot stat ELF input '%s': %s",
                           diagnostic_name, strerror(saved));
    }
    if ((uintmax_t) status.st_size > SIZE_MAX) {
        close(fd);
        return ld_elf_fail(ctx, LD_IO_ERROR, "ELF input '%s' is too large",
                           diagnostic_name);
    }

    size_t size = (size_t) status.st_size;
    uint8_t *bytes = size ? malloc(size) : NULL;
    if (size && !bytes) {
        close(fd);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading ELF input '%s'",
                           diagnostic_name);
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(fd, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int saved = errno;
            free(bytes);
            close(fd);
            return ld_elf_fail(ctx, LD_IO_ERROR, "cannot read ELF input '%s': %s",
                               diagnostic_name,
                               count == 0 ? "unexpected end of file"
                                          : strerror(saved));
        }
        offset += (size_t) count;
    }
    close(fd);

    ld_elf_file_t *file = calloc(1, sizeof(*file));
    if (!file) {
        free(bytes);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording ELF input '%s'",
                           diagnostic_name);
    }
    file->bytes = bytes;
    file->size = size;
    file->path = ld_elf_strndup(path, strlen(path));
    if (!file->path || ld_elf_file_push(ctx, file) != LD_OK) {
        free(file->path);
        free(file->bytes);
        free(file);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording ELF input '%s'",
                           diagnostic_name);
    }
    *result = file;
    return LD_OK;
}

static const uint8_t *ld_elf_view_range(ld_elf_view_t view, uint64_t offset,
                                        uint64_t length) {
    return ld_elf_range_ok(view.size, offset, length)
                   ? view.bytes + (size_t) offset
                   : NULL;
}

static void ld_elf_decode_header(const uint8_t *bytes, ld_elf64_ehdr_t *header) {
    memcpy(header->e_ident, bytes, LD_ELF_IDENT_SIZE);
    header->e_type = ld_elf_read_u16(bytes + 16U);
    header->e_machine = ld_elf_read_u16(bytes + 18U);
    header->e_version = ld_elf_read_u32(bytes + 20U);
    header->e_entry = ld_elf_read_u64(bytes + 24U);
    header->e_phoff = ld_elf_read_u64(bytes + 32U);
    header->e_shoff = ld_elf_read_u64(bytes + 40U);
    header->e_flags = ld_elf_read_u32(bytes + 48U);
    header->e_ehsize = ld_elf_read_u16(bytes + 52U);
    header->e_phentsize = ld_elf_read_u16(bytes + 54U);
    header->e_phnum = ld_elf_read_u16(bytes + 56U);
    header->e_shentsize = ld_elf_read_u16(bytes + 58U);
    header->e_shnum = ld_elf_read_u16(bytes + 60U);
    header->e_shstrndx = ld_elf_read_u16(bytes + 62U);
}

static void ld_elf_decode_section(const uint8_t *bytes,
                                  ld_elf64_shdr_t *section) {
    section->sh_name = ld_elf_read_u32(bytes);
    section->sh_type = ld_elf_read_u32(bytes + 4U);
    section->sh_flags = ld_elf_read_u64(bytes + 8U);
    section->sh_addr = ld_elf_read_u64(bytes + 16U);
    section->sh_offset = ld_elf_read_u64(bytes + 24U);
    section->sh_size = ld_elf_read_u64(bytes + 32U);
    section->sh_link = ld_elf_read_u32(bytes + 40U);
    section->sh_info = ld_elf_read_u32(bytes + 44U);
    section->sh_addralign = ld_elf_read_u64(bytes + 48U);
    section->sh_entsize = ld_elf_read_u64(bytes + 56U);
}

static void ld_elf_decode_symbol(const uint8_t *bytes, ld_elf64_sym_t *symbol) {
    symbol->st_name = ld_elf_read_u32(bytes);
    symbol->st_info = bytes[4];
    symbol->st_other = bytes[5];
    symbol->st_shndx = ld_elf_read_u16(bytes + 6U);
    symbol->st_value = ld_elf_read_u64(bytes + 8U);
    symbol->st_size = ld_elf_read_u64(bytes + 16U);
}

static void ld_elf_decode_relocation(const uint8_t *bytes,
                                     ld_elf64_rela_t *relocation) {
    relocation->r_offset = ld_elf_read_u64(bytes);
    relocation->r_info = ld_elf_read_u64(bytes + 8U);
    relocation->r_addend = (int64_t) ld_elf_read_u64(bytes + 16U);
}

static void ld_elf_decode_rel(const uint8_t *bytes,
                              ld_elf64_rela_t *relocation) {
    relocation->r_offset = ld_elf_read_u64(bytes);
    relocation->r_info = ld_elf_read_u64(bytes + 8U);
    relocation->r_addend = 0;
}

static bool ld_elf_string_at(const char *table, size_t table_size, uint32_t offset,
                             const char **result) {
    if (!table || offset >= table_size) return false;
    const char *value = table + offset;
    if (!memchr(value, '\0', table_size - offset)) return false;
    *result = value;
    return true;
}

static uint16_t ld_elf_expected_machine(ld_arch_t arch) {
    switch (arch) {
        case LD_ARCH_ARM64:
            return LD_ELF_EM_AARCH64;
        case LD_ARCH_AMD64:
            return LD_ELF_EM_X86_64;
        case LD_ARCH_RISCV64:
            return LD_ELF_EM_RISCV;
        default:
            return LD_ELF_ET_NONE;
    }
}

static const char *ld_elf_machine_name(uint16_t machine) {
    switch (machine) {
        case LD_ELF_EM_AARCH64:
            return "AArch64";
        case LD_ELF_EM_X86_64:
            return "x86-64";
        case LD_ELF_EM_RISCV:
            return "RISC-V 64";
        default:
            return "unknown";
    }
}

static char *ld_elf_display_name(const char *path, const char *member_name) {
    if (!member_name) return ld_elf_strndup(path, strlen(path));
    size_t path_length = strlen(path);
    size_t member_length = strlen(member_name);
    if (path_length > SIZE_MAX - member_length - 3U) return NULL;
    size_t length = path_length + member_length + 2U;
    char *name = malloc(length + 1U);
    if (!name) return NULL;
    memcpy(name, path, path_length);
    name[path_length] = '(';
    memcpy(name + path_length + 1U, member_name, member_length);
    name[length - 1U] = ')';
    name[length] = '\0';
    return name;
}

static int ld_elf_relocation_push(ld_elf_context_t *ctx,
                                  ld_elf_object_t *object,
                                  ld_elf_section_t *section,
                                  const ld_elf_relocation_t *relocation) {
    if (section->relocation_count == section->relocation_capacity) {
        if (section->relocation_capacity > SIZE_MAX / 2U) {
            return ld_elf_fail(ctx, LD_IO_ERROR,
                               "too many relocations for section '%s' in '%s'",
                               section->name, object->display_name);
        }
        size_t next = section->relocation_capacity
                              ? section->relocation_capacity * 2U
                              : 16U;
        ld_elf_relocation_t *items = ld_elf_grow_array(
                section->relocations, section->relocation_capacity, next,
                sizeof(*items));
        if (!items) {
            return ld_elf_fail(ctx, LD_IO_ERROR,
                               "out of memory reading relocations for section "
                               "'%s' in '%s'",
                               section->name, object->display_name);
        }
        section->relocations = items;
        section->relocation_capacity = next;
    }
    section->relocations[section->relocation_count++] = *relocation;
    return LD_OK;
}

static int ld_elf_eh_record_push(ld_elf_context_t *ctx,
                                 ld_elf_object_t *object,
                                 ld_elf_section_t *section,
                                 const ld_elf_eh_record_t *record) {
    if (section->eh_record_count == section->eh_record_capacity) {
        if (section->eh_record_capacity > SIZE_MAX / 2U) {
            return ld_elf_fail(ctx, LD_IO_ERROR,
                               "too many .eh_frame records in '%s'",
                               object->display_name);
        }
        size_t next = section->eh_record_capacity
                              ? section->eh_record_capacity * 2U
                              : 16U;
        ld_elf_eh_record_t *items = ld_elf_grow_array(
                section->eh_records, section->eh_record_capacity, next,
                sizeof(*items));
        if (!items) {
            return ld_elf_fail(ctx, LD_IO_ERROR,
                               "out of memory reading .eh_frame records from '%s'",
                               object->display_name);
        }
        section->eh_records = items;
        section->eh_record_capacity = next;
    }
    section->eh_records[section->eh_record_count++] = *record;
    return LD_OK;
}

static uint32_t ld_elf_link_order_next(const ld_elf_object_t *object,
                                       uint32_t section_index) {
    if (section_index == LD_ELF_SHN_UNDEF ||
        section_index >= object->section_count) {
        return LD_ELF_SHN_UNDEF;
    }
    const ld_elf_section_t *section = &object->sections[section_index];
    if ((section->header.sh_flags & LD_ELF_SHF_LINK_ORDER) == 0U)
        return LD_ELF_SHN_UNDEF;
    return section->header.sh_link;
}

static int ld_elf_validate_link_order(ld_elf_context_t *ctx,
                                      ld_elf_object_t *object) {
    for (size_t i = 1; i < object->section_count; i++) {
        const ld_elf_section_t *section = &object->sections[i];
        if ((section->header.sh_flags & LD_ELF_SHF_LINK_ORDER) == 0U)
            continue;
        if (section->header.sh_link >= object->section_count) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHF_LINK_ORDER section %zu '%s' has out-of-range "
                    "section link %u in '%s'",
                    i, section->name, section->header.sh_link,
                    object->display_name);
        }
        if (section->header.sh_link == i) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHF_LINK_ORDER section %zu '%s' links to itself "
                    "in '%s'",
                    i, section->name, object->display_name);
        }
    }

    uint8_t *states = calloc(object->section_count, sizeof(*states));
    if (!states) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory validating ELF SHF_LINK_ORDER "
                           "sections in '%s'",
                           object->display_name);
    }

    /* sh_link zero is the ABI-defined no-dependency value.  The three-state
       walk validates all dependency chains in linear time. */
    for (size_t i = 1; i < object->section_count; i++) {
        const ld_elf_section_t *section = &object->sections[i];
        if ((section->header.sh_flags & LD_ELF_SHF_LINK_ORDER) == 0U ||
            states[i] != 0U) {
            continue;
        }
        uint32_t cursor = (uint32_t) i;
        while (cursor != LD_ELF_SHN_UNDEF && states[cursor] == 0U) {
            states[cursor] = 1U;
            cursor = ld_elf_link_order_next(object, cursor);
        }
        if (cursor != LD_ELF_SHN_UNDEF && states[cursor] == 1U) {
            free(states);
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHF_LINK_ORDER dependency cycle involving section "
                    "%zu '%s' in '%s'",
                    i, section->name, object->display_name);
        }
        cursor = (uint32_t) i;
        while (cursor != LD_ELF_SHN_UNDEF && states[cursor] == 1U) {
            states[cursor] = 2U;
            cursor = ld_elf_link_order_next(object, cursor);
        }
    }
    free(states);
    return LD_OK;
}

static int ld_elf_decompress_debug_sections(ld_elf_context_t *ctx,
                                            ld_elf_object_t *object) {
    for (size_t i = 1; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if ((section->header.sh_flags & LD_ELF_SHF_COMPRESSED) == 0U)
            continue;
        if (ld_elf_skip_nonalloc_section(ctx, section)) {
            continue;
        }
        bool dwarf = strncmp(section->name, ".debug", 6U) == 0;
        if (!dwarf ||
            (section->header.sh_flags & LD_ELF_SHF_ALLOC) != 0U ||
            section->header.sh_type != LD_ELF_SHT_PROGBITS ||
            section->nobits) {
            return ld_elf_fail(
                    ctx, LD_UNSUPPORTED,
                    "unsupported compressed ELF section '%s' in '%s': "
                    "only non-allocated DWARF PROGBITS is supported",
                    section->name, object->display_name);
        }
        if (!section->data || section->data_size < LD_ELF64_CHDR_SIZE) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "truncated Elf64_Chdr in section '%s' in '%s'",
                    section->name, object->display_name);
        }
        uint32_t type = ld_elf_read_u32(section->data);
        uint32_t reserved = ld_elf_read_u32(section->data + 4U);
        uint64_t uncompressed_size = ld_elf_read_u64(section->data + 8U);
        uint64_t alignment = ld_elf_read_u64(section->data + 16U);
        if (reserved != 0U) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "compressed ELF section '%s' in '%s' has non-zero "
                    "ch_reserved",
                    section->name, object->display_name);
        }
        if (type != LD_ELF_COMPRESS_ZLIB) {
            return ld_elf_fail(
                    ctx, LD_UNSUPPORTED,
                    "compressed ELF section '%s' in '%s' uses unsupported "
                    "compression type %u",
                    section->name, object->display_name, type);
        }
        if (alignment == 0U || !ld_elf_is_power_of_two(alignment)) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "compressed ELF section '%s' in '%s' has invalid "
                    "ch_addralign %llu",
                    section->name, object->display_name,
                    (unsigned long long) alignment);
        }
        const uint8_t *payload = section->data + LD_ELF64_CHDR_SIZE;
        size_t payload_size = section->data_size - LD_ELF64_CHDR_SIZE;
        uint8_t *uncompressed = NULL;
        size_t result_size = 0U;
        ld_elf_zlib_result_t result = ld_elf_zlib_decompress(
                payload, payload_size, uncompressed_size, &uncompressed,
                &result_size);
        if (result != LD_ELF_ZLIB_OK) {
            int code = result == LD_ELF_ZLIB_ALLOCATION_FAILED
                               ? LD_IO_ERROR
                               : LD_INVALID_INPUT;
            return ld_elf_fail(
                    ctx, code,
                    "cannot decompress ELF section '%s' in '%s': %s",
                    section->name, object->display_name,
                    ld_elf_zlib_result_string(result));
        }
        section->owned_data = uncompressed;
        section->data = uncompressed;
        section->data_size = result_size;
        section->header.sh_size = result_size;
        section->header.sh_addralign = alignment;
        section->header.sh_flags &= ~LD_ELF_SHF_COMPRESSED;
    }
    return LD_OK;
}

static int ld_elf_parse_sections(ld_elf_context_t *ctx, ld_elf_object_t *object,
                                 ld_elf_view_t view) {
    ld_elf64_ehdr_t *header = &object->header;
    uint64_t section_count_u64 = header->e_shnum;
    uint32_t section_name_index = header->e_shstrndx;
    ld_elf64_shdr_t section_zero;
    memset(&section_zero, 0, sizeof(section_zero));

    if (header->e_shoff == 0U) {
        if (header->e_shnum != 0U || header->e_shstrndx != LD_ELF_SHN_UNDEF) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "corrupt ELF section table in '%s': zero offset "
                               "with non-zero section metadata",
                               object->display_name);
        }
        return LD_OK;
    }
    if (header->e_shentsize != LD_ELF64_SHDR_SIZE) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "unsupported ELF section-header size %u in '%s'",
                           header->e_shentsize, object->display_name);
    }
    const uint8_t *zero_bytes = ld_elf_view_range(
            view, header->e_shoff, LD_ELF64_SHDR_SIZE);
    if (!zero_bytes) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "truncated ELF section table in '%s'",
                           object->display_name);
    }
    ld_elf_decode_section(zero_bytes, &section_zero);
    if (section_zero.sh_type != LD_ELF_SHT_NULL) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF section zero is not SHT_NULL in '%s'",
                           object->display_name);
    }
    if (section_count_u64 == 0U) section_count_u64 = section_zero.sh_size;
    if (section_name_index == LD_ELF_SHN_XINDEX) {
        section_name_index = section_zero.sh_link;
    }
    if (section_count_u64 > UINT32_MAX || section_count_u64 > SIZE_MAX) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "too many ELF sections in '%s'", object->display_name);
    }
    size_t section_count = (size_t) section_count_u64;
    size_t table_size;
    if (!ld_elf_multiply_size(section_count, LD_ELF64_SHDR_SIZE, &table_size) ||
        !ld_elf_range_ok(view.size, header->e_shoff, table_size)) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF section table extends past the end of '%s'",
                           object->display_name);
    }
    if (section_count == 0U) {
        if (section_name_index != LD_ELF_SHN_UNDEF) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF section-name index without sections in '%s'",
                               object->display_name);
        }
        return LD_OK;
    }
    if (section_name_index != LD_ELF_SHN_UNDEF &&
        section_name_index >= section_count) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF section-name table index %u is out of range in '%s'",
                           section_name_index, object->display_name);
    }

    object->sections = calloc(section_count, sizeof(*object->sections));
    if (!object->sections) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading ELF sections from '%s'",
                           object->display_name);
    }
    object->section_count = section_count;

    for (size_t i = 0; i < section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        uint64_t wire_offset = header->e_shoff + i * LD_ELF64_SHDR_SIZE;
        const uint8_t *wire = ld_elf_view_range(view, wire_offset,
                                                LD_ELF64_SHDR_SIZE);
        ld_elf_decode_section(wire, &section->header);
        section->index = (uint32_t) i;
        section->group_index = LD_ELF_GROUP_NONE;
        section->nobits = section->header.sh_type == LD_ELF_SHT_NOBITS;
        if (section->header.sh_addralign != 0U &&
            !ld_elf_is_power_of_two(section->header.sh_addralign)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF section %zu in '%s' has invalid alignment %llu",
                               i, object->display_name,
                               (unsigned long long) section->header.sh_addralign);
        }
        if (section->header.sh_size > SIZE_MAX) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF section %zu in '%s' is too large", i,
                               object->display_name);
        }
        section->data_size = (size_t) section->header.sh_size;
        if (section->header.sh_type != LD_ELF_SHT_NOBITS &&
            section->header.sh_type != LD_ELF_SHT_NULL) {
            section->data = ld_elf_view_range(view, section->header.sh_offset,
                                              section->header.sh_size);
            if (!section->data && section->header.sh_size != 0U) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "ELF section %zu extends past the end of '%s'",
                                   i, object->display_name);
            }
            if (section->header.sh_size == 0U) {
                if (!ld_elf_range_ok(view.size, section->header.sh_offset, 0U)) {
                    return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                       "ELF section %zu has an out-of-range offset "
                                       "in '%s'",
                                       i, object->display_name);
                }
                section->data = view.bytes + (size_t) section->header.sh_offset;
            }
        }
    }

    if (section_name_index == LD_ELF_SHN_UNDEF) {
        for (size_t i = 0; i < section_count; i++) {
            if (object->sections[i].header.sh_name != 0U) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "ELF section %zu has a name but '%s' has no "
                                   "section-name table",
                                   i, object->display_name);
            }
            object->sections[i].name = "";
        }
        int status = ld_elf_decompress_debug_sections(ctx, object);
        if (status != LD_OK) return status;
        return ld_elf_validate_link_order(ctx, object);
    }

    ld_elf_section_t *names = &object->sections[section_name_index];
    if (names->header.sh_type != LD_ELF_SHT_STRTAB || names->nobits ||
        names->data_size == 0U || names->data[0] != '\0') {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "invalid ELF section-name string table in '%s'",
                           object->display_name);
    }
    object->section_names = (const char *) names->data;
    object->section_names_size = names->data_size;
    for (size_t i = 0; i < section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if (!ld_elf_string_at(object->section_names,
                              object->section_names_size,
                              section->header.sh_name, &section->name)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF section %zu has an invalid name offset in '%s'",
                               i, object->display_name);
        }
    }
    int status = ld_elf_decompress_debug_sections(ctx, object);
    if (status != LD_OK) return status;
    return ld_elf_validate_link_order(ctx, object);
}

static int ld_elf_parse_symbols(ld_elf_context_t *ctx, ld_elf_object_t *object,
                                ld_elf_view_t view) {
    size_t symtab_index = SIZE_MAX;
    for (size_t i = 0; i < object->section_count; i++) {
        if (object->sections[i].header.sh_type != LD_ELF_SHT_SYMTAB) continue;
        if (symtab_index != SIZE_MAX) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "multiple SHT_SYMTAB sections in '%s'",
                               object->display_name);
        }
        symtab_index = i;
    }
    if (symtab_index == SIZE_MAX) return LD_OK;

    size_t symtab_shndx_index = SIZE_MAX;
    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *candidate = &object->sections[i];
        if (candidate->header.sh_type != LD_ELF_SHT_SYMTAB_SHNDX) continue;
        if (candidate->header.sh_link >= object->section_count) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_SYMTAB_SHNDX section %zu has an out-of-range "
                    "symbol-table link in '%s'",
                    i, object->display_name);
        }
        uint32_t linked_type =
                object->sections[candidate->header.sh_link].header.sh_type;
        if (linked_type != LD_ELF_SHT_SYMTAB &&
            linked_type != LD_ELF_SHT_DYNSYM) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_SYMTAB_SHNDX section %zu does not link to a "
                    "symbol table in '%s'",
                    i, object->display_name);
        }
        if (candidate->header.sh_link != symtab_index) continue;
        if (symtab_shndx_index != SIZE_MAX) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "multiple SHT_SYMTAB_SHNDX sections link to the ELF "
                    "symbol table in '%s'",
                    object->display_name);
        }
        symtab_shndx_index = i;
    }

    ld_elf_section_t *symtab = &object->sections[symtab_index];
    if (symtab->header.sh_entsize != LD_ELF64_SYM_SIZE ||
        symtab->header.sh_size % LD_ELF64_SYM_SIZE != 0U) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "invalid ELF symbol-table entry size in '%s'",
                           object->display_name);
    }
    if (symtab->header.sh_link >= object->section_count) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF symbol string-table index is out of range in '%s'",
                           object->display_name);
    }
    ld_elf_section_t *strtab = &object->sections[symtab->header.sh_link];
    if (strtab->header.sh_type != LD_ELF_SHT_STRTAB || strtab->nobits ||
        strtab->data_size == 0U || strtab->data[0] != '\0') {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "invalid ELF symbol string table in '%s'",
                           object->display_name);
    }

    uint64_t count_u64 = symtab->header.sh_size / LD_ELF64_SYM_SIZE;
    if (count_u64 > SIZE_MAX || count_u64 > UINT32_MAX) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "too many ELF symbols in '%s'", object->display_name);
    }
    size_t symbol_count = (size_t) count_u64;
    if (symtab->header.sh_info > symbol_count) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF first-global symbol index is out of range in '%s'",
                           object->display_name);
    }

    ld_elf_section_t *symtab_shndx = NULL;
    if (symtab_shndx_index != SIZE_MAX) {
        symtab_shndx = &object->sections[symtab_shndx_index];
        size_t expected_size;
        if (symtab_shndx->nobits ||
            symtab_shndx->header.sh_entsize != sizeof(uint32_t) ||
            !ld_elf_multiply_size(symbol_count, sizeof(uint32_t),
                                  &expected_size) ||
            symtab_shndx->data_size != expected_size) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_SYMTAB_SHNDX section has invalid entry size or "
                    "count in '%s'",
                    object->display_name);
        }
    }
    object->symbols = calloc(symbol_count, sizeof(*object->symbols));
    if (symbol_count && !object->symbols) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading ELF symbols from '%s'",
                           object->display_name);
    }
    object->symbol_count = symbol_count;
    object->first_global_symbol = symtab->header.sh_info;
    object->symtab_section_index = (uint32_t) symtab_index;
    object->symbol_names = (const char *) strtab->data;
    object->symbol_names_size = strtab->data_size;

    const uint8_t *wire_symbols = ld_elf_view_range(
            view, symtab->header.sh_offset, symtab->header.sh_size);
    if (!wire_symbols && symtab->header.sh_size != 0U) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF symbol table extends past the end of '%s'",
                           object->display_name);
    }
    for (size_t i = 0; i < symbol_count; i++) {
        ld_elf_symbol_t *symbol = &object->symbols[i];
        ld_elf_decode_symbol(wire_symbols + i * LD_ELF64_SYM_SIZE,
                             &symbol->entry);
        symbol->index = (uint32_t) i;
        symbol->binding = LD_ELF_SYM_BIND(symbol->entry.st_info);
        symbol->type = LD_ELF_SYM_TYPE(symbol->entry.st_info);
        symbol->visibility = LD_ELF_SYM_VISIBILITY(symbol->entry.st_other);
        if (!ld_elf_string_at(object->symbol_names, object->symbol_names_size,
                              symbol->entry.st_name, &symbol->name)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF symbol %zu has an invalid name offset in '%s'",
                               i, object->display_name);
        }
        if (symbol->entry.st_shndx == LD_ELF_SHN_XINDEX) {
            if (!symtab_shndx) {
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF symbol %zu uses SHN_XINDEX without a linked "
                        "SHT_SYMTAB_SHNDX section in '%s'",
                        i, object->display_name);
            }
            uint32_t extended_index = ld_elf_read_u32(
                    symtab_shndx->data + i * sizeof(uint32_t));
            if (extended_index == LD_ELF_SHN_UNDEF ||
                extended_index >= object->section_count) {
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF symbol %zu has an out-of-range extended section "
                        "index %u in '%s'",
                        i, extended_index, object->display_name);
            }
            symbol->section = &object->sections[extended_index];
        } else if (symbol->entry.st_shndx < LD_ELF_SHN_LORESERVE) {
            if (symbol->entry.st_shndx >= object->section_count) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "ELF symbol %zu has an out-of-range section "
                                   "index in '%s'",
                                   i, object->display_name);
            }
            if (symbol->entry.st_shndx != LD_ELF_SHN_UNDEF) {
                symbol->section = &object->sections[symbol->entry.st_shndx];
            }
        }
        if (symbol->entry.st_shndx == LD_ELF_SHN_COMMON &&
            symbol->entry.st_value != 0U &&
            !ld_elf_is_power_of_two(symbol->entry.st_value)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "common symbol '%s' has invalid alignment %llu in '%s'",
                               symbol->name,
                               (unsigned long long) symbol->entry.st_value,
                               object->display_name);
        }
        if (symbol->type == LD_ELF_STT_SECTION && symbol->entry.st_name == 0U &&
            symbol->section) {
            symbol->name = symbol->section->name;
        }
    }
    return LD_OK;
}

static int ld_elf_parse_groups(ld_elf_context_t *ctx, ld_elf_object_t *object) {
    size_t group_count = 0U;
    for (size_t i = 0; i < object->section_count; i++) {
        if (object->sections[i].header.sh_type == LD_ELF_SHT_GROUP) {
            group_count++;
        }
    }
    object->groups = calloc(group_count, sizeof(*object->groups));
    if (group_count != 0U && !object->groups) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading ELF section groups from '%s'",
                           object->display_name);
    }
    object->group_count = group_count;

    size_t group_index = 0U;
    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *group_section = &object->sections[i];
        if (group_section->header.sh_type != LD_ELF_SHT_GROUP) continue;

        ld_elf_group_t *group = &object->groups[group_index];
        group->index = (uint32_t) group_index;
        group->section_index = (uint32_t) i;
        group->signature_symbol_index = group_section->header.sh_info;

        if (group_section->header.sh_link >= object->section_count ||
            group_section->header.sh_link != object->symtab_section_index ||
            object->sections[group_section->header.sh_link].header.sh_type !=
                    LD_ELF_SHT_SYMTAB) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s') in '%s', signature "
                    "symbol index %u, does not link to the object's current "
                    "SHT_SYMTAB",
                    group_index, i, group_section->name, object->display_name,
                    group_section->header.sh_info);
        }
        if (group_section->header.sh_info >= object->symbol_count) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s') in '%s' has "
                    "out-of-range signature symbol index %u (symbol count "
                    "%zu)",
                    group_index, i, group_section->name, object->display_name,
                    group_section->header.sh_info, object->symbol_count);
        }

        ld_elf_symbol_t *signature_symbol =
                &object->symbols[group_section->header.sh_info];
        if (signature_symbol->entry.st_name != 0U) {
            group->signature = signature_symbol->name;
        } else if (signature_symbol->type == LD_ELF_STT_SECTION &&
                   signature_symbol->section) {
            group->signature = signature_symbol->section->name;
        }
        if (!group->signature || group->signature[0] == '\0') {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s') in '%s' has an "
                    "empty signature at symbol index %u; only a non-empty "
                    "st_name or an unnamed STT_SECTION symbol is valid",
                    group_index, i, group_section->name, object->display_name,
                    group_section->header.sh_info);
        }
        if (group_section->header.sh_entsize != sizeof(uint32_t)) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s', signature '%s') "
                    "in '%s' has invalid sh_entsize %llu; expected 4",
                    group_index, i, group_section->name, group->signature,
                    object->display_name,
                    (unsigned long long) group_section->header.sh_entsize);
        }
        if (group_section->header.sh_size < 2U * sizeof(uint32_t) ||
            group_section->header.sh_size % sizeof(uint32_t) != 0U) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s', signature '%s') "
                    "in '%s' has invalid size %llu; a flag and at least one "
                    "member are required",
                    group_index, i, group_section->name, group->signature,
                    object->display_name,
                    (unsigned long long) group_section->header.sh_size);
        }

        group->flags = ld_elf_read_u32(group_section->data);
        if (group->flags != 0U && group->flags != LD_ELF_GRP_COMDAT) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF SHT_GROUP %zu (section %zu '%s', signature '%s') "
                    "in '%s' has unsupported flag value 0x%x; expected 0 or "
                    "GRP_COMDAT",
                    group_index, i, group_section->name, group->signature,
                    object->display_name, group->flags);
        }
        group->is_comdat = group->flags == LD_ELF_GRP_COMDAT;
        group->member_count =
                (size_t) (group_section->header.sh_size / sizeof(uint32_t)) -
                1U;
        group->members = calloc(group->member_count, sizeof(*group->members));
        if (!group->members) {
            return ld_elf_fail(
                    ctx, LD_IO_ERROR,
                    "out of memory reading %zu members of ELF SHT_GROUP %zu "
                    "(signature '%s') from '%s'",
                    group->member_count, group_index, group->signature,
                    object->display_name);
        }

        for (size_t j = 0; j < group->member_count; j++) {
            uint32_t member_index = ld_elf_read_u32(
                    group_section->data + (j + 1U) * sizeof(uint32_t));
            if (member_index == LD_ELF_SHN_UNDEF ||
                member_index >= object->section_count || member_index == i) {
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF SHT_GROUP %zu (section %zu '%s', signature "
                        "'%s') in '%s' has invalid member %zu section index "
                        "%u",
                        group_index, i, group_section->name, group->signature,
                        object->display_name, j, member_index);
            }
            ld_elf_section_t *member = &object->sections[member_index];
            if ((member->header.sh_flags & LD_ELF_SHF_GROUP) == 0U) {
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF SHT_GROUP %zu (signature '%s') in '%s' member "
                        "%zu is section %u '%s' without SHF_GROUP",
                        group_index, group->signature, object->display_name, j,
                        member_index, member->name);
            }
            if (member->group_index != LD_ELF_GROUP_NONE) {
                const ld_elf_group_t *owner =
                        &object->groups[member->group_index];
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF SHT_GROUP %zu (signature '%s') in '%s' member "
                        "%zu section %u '%s' is already owned by group %u "
                        "(signature '%s')",
                        group_index, group->signature, object->display_name, j,
                        member_index, member->name, member->group_index,
                        owner->signature ? owner->signature : "<unresolved>");
            }
            group->members[j] = member_index;
            member->group_index = (uint32_t) group_index;
        }
        group_index++;
    }

    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if ((section->header.sh_flags & LD_ELF_SHF_GROUP) != 0U &&
            section->group_index == LD_ELF_GROUP_NONE) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF section %zu '%s' in '%s' has SHF_GROUP but no "
                    "SHT_GROUP owner (signature '<none>', member index %zu)",
                    i, section->name, object->display_name, i);
        }
    }

    for (size_t i = 0; i < object->group_count; i++) {
        const ld_elf_group_t *group = &object->groups[i];
        for (size_t j = 0; j < group->member_count; j++) {
            uint32_t member_index = group->members[j];
            const ld_elf_section_t *member =
                    &object->sections[member_index];
            if (member->header.sh_type != LD_ELF_SHT_REL &&
                member->header.sh_type != LD_ELF_SHT_RELA) {
                continue;
            }
            uint32_t target_index = member->header.sh_info;
            if (target_index == LD_ELF_SHN_UNDEF ||
                target_index >= object->section_count ||
                object->sections[target_index].group_index != group->index) {
                const char *target_name =
                        target_index < object->section_count
                                ? object->sections[target_index].name
                                : "<out-of-range>";
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "ELF SHT_GROUP %zu (signature '%s') in '%s' has "
                        "relocation "
                        "member %zu section %u '%s' targeting section %u "
                        "'%s' outside the same group",
                        i, group->signature, object->display_name, j,
                        member_index, member->name, target_index, target_name);
            }
        }
    }
    return LD_OK;
}

static int ld_elf_parse_relocations(ld_elf_context_t *ctx,
                                    ld_elf_object_t *object,
                                    ld_elf_view_t view) {
    for (size_t i = 0; i < object->section_count; i++) {
        ld_elf_section_t *rela_section = &object->sections[i];
        bool implicit = rela_section->header.sh_type == LD_ELF_SHT_REL;
        if (!implicit &&
            rela_section->header.sh_type != LD_ELF_SHT_RELA) {
            continue;
        }
        if (rela_section->header.sh_info == LD_ELF_SHN_UNDEF ||
            rela_section->header.sh_info >= object->section_count) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF relocation target section is out of range in "
                               "'%s' (section '%s')",
                               object->display_name, rela_section->name);
        }
        ld_elf_section_t *target =
                &object->sections[rela_section->header.sh_info];
        if (ld_elf_skip_nonalloc_section(ctx, target)) {
            continue;
        }
        size_t entry_size = implicit ? LD_ELF64_REL_SIZE
                                     : LD_ELF64_RELA_SIZE;
        if (rela_section->header.sh_entsize != entry_size ||
            rela_section->header.sh_size % entry_size != 0U) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "invalid ELF %s relocation entry size in section '%s' "
                    "of '%s': sh_entsize is %llu and section size is %llu; "
                    "expected %zu-byte entries",
                    implicit ? "REL" : "RELA", rela_section->name,
                    object->display_name,
                    (unsigned long long) rela_section->header.sh_entsize,
                    (unsigned long long) rela_section->header.sh_size,
                    entry_size);
        }
        if (!object->symbols ||
            rela_section->header.sh_link != object->symtab_section_index) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF relocation section '%s' in '%s' does not link "
                               "to its SHT_SYMTAB",
                               rela_section->name, object->display_name);
        }
        const uint8_t *wire = ld_elf_view_range(
                view, rela_section->header.sh_offset,
                rela_section->header.sh_size);
        if (!wire && rela_section->header.sh_size != 0U) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "ELF relocation section '%s' extends past the end "
                               "of '%s'",
                               rela_section->name, object->display_name);
        }
        size_t count =
                (size_t) (rela_section->header.sh_size / entry_size);
        for (size_t j = 0; j < count; j++) {
            ld_elf64_rela_t decoded;
            if (implicit) {
                ld_elf_decode_rel(wire + j * entry_size, &decoded);
            } else {
                ld_elf_decode_relocation(wire + j * entry_size, &decoded);
            }
            uint32_t symbol_index = LD_ELF_RELA_SYMBOL(decoded.r_info);
            if (symbol_index >= object->symbol_count) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "ELF relocation %zu in section '%s' of '%s' "
                                   "uses out-of-range symbol index %u",
                                   j, rela_section->name, object->display_name,
                                   symbol_index);
            }
            if (decoded.r_offset > target->header.sh_size) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "ELF relocation %zu in section '%s' of '%s' "
                                   "has offset 0x%llx past target section '%s'",
                                   j, rela_section->name, object->display_name,
                                   (unsigned long long) decoded.r_offset,
                                   target->name);
            }
            ld_elf_relocation_t relocation = {
                    .offset = decoded.r_offset,
                    .symbol_index = symbol_index,
                    .type = LD_ELF_RELA_TYPE(decoded.r_info),
                    .addend = decoded.r_addend,
                    .relocation_section_index = (uint32_t) i,
                    .x86_tls_pair_index = SIZE_MAX,
                    .aarch64_thunk_entry_index =
                            LD_ELF_AARCH64_NO_THUNK,
            };
            if (implicit) {
                int result = ld_elf_rel_decode_addend(
                        ctx, object, target, &relocation,
                        &relocation.addend);
                if (result != LD_OK) return result;
            }
            int result = ld_elf_relocation_push(ctx, object, target,
                                                &relocation);
            if (result != LD_OK) return result;
        }
    }
    return LD_OK;
}

/*
 * This mirrors the record model used by Zig's Elf/Object.parseEhFrame at the
 * referenced commit. Keeping record boundaries lets the linker remove FDEs
 * owned by discarded COMDAT sections without retaining stale unwind entries.
 */
static int ld_elf_parse_eh_frame_section(ld_elf_context_t *ctx,
                                         ld_elf_object_t *object,
                                         ld_elf_section_t *section) {
    uint64_t offset = 0U;
    while (offset < section->data_size) {
        uint64_t remaining = section->data_size - offset;
        if (remaining < sizeof(uint32_t)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "truncated .eh_frame length at offset 0x%llx in '%s'",
                               (unsigned long long) offset,
                               object->display_name);
        }

        uint32_t length32 = ld_elf_read_u32(section->data + offset);
        if (length32 == 0U) {
            for (uint64_t i = offset; i < section->data_size; i++) {
                if (section->data[i] != 0U) {
                    return ld_elf_fail(
                            ctx, LD_INVALID_INPUT,
                            "non-zero bytes follow the .eh_frame terminator at "
                            "offset 0x%llx in '%s'",
                            (unsigned long long) offset,
                            object->display_name);
                }
            }
            break;
        }

        uint64_t payload_size;
        uint64_t record_size;
        uint8_t length_field_size;
        uint8_t offset_size;
        if (length32 == UINT32_MAX) {
            if (remaining < 12U) {
                return ld_elf_fail(
                        ctx, LD_INVALID_INPUT,
                        "truncated DWARF64 .eh_frame length at offset 0x%llx "
                        "in '%s'",
                        (unsigned long long) offset, object->display_name);
            }
            payload_size = ld_elf_read_u64(section->data + offset + 4U);
            length_field_size = 12U;
            /* The LSB .eh_frame CIE id/pointer is always four bytes, even
             * when the initial-length field uses the DWARF64 form. */
            offset_size = 4U;
        } else {
            payload_size = length32;
            length_field_size = 4U;
            offset_size = 4U;
        }
        if (payload_size < offset_size ||
            payload_size > UINT64_MAX - length_field_size) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "invalid .eh_frame record length at offset 0x%llx in '%s'",
                    (unsigned long long) offset, object->display_name);
        }
        record_size = payload_size + length_field_size;
        if (record_size > remaining) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    ".eh_frame record at offset 0x%llx extends past section "
                    "end in '%s'",
                    (unsigned long long) offset, object->display_name);
        }

        const uint8_t *id_bytes = section->data + offset + length_field_size;
        uint64_t id = ld_elf_read_u32(id_bytes);
        ld_elf_eh_record_t record = {
                .input_offset = offset,
                .size = record_size,
                .output_offset = 0U,
                .cie_record_index = LD_ELF_EH_CIE_NONE,
                .owner_section_index = LD_ELF_GROUP_NONE,
                .owner_relocation_index = LD_ELF_EH_RELOCATION_NONE,
                .length_field_size = length_field_size,
                .offset_size = offset_size,
                .cie = id == 0U,
                .alive = false,
        };
        if (!record.cie && id > offset + length_field_size) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx has an invalid CIE pointer "
                    "0x%llx in '%s'",
                    (unsigned long long) offset, (unsigned long long) id,
                    object->display_name);
        }
        int status = ld_elf_eh_record_push(ctx, object, section, &record);
        if (status != LD_OK) return status;
        offset += record_size;
    }

    for (size_t i = 0; i < section->relocation_count; i++) {
        const ld_elf_relocation_t *relocation = &section->relocations[i];
        bool found = false;
        for (size_t j = 0; j < section->eh_record_count; j++) {
            const ld_elf_eh_record_t *record = &section->eh_records[j];
            if (relocation->offset >= record->input_offset &&
                relocation->offset - record->input_offset < record->size) {
                found = true;
                break;
            }
        }
        if (!found) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    ".eh_frame relocation at offset 0x%llx in '%s' is in the "
                    "terminator, padding, or outside a record",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
    }

    for (size_t i = 0; i < section->eh_record_count; i++) {
        ld_elf_eh_record_t *record = &section->eh_records[i];
        if (record->cie) continue;
        uint64_t pointer_offset = record->input_offset +
                                  record->length_field_size;
        const uint8_t *pointer = section->data + pointer_offset;
        uint64_t delta = ld_elf_read_u32(pointer);
        uint64_t cie_offset = pointer_offset - delta;
        for (size_t j = 0; j < section->eh_record_count; j++) {
            if (section->eh_records[j].cie &&
                section->eh_records[j].input_offset == cie_offset) {
                record->cie_record_index = (uint32_t) j;
                break;
            }
        }
        if (record->cie_record_index == LD_ELF_EH_CIE_NONE) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx has no matching CIE at "
                    "offset 0x%llx in '%s'",
                    (unsigned long long) record->input_offset,
                    (unsigned long long) cie_offset, object->display_name);
        }
        if (section->eh_records[record->cie_record_index].offset_size !=
            record->offset_size) {
            return ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx and its CIE use different "
                    "offset widths in '%s'",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }

        const ld_elf_relocation_t *owner_relocation = NULL;
        size_t owner_relocation_index = LD_ELF_EH_RELOCATION_NONE;
        uint64_t owner_offset = pointer_offset + sizeof(uint32_t);
        for (size_t j = 0; j < section->relocation_count; j++) {
            const ld_elf_relocation_t *relocation = &section->relocations[j];
            if (relocation->offset != owner_offset) continue;
            const ld_elf_symbol_t *candidate =
                    &object->symbols[relocation->symbol_index];
            if (!candidate->section ||
                (candidate->section->header.sh_flags & LD_ELF_SHF_ALLOC) == 0U) {
                continue;
            }
            if (!owner_relocation ||
                (candidate->section->header.sh_flags & LD_ELF_SHF_EXECINSTR) !=
                        0U) {
                owner_relocation = relocation;
                owner_relocation_index = j;
            }
            if ((candidate->section->header.sh_flags & LD_ELF_SHF_EXECINSTR) !=
                0U) {
                break;
            }
        }
        /* Zig drops ET_REL FDEs without a relocation because no input atom can
         * be associated with them reliably. */
        if (!owner_relocation) continue;

        const ld_elf_symbol_t *owner_symbol =
                &object->symbols[owner_relocation->symbol_index];
        record->owner_section_index = owner_symbol->section->index;
        record->owner_relocation_index = owner_relocation_index;
    }
    return LD_OK;
}

static int ld_elf_parse_eh_frames(ld_elf_context_t *ctx,
                                  ld_elf_object_t *object) {
    for (size_t i = 1; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if (strcmp(section->name, ".eh_frame") != 0) continue;
        if (section->nobits ||
            (section->header.sh_type != LD_ELF_SHT_PROGBITS &&
             section->header.sh_type != LD_ELF_SHT_X86_64_UNWIND)) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "invalid .eh_frame section type %u in '%s'",
                               section->header.sh_type, object->display_name);
        }
        int status = ld_elf_parse_eh_frame_section(ctx, object, section);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static int ld_elf_parse_object_view(ld_elf_context_t *ctx, ld_elf_file_t *file,
                                    uint64_t file_offset, size_t size,
                                    const char *member_name,
                                    ld_elf_archive_t *archive, bool lazy,
                                    ld_elf_object_t **result) {
    if (!ld_elf_range_ok(file->size, file_offset, size)) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "ELF object view is out of range in '%s'", file->path);
    }

    ld_elf_object_t *object = calloc(1, sizeof(*object));
    if (!object) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading ELF object from '%s'", file->path);
    }
    object->file = file;
    object->archive = archive;
    object->bytes = file->bytes + (size_t) file_offset;
    object->size = size;
    object->file_offset = file_offset;
    object->archive_member = archive != NULL;
    object->lazy = lazy;
    object->selected = !lazy;
    if (member_name) object->member_name = ld_elf_strndup(member_name, strlen(member_name));
    object->display_name = ld_elf_display_name(
            archive ? archive->file->path : file->path, member_name);
    if ((member_name && !object->member_name) || !object->display_name) {
        ld_elf_object_destroy(object);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording ELF object from '%s'", file->path);
    }

    ld_elf_view_t view = {.bytes = object->bytes, .size = object->size};
    const uint8_t *header_bytes = ld_elf_view_range(view, 0U, LD_ELF64_EHDR_SIZE);
    if (!header_bytes) {
        int error = ld_elf_fail(ctx, LD_INVALID_INPUT,
                                "truncated ELF header in '%s'",
                                object->display_name);
        ld_elf_object_destroy(object);
        return error;
    }
    ld_elf_decode_header(header_bytes, &object->header);
    const uint8_t *ident = object->header.e_ident;
    if (ident[0] != LD_ELF_MAGIC_0 || ident[1] != LD_ELF_MAGIC_1 ||
        ident[2] != LD_ELF_MAGIC_2 || ident[3] != LD_ELF_MAGIC_3) {
        int error = ld_elf_fail(ctx, LD_INVALID_INPUT,
                                "'%s' is not an ELF file", object->display_name);
        ld_elf_object_destroy(object);
        return error;
    }
    if (ident[LD_ELF_EI_CLASS] != LD_ELF_CLASS_64 ||
        ident[LD_ELF_EI_DATA] != LD_ELF_DATA_LSB ||
        ident[LD_ELF_EI_VERSION] != LD_ELF_VERSION_CURRENT) {
        int error = ld_elf_fail(
                ctx, LD_UNSUPPORTED,
                "unsupported ELF encoding in '%s': expected ELF64 little-endian",
                object->display_name);
        ld_elf_object_destroy(object);
        return error;
    }
    if (object->header.e_type != LD_ELF_ET_REL) {
        int error = ld_elf_fail(ctx, LD_UNSUPPORTED,
                                "unsupported ELF type %u in '%s': expected ET_REL",
                                object->header.e_type, object->display_name);
        ld_elf_object_destroy(object);
        return error;
    }
    if (object->header.e_version != LD_ELF_VERSION_CURRENT ||
        object->header.e_ehsize != LD_ELF64_EHDR_SIZE) {
        int error = ld_elf_fail(ctx, LD_INVALID_INPUT,
                                "invalid ELF header version or size in '%s'",
                                object->display_name);
        ld_elf_object_destroy(object);
        return error;
    }
    uint16_t expected_machine = ctx->options
                                        ? ld_elf_expected_machine(ctx->options->arch)
                                        : 0U;
    if (expected_machine == 0U) {
        int error = ld_elf_fail(ctx, LD_INVALID_ARGUMENT,
                                "ELF target architecture is not configured");
        ld_elf_object_destroy(object);
        return error;
    }
    if (object->header.e_machine != expected_machine) {
        int error = ld_elf_fail(
                ctx, LD_UNSUPPORTED,
                "ELF architecture mismatch in '%s': found %s (machine %u), "
                "expected %s (machine %u)",
                object->display_name,
                ld_elf_machine_name(object->header.e_machine),
                object->header.e_machine, ld_elf_machine_name(expected_machine),
                expected_machine);
        ld_elf_object_destroy(object);
        return error;
    }
    if (object->header.e_phnum != 0U) {
        if (object->header.e_phentsize != LD_ELF64_PHDR_SIZE) {
            int error = ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "invalid ELF program-header entry size in relocatable '%s'",
                    object->display_name);
            ld_elf_object_destroy(object);
            return error;
        }
        size_t program_table_size;
        if (!ld_elf_multiply_size(object->header.e_phnum,
                                  LD_ELF64_PHDR_SIZE,
                                  &program_table_size) ||
            !ld_elf_range_ok(view.size, object->header.e_phoff,
                             program_table_size)) {
            int error = ld_elf_fail(
                    ctx, LD_INVALID_INPUT,
                    "ELF program-header table extends past the end of '%s'",
                    object->display_name);
            ld_elf_object_destroy(object);
            return error;
        }
    }

    int parse_result = ld_elf_parse_sections(ctx, object, view);
    if (parse_result == LD_OK) {
        parse_result = ld_elf_parse_symbols(ctx, object, view);
    }
    if (parse_result == LD_OK) {
        parse_result = ld_elf_parse_groups(ctx, object);
    }
    if (parse_result == LD_OK) {
        parse_result = ld_elf_parse_relocations(ctx, object, view);
    }
    if (parse_result == LD_OK) {
        parse_result = ld_elf_parse_eh_frames(ctx, object);
    }
    if (parse_result != LD_OK) {
        ld_elf_object_destroy(object);
        return parse_result;
    }

    if (ld_elf_object_push(ctx, object) != LD_OK) {
        ld_elf_object_destroy(object);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording ELF object '%s'",
                           file->path);
    }
    if (archive && ld_elf_archive_member_push(archive, object) != LD_OK) {
        ctx->objects.count--;
        ld_elf_object_destroy(object);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording archive member in '%s'",
                           file->path);
    }
    if (result) *result = object;
    return LD_OK;
}

static int ld_elf_parse_archive(ld_elf_context_t *ctx, ld_elf_file_t *file,
                                bool thin) {
    ld_elf_archive_t *archive = calloc(1, sizeof(*archive));
    if (!archive) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory reading archive '%s'", file->path);
    }
    archive->file = file;
    archive->thin = thin;
    if (ld_elf_archive_push(ctx, archive) != LD_OK) {
        free(archive);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory recording archive '%s'", file->path);
    }

    size_t offset = LD_ELF_AR_MAGIC_SIZE;
    while (offset < file->size) {
        ld_elf_archive_record_t record;
        ld_elf_archive_result_t decode = ld_elf_archive_record_at(
                file->bytes, file->size, offset, thin, &record);
        if (decode != LD_ELF_ARCHIVE_OK) {
            int code = decode == LD_ELF_ARCHIVE_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : LD_INVALID_INPUT;
            return ld_elf_fail(ctx, code, "%s at offset 0x%zx in '%s'",
                               ld_elf_archive_result_string(decode), offset,
                               file->path);
        }
        if (record.kind == LD_ELF_ARCHIVE_MEMBER_NAME_TABLE) {
            if (archive->gnu_name_table) {
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "duplicate GNU archive long-name table in '%s'",
                                   file->path);
            }
            archive->gnu_name_table =
                    (const char *) file->bytes + record.payload_offset;
            archive->gnu_name_table_size = record.payload_size;
        }
        offset = record.next_offset;
    }

    offset = LD_ELF_AR_MAGIC_SIZE;
    while (offset < file->size) {
        ld_elf_archive_record_t record;
        ld_elf_archive_result_t decode = ld_elf_archive_record_at(
                file->bytes, file->size, offset, thin, &record);
        if (decode != LD_ELF_ARCHIVE_OK) {
            int code = decode == LD_ELF_ARCHIVE_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : LD_INVALID_INPUT;
            return ld_elf_fail(ctx, code, "%s at offset 0x%zx in '%s'",
                               ld_elf_archive_result_string(decode), offset,
                               file->path);
        }
        char *member_name = NULL;
        size_t object_offset = 0U, object_size = 0U;
        decode = ld_elf_archive_member_name(
                file->bytes, file->size, &record, archive->gnu_name_table,
                archive->gnu_name_table_size, thin, &member_name,
                &object_offset, &object_size);
        if (decode != LD_ELF_ARCHIVE_OK) {
            int code = decode == LD_ELF_ARCHIVE_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : LD_INVALID_INPUT;
            return ld_elf_fail(ctx, code, "%s at offset 0x%zx in '%s'",
                               ld_elf_archive_result_string(decode), offset,
                               file->path);
        }
        if (record.kind == LD_ELF_ARCHIVE_MEMBER_REGULAR) {
            if (archive->member_count > UINT32_MAX) {
                free(member_name);
                return ld_elf_fail(ctx, LD_INVALID_INPUT,
                                   "too many members in archive '%s'", file->path);
            }
            int status;
            if (thin) {
                char *member_path = NULL;
                decode = ld_elf_archive_resolve_member_path(
                        file->path, member_name, &member_path);
                if (decode != LD_ELF_ARCHIVE_OK) {
                    int code = decode == LD_ELF_ARCHIVE_OUT_OF_MEMORY
                                       ? LD_IO_ERROR
                                       : LD_INVALID_INPUT;
                    int error = ld_elf_fail(
                            ctx, code, "%s for '%s' at offset 0x%zx in '%s'",
                            ld_elf_archive_result_string(decode), member_name,
                            offset, file->path);
                    free(member_name);
                    return error;
                }
                char *display_name =
                        ld_elf_display_name(file->path, member_name);
                if (!display_name) {
                    free(member_path);
                    free(member_name);
                    return ld_elf_fail(
                            ctx, LD_IO_ERROR,
                            "out of memory recording thin archive member in '%s'",
                            file->path);
                }
                ld_elf_file_t *member_file = NULL;
                status = ld_elf_read_file(ctx, member_path, display_name,
                                          &member_file);
                free(display_name);
                free(member_path);
                if (status == LD_OK) {
                    status = ld_elf_parse_object_view(
                            ctx, member_file, 0U, member_file->size,
                            member_name, archive, true, NULL);
                }
            } else {
                status = ld_elf_parse_object_view(
                        ctx, file, object_offset, object_size, member_name,
                        archive, true, NULL);
            }
            free(member_name);
            if (status != LD_OK) return status;
        }
        offset = record.next_offset;
    }
    return LD_OK;
}

static bool ld_elf_input_regular_file(const char *path) {
    struct stat status;
    return path && stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static char *ld_elf_input_join_path(const char *prefix,
                                    const char *suffix) {
    if (!prefix || !suffix) return NULL;
    size_t prefix_length = strlen(prefix);
    size_t suffix_length = strlen(suffix);
    bool separator = prefix_length != 0U && suffix_length != 0U &&
                     prefix[prefix_length - 1U] != '/' && suffix[0] != '/';
    bool duplicate_separator = prefix_length != 0U && suffix_length != 0U &&
                               prefix[prefix_length - 1U] == '/' &&
                               suffix[0] == '/';
    size_t skipped = duplicate_separator ? 1U : 0U;
    size_t suffix_bytes = suffix_length - skipped;
    size_t separator_bytes = separator ? 1U : 0U;
    if (suffix_bytes > SIZE_MAX - separator_bytes - 1U ||
        prefix_length >
                SIZE_MAX - suffix_bytes - separator_bytes - 1U) {
        return NULL;
    }
    size_t size = prefix_length + suffix_bytes + separator_bytes + 1U;
    char *path = malloc(size);
    if (!path) return NULL;
    memcpy(path, prefix, prefix_length);
    size_t cursor = prefix_length;
    if (separator) path[cursor++] = '/';
    memcpy(path + cursor, suffix + skipped, suffix_bytes);
    path[cursor + suffix_bytes] = '\0';
    return path;
}

static char *ld_elf_input_directory(const char *path) {
    if (!path) return NULL;
    const char *slash = strrchr(path, '/');
    if (!slash) return ld_elf_strndup(".", 1U);
    if (slash == path) return ld_elf_strndup("/", 1U);
    return ld_elf_strndup(path, (size_t) (slash - path));
}

static int ld_elf_input_use_candidate(ld_elf_context_t *ctx,
                                      char *candidate, char **result) {
    if (!candidate) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory resolving GNU linker-script path");
    }
    if (ld_elf_input_regular_file(candidate)) {
        *result = candidate;
    } else {
        free(candidate);
    }
    return LD_OK;
}

static int ld_elf_input_try_joined_candidate(ld_elf_context_t *ctx,
                                             const char *directory,
                                             const char *name,
                                             char **result) {
    if (*result) return LD_OK;
    return ld_elf_input_use_candidate(
            ctx, ld_elf_input_join_path(directory, name), result);
}

static int ld_elf_input_try_directory(ld_elf_context_t *ctx,
                                      const char *script_directory,
                                      const char *directory,
                                      const char *name, char **result) {
    if (*result || !directory || !*directory) return LD_OK;
    const char *sysroot = ctx->options->sysroot;
    bool has_sysroot = sysroot && *sysroot;
    if (directory[0] == '=') {
        if (!has_sysroot) return LD_OK;
        char *rooted = ld_elf_input_join_path(sysroot, directory + 1U);
        if (!rooted) {
            return ld_elf_fail(
                    ctx, LD_IO_ERROR,
                    "out of memory expanding sysroot SEARCH_DIR");
        }
        int status = ld_elf_input_try_joined_candidate(
                ctx, rooted, name, result);
        free(rooted);
        return status;
    }
    static const char sysroot_variable[] = "$SYSROOT";
    if (strncmp(directory, sysroot_variable,
                sizeof(sysroot_variable) - 1U) == 0) {
        if (!has_sysroot) return LD_OK;
        char *rooted = ld_elf_input_join_path(
                sysroot, directory + sizeof(sysroot_variable) - 1U);
        if (!rooted) {
            return ld_elf_fail(
                    ctx, LD_IO_ERROR,
                    "out of memory expanding $SYSROOT SEARCH_DIR");
        }
        int status = ld_elf_input_try_joined_candidate(
                ctx, rooted, name, result);
        free(rooted);
        return status;
    }
    if (directory[0] == '/') {
        if (has_sysroot) {
            char *rooted = ld_elf_input_join_path(sysroot, directory);
            if (!rooted) {
                return ld_elf_fail(
                        ctx, LD_IO_ERROR,
                        "out of memory resolving sysroot library path");
            }
            int status = ld_elf_input_try_joined_candidate(
                    ctx, rooted, name, result);
            free(rooted);
            if (status != LD_OK || *result) return status;
        }
        return ld_elf_input_try_joined_candidate(
                ctx, directory, name, result);
    }

    char *relative = ld_elf_input_join_path(script_directory, directory);
    if (!relative) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory resolving relative SEARCH_DIR");
    }
    int status = ld_elf_input_try_joined_candidate(
            ctx, relative, name, result);
    free(relative);
    if (status != LD_OK || *result) return status;
    return ld_elf_input_try_joined_candidate(ctx, directory, name, result);
}

static int ld_elf_input_resolve_script_library(
        ld_elf_context_t *ctx, const ld_elf_script_t *script,
        const char *script_directory, const char *argument,
        char **result) {
    *result = NULL;
    const char *name = argument + 2U;
    char *filename = NULL;
    if (*name == ':') {
        name++;
        filename = ld_elf_strndup(name, strlen(name));
    } else if (*name) {
        size_t length = strlen(name);
        if (length <= SIZE_MAX - sizeof("lib.a")) {
            filename = malloc(length + sizeof("lib.a"));
            if (filename)
                snprintf(filename, length + sizeof("lib.a"),
                         "lib%s.a", name);
        }
    }
    if (!filename || !*filename) {
        free(filename);
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "invalid GNU linker-script library '%s'",
                           argument);
    }

    int status = LD_OK;
    for (size_t i = 0; i < script->search_dir_count && !*result; i++) {
        status = ld_elf_input_try_directory(
                ctx, script_directory, script->search_dirs[i], filename,
                result);
        if (status != LD_OK) break;
    }
    for (size_t i = 0;
         status == LD_OK && i < ctx->options->library_paths.count && !*result;
         i++) {
        status = ld_elf_input_try_directory(
                ctx, script_directory, ctx->options->library_paths.items[i],
                filename, result);
    }
    if (status == LD_OK && !*result) {
        status = ld_elf_input_try_joined_candidate(
                ctx, script_directory, filename, result);
    }
    if (status == LD_OK && !*result && ctx->options->sysroot &&
        *ctx->options->sysroot) {
        static const char *suffixes[] = {"usr/lib", "lib"};
        for (size_t i = 0;
             i < sizeof(suffixes) / sizeof(suffixes[0]) && !*result; i++) {
            char *directory = ld_elf_input_join_path(
                    ctx->options->sysroot, suffixes[i]);
            if (!directory) {
                status = ld_elf_fail(
                        ctx, LD_IO_ERROR,
                        "out of memory resolving sysroot script library");
                break;
            }
            status = ld_elf_input_try_joined_candidate(
                    ctx, directory, filename, result);
            free(directory);
            if (status != LD_OK) break;
        }
    }
    free(filename);
    return status;
}

static int ld_elf_input_resolve_script_path(
        ld_elf_context_t *ctx, const ld_elf_script_t *script,
        const char *script_directory, const char *argument,
        char **result) {
    *result = NULL;
    if (argument[0] == '-' && argument[1] == 'l') {
        return ld_elf_input_resolve_script_library(
                ctx, script, script_directory, argument, result);
    }

    const char *sysroot = ctx->options->sysroot;
    bool has_sysroot = sysroot && *sysroot;
    if (argument[0] == '=') {
        if (!has_sysroot) return LD_OK;
        return ld_elf_input_use_candidate(
                ctx, ld_elf_input_join_path(sysroot, argument + 1U), result);
    }
    static const char sysroot_variable[] = "$SYSROOT";
    if (strncmp(argument, sysroot_variable,
                sizeof(sysroot_variable) - 1U) == 0) {
        if (!has_sysroot) return LD_OK;
        return ld_elf_input_use_candidate(
                ctx,
                ld_elf_input_join_path(
                        sysroot,
                        argument + sizeof(sysroot_variable) - 1U),
                result);
    }
    if (argument[0] == '/') {
        if (has_sysroot) {
            int status = ld_elf_input_use_candidate(
                    ctx, ld_elf_input_join_path(sysroot, argument), result);
            if (status != LD_OK || *result) return status;
        }
        return ld_elf_input_use_candidate(
                ctx, ld_elf_strndup(argument, strlen(argument)), result);
    }

    int status = ld_elf_input_try_joined_candidate(
            ctx, script_directory, argument, result);
    if (status != LD_OK || *result) return status;
    for (size_t i = 0; i < script->search_dir_count && !*result; i++) {
        status = ld_elf_input_try_directory(
                ctx, script_directory, script->search_dirs[i], argument,
                result);
        if (status != LD_OK) return status;
    }
    for (size_t i = 0; i < ctx->options->library_paths.count && !*result;
         i++) {
        status = ld_elf_input_try_directory(
                ctx, script_directory, ctx->options->library_paths.items[i],
                argument, result);
        if (status != LD_OK) return status;
    }
    if (!*result) {
        status = ld_elf_input_use_candidate(
                ctx, ld_elf_strndup(argument, strlen(argument)), result);
    }
    return status;
}

static int ld_elf_load_script(ld_elf_context_t *ctx, ld_elf_file_t *file) {
    if (file->size > LD_ELF_SCRIPT_MAX_SIZE) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "GNU linker script '%s' is too large", file->path);
    }
    for (size_t i = 0; i < ctx->script_depth; i++) {
        if (strcmp(ctx->script_stack[i], file->path) == 0) {
            return ld_elf_fail(ctx, LD_INVALID_INPUT,
                               "GNU linker script cycle includes '%s'",
                               file->path);
        }
    }
    if (ctx->script_depth == LD_ELF_SCRIPT_LOAD_DEPTH) {
        return ld_elf_fail(ctx, LD_INVALID_INPUT,
                           "GNU linker-script nesting exceeds %u inputs at '%s'",
                           (unsigned) LD_ELF_SCRIPT_LOAD_DEPTH, file->path);
    }

    ld_elf_script_t script;
    ld_elf_script_error_t error;
    ld_elf_script_result_t parse = ld_elf_script_parse(
            file->bytes, file->size, ctx->options->arch, &script, &error);
    if (parse != LD_ELF_SCRIPT_OK) {
        int code = parse == LD_ELF_SCRIPT_UNSUPPORTED_ARCH
                           ? LD_UNSUPPORTED
                           : (parse == LD_ELF_SCRIPT_OUT_OF_MEMORY
                                      ? LD_IO_ERROR
                                      : LD_INVALID_INPUT);
        return ld_elf_fail(
                ctx, code, "%s '%s' at offset 0x%zx: %s",
                ld_elf_script_result_string(parse), file->path, error.offset,
                error.message ? error.message : "parse failed");
    }

    char *directory = ld_elf_input_directory(file->path);
    if (!directory) {
        ld_elf_script_deinit(&script);
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory resolving directory of GNU linker "
                           "script '%s'",
                           file->path);
    }

    ctx->script_stack[ctx->script_depth++] = file->path;
    int status = LD_OK;
    for (size_t i = 0; i < script.input_count; i++) {
        char *input_path = NULL;
        status = ld_elf_input_resolve_script_path(
                ctx, &script, directory, script.inputs[i].path,
                &input_path);
        if (status != LD_OK) break;
        if (!input_path) {
            status = ld_elf_fail(
                    ctx, LD_IO_ERROR,
                    "cannot find GNU linker-script input '%s' listed in '%s'",
                    script.inputs[i].path, file->path);
            break;
        }
        status = ld_elf_load_input(ctx, input_path);
        free(input_path);
        if (status != LD_OK) break;
    }
    ctx->script_depth--;
    ctx->script_stack[ctx->script_depth] = NULL;
    free(directory);
    ld_elf_script_deinit(&script);
    return status;
}

int ld_elf_load_input(ld_elf_context_t *ctx, const char *path) {
    if (!ctx || !ctx->options || !path || !*path) return LD_INVALID_ARGUMENT;
    if (ld_elf_expected_machine(ctx->options->arch) == 0U) {
        return ld_elf_fail(ctx, LD_UNSUPPORTED,
                           "unsupported ELF target architecture %d",
                           (int) ctx->options->arch);
    }
    ld_elf_file_t *file = NULL;
    int status = ld_elf_read_file(ctx, path, path, &file);
    if (status != LD_OK) return status;
    if (!file) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "internal error reading ELF input '%s'", path);
    }
    if (file->size >= LD_ELF_AR_MAGIC_SIZE &&
        memcmp(file->bytes, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE) == 0) {
        return ld_elf_parse_archive(ctx, file, false);
    }
    if (file->size >= LD_ELF_AR_MAGIC_SIZE &&
        memcmp(file->bytes, LD_ELF_AR_THIN_MAGIC,
               LD_ELF_AR_MAGIC_SIZE) == 0) {
        return ld_elf_parse_archive(ctx, file, true);
    }
    if (file->size >= 4U && file->bytes[0] == LD_ELF_MAGIC_0 &&
        file->bytes[1] == LD_ELF_MAGIC_1 &&
        file->bytes[2] == LD_ELF_MAGIC_2 &&
        file->bytes[3] == LD_ELF_MAGIC_3) {
        return ld_elf_parse_object_view(ctx, file, 0U, file->size, NULL, NULL,
                                        false, NULL);
    }
    return ld_elf_load_script(ctx, file);
}

int ld_elf_load_options_inputs(ld_elf_context_t *ctx) {
    if (!ctx || !ctx->options) return LD_INVALID_ARGUMENT;
    for (size_t i = 0; i < ctx->options->inputs.count; i++) {
        int status = ld_elf_load_input(ctx, ctx->options->inputs.items[i]);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

int ld_elf_select_object(ld_elf_context_t *ctx, ld_elf_object_t *object) {
    if (!ctx || !object) return LD_INVALID_ARGUMENT;
    bool owned = false;
    for (size_t i = 0; i < ctx->objects.count; i++) {
        if (ctx->objects.items[i] == object) {
            owned = true;
            break;
        }
    }
    if (!owned) {
        return ld_elf_fail(ctx, LD_INVALID_ARGUMENT,
                           "attempted to select an object outside this ELF context");
    }
    if (object->selected) return LD_OK;
    object->selected = true;
    if (object->archive) object->archive->selected_member_count++;
    return LD_OK;
}

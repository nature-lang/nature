#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARCHIVE_HEADER_SIZE 60U
#define COFF_HEADER_SIZE 20U
#define COFF_BIGOBJ_HEADER_SIZE 56U
#define COFF_SECTION_HEADER_SIZE 40U

static int keep_dwarf;

static int fail(const char *path, const char *message) {
    fprintf(stderr, "archive-normalize: %s: %s\n", path, message);
    return 1;
}

static int decimal(const uint8_t *bytes, size_t size, size_t *value) {
    size_t result = 0U;
    size_t index = 0U;
    while (index < size && bytes[index] == ' ') index++;
    if (index == size) return 0;
    for (; index < size && bytes[index] != ' '; index++) {
        if (bytes[index] < '0' || bytes[index] > '9') return 0;
        unsigned digit = (unsigned) (bytes[index] - '0');
        if (result > (SIZE_MAX - digit) / 10U) return 0;
        result = result * 10U + digit;
    }
    while (index < size) {
        if (bytes[index] != ' ' && bytes[index] != '\0') return 0;
        index++;
    }
    *value = result;
    return 1;
}

static int absolute_name(const uint8_t *name, size_t length) {
    if (length && (name[0] == '/' || name[0] == '\\')) return 1;
    return length >= 3U && isalpha(name[0]) && name[1] == ':' &&
           (name[2] == '/' || name[2] == '\\');
}

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t) bytes[0] | (uint16_t) bytes[1] << 8U;
}

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8U |
           (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

static const char *coff_section_name(const uint8_t *section,
                                     const uint8_t *bytes, size_t size,
                                     size_t string_table) {
    static char short_name[9];
    if (section[0] != '/') {
        memcpy(short_name, section, 8U);
        short_name[8] = '\0';
        return short_name;
    }
    size_t offset = 0U;
    if (!decimal(section + 1U, 7U, &offset) || offset < 4U ||
        string_table > size || offset > size - string_table)
        return NULL;
    const char *name = (const char *) bytes + string_table + offset;
    if (!memchr(name, '\0', size - string_table - offset)) return NULL;
    return name;
}

static void clear_section_aux_checksum(uint8_t *bytes, size_t size,
                                       size_t symbol_table,
                                       uint32_t symbol_count,
                                       size_t symbol_size, int bigobj,
                                       uint32_t section_number) {
    if (symbol_table > size ||
        symbol_count > (size - symbol_table) / symbol_size)
        return;
    for (uint32_t index = 0U; index < symbol_count;) {
        uint8_t *symbol = bytes + symbol_table + (size_t) index * symbol_size;
        size_t type_offset = bigobj ? 16U : 14U;
        uint8_t storage_class = symbol[type_offset + 2U];
        uint8_t aux_count = symbol[type_offset + 3U];
        if (aux_count > symbol_count - index - 1U) return;
        uint32_t symbol_section = bigobj ? read_u32(symbol + 12U)
                                         : read_u16(symbol + 12U);
        if (storage_class == 3U && aux_count != 0U &&
            symbol_section == section_number)
            memset(symbol + symbol_size + 8U, 0, 4U);
        index += 1U + aux_count;
    }
}

static void strip_debug_sections(uint8_t *bytes, size_t size) {
    static const uint8_t bigobj_magic[16] = {
            0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b,
            0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8,
    };
    size_t table = 0U;
    size_t string_table = 0U;
    size_t symbol_table = 0U;
    size_t symbol_size = 0U;
    uint32_t section_count = 0U;
    uint32_t symbol_count = 0U;
    int bigobj = 0;
    if (size >= COFF_HEADER_SIZE && read_u16(bytes) == 0x8664U) {
        section_count = read_u16(bytes + 2U);
        table = COFF_HEADER_SIZE + read_u16(bytes + 16U);
        symbol_table = read_u32(bytes + 8U);
        symbol_count = read_u32(bytes + 12U);
        symbol_size = 18U;
        if (symbol_count <= (SIZE_MAX - symbol_table) / symbol_size)
            string_table =
                    symbol_table + (size_t) symbol_count * symbol_size;
    } else if (size >= COFF_BIGOBJ_HEADER_SIZE && read_u16(bytes) == 0U &&
               read_u16(bytes + 2U) == 0xffffU &&
               memcmp(bytes + 12U, bigobj_magic, sizeof(bigobj_magic)) ==
                       0) {
        bigobj = 1;
        section_count = read_u32(bytes + 44U);
        table = COFF_BIGOBJ_HEADER_SIZE;
        symbol_table = read_u32(bytes + 48U);
        symbol_count = read_u32(bytes + 52U);
        symbol_size = 20U;
        if (symbol_count <= (SIZE_MAX - symbol_table) / symbol_size)
            string_table =
                    symbol_table + (size_t) symbol_count * symbol_size;
    } else {
        return;
    }
    if (section_count > (SIZE_MAX - table) / COFF_SECTION_HEADER_SIZE ||
        table + (size_t) section_count * COFF_SECTION_HEADER_SIZE > size)
        return;
    for (uint32_t i = 0U; i < section_count; i++) {
        uint8_t *section =
                bytes + table + (size_t) i * COFF_SECTION_HEADER_SIZE;
        const char *name =
                coff_section_name(section, bytes, size, string_table);
        if (!name) continue;
        int codeview = strcmp(name, ".debug$S") == 0 ||
                       strcmp(name, ".debug$T") == 0;
        int dwarf = strncmp(name, ".debug", 6U) == 0 ||
                    strncmp(name, ".zdebug", 7U) == 0;
        if (!codeview && (keep_dwarf || !dwarf))
            continue;
        uint32_t raw_size = read_u32(section + 16U);
        uint32_t raw_offset = read_u32(section + 20U);
        if (raw_offset <= size && raw_size <= size - raw_offset)
            memset(bytes + raw_offset, 0, raw_size);
        clear_section_aux_checksum(bytes, size, symbol_table, symbol_count,
                                   symbol_size, bigobj, i + 1U);
    }
}

static void normalize_embedded_prefix(uint8_t *bytes, size_t size,
                                      const char *marker,
                                      const char *replacement) {
    size_t marker_length = strlen(marker);
    size_t replacement_length = strlen(replacement);
    if (marker_length > size) return;
    for (size_t offset = 0U; offset <= size - marker_length; offset++) {
        if (memcmp(bytes + offset, marker, marker_length) != 0) continue;
        size_t boundary = offset;
        while (boundary && bytes[boundary - 1U] >= 0x20U &&
               bytes[boundary - 1U] <= 0x7eU &&
               bytes[boundary - 1U] != '"')
            boundary--;
        size_t start = SIZE_MAX;
        for (size_t i = boundary; i < offset; i++) {
            size_t available = offset - i;
            if ((available >= 7U &&
                 memcmp(bytes + i, "/Users/", 7U) == 0) ||
                (available >= 9U &&
                 memcmp(bytes + i, "/private/", 9U) == 0) ||
                (available >= 6U &&
                 memcmp(bytes + i, "/home/", 6U) == 0)) {
                start = i;
                break;
            }
        }
        if (start == SIZE_MAX) continue;
        size_t end = offset + marker_length - 1U;
        size_t length = end - start;
        if (replacement_length > length) continue;
        memset(bytes + start, '_', length);
        memcpy(bytes + start, replacement, replacement_length);
        offset = end;
    }
}

static void normalize_embedded_paths(uint8_t *bytes, size_t size) {
    normalize_embedded_prefix(bytes, size, "/Code/nature/", "nature-src/");
    normalize_embedded_prefix(bytes, size, "nature-libuv-windows-src/",
                              "libuv-src/");
    normalize_embedded_prefix(bytes, size, "nature-zig-cache/",
                              "zig-cache/");
}

static void normalize_name(uint8_t *name, size_t length, size_t ordinal) {
    static const char normalized_prefix[] = "nature-sysroot/";
    if (!absolute_name(name, length) ||
        (length >= sizeof(normalized_prefix) - 1U &&
         memcmp(name, normalized_prefix, sizeof(normalized_prefix) - 1U) ==
                 0))
        return;
    size_t basename = 0U;
    for (size_t i = 0U; i < length; i++)
        if (name[i] == '/' || name[i] == '\\') basename = i + 1U;
    size_t basename_length = length - basename;
    char prefix[64];
    int prefix_length = snprintf(prefix, sizeof(prefix),
                                 "nature-sysroot/%06zu/", ordinal);
    if (prefix_length <= 0 || (size_t) prefix_length > length ||
        basename_length > length - (size_t) prefix_length)
        return;
    uint8_t *saved_basename = malloc(basename_length);
    if (!saved_basename) return;
    memcpy(saved_basename, name + basename, basename_length);
    memset(name, '_', length);
    memcpy(name, prefix, (size_t) prefix_length);
    memcpy(name + length - basename_length, saved_basename, basename_length);
    free(saved_basename);
}

static int normalize_long_names(uint8_t *bytes, size_t size,
                                size_t *ordinal) {
    size_t offset = 0U;
    while (offset < size) {
        size_t end = offset;
        while (end < size && bytes[end] != '\n' && bytes[end] != '\0') end++;
        size_t length = end - offset;
        if (length && bytes[offset + length - 1U] == '/') length--;
        normalize_name(bytes + offset, length, (*ordinal)++);
        if (end == size) break;
        offset = end + 1U;
    }
    return 1;
}

static int normalize_archive(uint8_t *bytes, size_t size, const char *path) {
    static const uint8_t magic[] = "!<arch>\n";
    if (size < sizeof(magic) - 1U ||
        memcmp(bytes, magic, sizeof(magic) - 1U) != 0)
        return fail(path, "not a Unix archive");
    size_t offset = sizeof(magic) - 1U;
    size_t ordinal = 0U;
    while (offset < size) {
        if (size - offset < ARCHIVE_HEADER_SIZE)
            return fail(path, "truncated archive member header");
        uint8_t *header = bytes + offset;
        if (header[58] != '`' || header[59] != '\n')
            return fail(path, "invalid archive member trailer");
        size_t member_size = 0U;
        if (!decimal(header + 48U, 10U, &member_size))
            return fail(path, "invalid archive member size");
        size_t payload = offset + ARCHIVE_HEADER_SIZE;
        if (payload > size || member_size > size - payload)
            return fail(path, "archive member extends past end of file");
        size_t name_length = 16U;
        while (name_length && header[name_length - 1U] == ' ') name_length--;
        int special = (name_length == 1U && header[0] == '/') ||
                      (name_length == 2U && header[0] == '/' &&
                       header[1] == '/') ||
                      (name_length == 7U &&
                       memcmp(header, "/SYM64/", 7U) == 0) ||
                      (name_length >= 9U &&
                       memcmp(header, "__.SYMDEF", 9U) == 0);
        if (name_length == 2U && header[0] == '/' && header[1] == '/') {
            normalize_long_names(bytes + payload, member_size, &ordinal);
        } else if (name_length > 3U && header[0] == '#' &&
                   header[1] == '1' && header[2] == '/') {
            size_t embedded = 0U;
            if (!decimal(header + 3U, name_length - 3U, &embedded) ||
                embedded > member_size)
                return fail(path, "invalid BSD archive member name");
            normalize_name(bytes + payload, embedded, ordinal++);
        }
        size_t object_offset = 0U;
        if (name_length > 3U && header[0] == '#' && header[1] == '1' &&
            header[2] == '/') {
            if (!decimal(header + 3U, name_length - 3U, &object_offset) ||
                object_offset > member_size)
                return fail(path, "invalid BSD archive member name");
        }
        if (!special)
            strip_debug_sections(bytes + payload + object_offset,
                                 member_size - object_offset);
        if (member_size > SIZE_MAX - payload)
            return fail(path, "archive size overflow");
        size_t next = payload + member_size;
        if (next & 1U) next++;
        if (next > size) return fail(path, "invalid archive padding");
        offset = next;
    }
    return 0;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = length ? malloc((size_t) length) : NULL;
    if (length &&
        (!bytes || fread(bytes, 1U, (size_t) length, file) !=
                           (size_t) length)) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    *size = (size_t) length;
    return bytes;
}

static int write_file(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int result = fwrite(bytes, 1U, size, file) == size;
    if (fclose(file) != 0) result = 0;
    return result;
}

int main(int argc, char **argv) {
    int input_index = 1;
    if (argc == 4 && strcmp(argv[1], "--keep-dwarf") == 0) {
        keep_dwarf = 1;
        input_index = 2;
    }
    if (argc - input_index != 2) {
        fprintf(stderr,
                "usage: archive-normalize [--keep-dwarf] INPUT OUTPUT\n");
        return 2;
    }
    size_t size = 0U;
    const char *input = argv[input_index];
    const char *output = argv[input_index + 1];
    uint8_t *bytes = read_file(input, &size);
    if (!bytes) return fail(input, strerror(errno));
    static const uint8_t archive_magic[] = "!<arch>\n";
    int status = size >= sizeof(archive_magic) - 1U &&
                         memcmp(bytes, archive_magic,
                                sizeof(archive_magic) - 1U) == 0
                         ? normalize_archive(bytes, size, input)
                         : (strip_debug_sections(bytes, size), 0);
    normalize_embedded_paths(bytes, size);
    if (status == 0 && !write_file(output, bytes, size))
        status = fail(output, strerror(errno));
    free(bytes);
    return status;
}

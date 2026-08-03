#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#endif

static void test_elf_set_binary_mode(int fd) {
#ifdef _WIN32
    assert(_setmode(fd, _O_BINARY) != -1);
#else
    (void) fd;
#endif
}

uint64_t test_elf_add_signed_u32(uint64_t base, uint32_t encoded) {
    if (encoded <= INT32_MAX) return base + encoded;
    uint64_t magnitude = (uint64_t) UINT32_MAX - encoded + 1U;
    assert(magnitude <= base);
    return base - magnitude;
}

void capture_diagnostic(void *context, ld_diag_level_t level, const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    capture->count++;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

void write_fixture(char path[], const void *bytes, size_t size) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    test_elf_set_binary_mode(fd);
    assert(write(fd, bytes, size) == (ssize_t) size);
    assert(close(fd) == 0);
}

void test_elf_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

void test_elf_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

void test_elf_write_u64(uint8_t *bytes, uint64_t value) {
    test_elf_write_u32(bytes, (uint32_t) value);
    test_elf_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

uint16_t test_elf_read_u16(const uint8_t *bytes) {
    return (uint16_t) ((uint16_t) bytes[0] |
                       (uint16_t) ((uint16_t) bytes[1] << 8U));
}

uint32_t test_elf_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) |
           ((uint32_t) bytes[3] << 24U);
}

uint64_t test_elf_read_u64(const uint8_t *bytes) {
    return (uint64_t) test_elf_read_u32(bytes) |
           ((uint64_t) test_elf_read_u32(bytes + 4U) << 32U);
}

const uint8_t *test_elf_find_output_section(const uint8_t *image,
                                            size_t image_size,
                                            const char *name) {
    if (!image || !name || image_size < LD_ELF64_EHDR_SIZE) return NULL;
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t entry_size = test_elf_read_u16(image + 58U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    uint16_t names_index = test_elf_read_u16(image + 62U);
    if (entry_size != LD_ELF64_SHDR_SIZE || names_index >= section_count ||
        table_offset > image_size ||
        (uint64_t) section_count * entry_size > image_size - table_offset) {
        return NULL;
    }
    const uint8_t *table = image + (size_t) table_offset;
    const uint8_t *names_header =
            table + (size_t) names_index * entry_size;
    uint64_t names_offset = test_elf_read_u64(names_header + 24U);
    uint64_t names_size = test_elf_read_u64(names_header + 32U);
    if (names_offset > image_size || names_size > image_size - names_offset)
        return NULL;
    const char *names = (const char *) image + (size_t) names_offset;
    for (uint16_t i = 1U; i < section_count; i++) {
        const uint8_t *section = table + (size_t) i * entry_size;
        uint32_t name_offset = test_elf_read_u32(section);
        if (name_offset >= names_size ||
            !memchr(names + name_offset, '\0',
                    (size_t) names_size - name_offset)) {
            return NULL;
        }
        if (strcmp(names + name_offset, name) == 0) return section;
    }
    return NULL;
}

const uint8_t *test_elf_find_program_header(const uint8_t *image,
                                            size_t image_size,
                                            uint32_t type,
                                            size_t *match_count) {
    if (match_count) *match_count = 0U;
    if (!image || image_size < LD_ELF64_EHDR_SIZE) return NULL;
    uint64_t table_offset = test_elf_read_u64(image + 32U);
    uint16_t entry_size = test_elf_read_u16(image + 54U);
    uint16_t count = test_elf_read_u16(image + 56U);
    if (entry_size != LD_ELF64_PHDR_SIZE || table_offset > image_size ||
        (uint64_t) count * entry_size > image_size - table_offset) {
        return NULL;
    }
    const uint8_t *first = NULL;
    size_t matches = 0U;
    for (uint16_t i = 0U; i < count; i++) {
        const uint8_t *program =
                image + (size_t) table_offset + (size_t) i * entry_size;
        if (test_elf_read_u32(program) != type) continue;
        if (!first) first = program;
        matches++;
    }
    if (match_count) *match_count = matches;
    return first;
}

size_t test_elf_align(size_t value, size_t alignment) {
    assert(alignment != 0U && (alignment & (alignment - 1U)) == 0U);
    assert(value <= SIZE_MAX - (alignment - 1U));
    return (value + alignment - 1U) & ~(alignment - 1U);
}

uint32_t test_elf_append_name(char *table, size_t capacity,
                              size_t *cursor, const char *name) {
    assert(table != NULL && cursor != NULL && name != NULL);
    size_t length = strlen(name) + 1U;
    assert(*cursor <= capacity && length <= capacity - *cursor);
    assert(*cursor <= UINT32_MAX);
    uint32_t result = (uint32_t) *cursor;
    memcpy(table + *cursor, name, length);
    *cursor += length;
    return result;
}

void test_elf_write_section(uint8_t *bytes, uint32_t name,
                            uint32_t type, uint64_t flags,
                            uint64_t offset, uint64_t size,
                            uint32_t link, uint32_t info,
                            uint64_t alignment, uint64_t entry_size) {
    memset(bytes, 0, LD_ELF64_SHDR_SIZE);
    test_elf_write_u32(bytes, name);
    test_elf_write_u32(bytes + 4U, type);
    test_elf_write_u64(bytes + 8U, flags);
    test_elf_write_u64(bytes + 24U, offset);
    test_elf_write_u64(bytes + 32U, size);
    test_elf_write_u32(bytes + 40U, link);
    test_elf_write_u32(bytes + 44U, info);
    test_elf_write_u64(bytes + 48U, alignment);
    test_elf_write_u64(bytes + 56U, entry_size);
}

uint8_t *read_test_fixture(const char *path, size_t *result_size,
                           mode_t *result_mode) {
    int fd = open(path, O_RDONLY | O_BINARY);
    assert(fd >= 0);
    struct stat status;
    assert(fstat(fd, &status) == 0 && status.st_size >= 0);
    size_t size = (size_t) status.st_size;
    uint8_t *bytes = malloc(size ? size : 1U);
    assert(bytes != NULL);
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = read(fd, bytes + offset, size - offset);
        assert(count > 0);
        offset += (size_t) count;
    }
    assert(close(fd) == 0);
    *result_size = size;
    if (result_mode) *result_mode = status.st_mode;
    return bytes;
}

void test_elf_assert_executable_mode(mode_t mode) {
#ifdef __WINDOWS
    (void) mode;
#else
    assert((mode & 0777U) == 0755U);
#endif
}

uint32_t read_test_elf_entry_word(const char *path) {
    size_t image_size;
    uint8_t *image = read_test_fixture(path, &image_size, NULL);
    assert(image_size >= LD_ELF64_EHDR_SIZE);
    uint64_t entry = test_elf_read_u64(image + 24U);
    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    assert(program_offset <= image_size);

    bool found = false;
    uint32_t result = 0U;
    for (uint16_t i = 0; i < program_count; i++) {
        uint64_t header_offset =
                program_offset + (uint64_t) i * LD_ELF64_PHDR_SIZE;
        assert(header_offset <= image_size &&
               LD_ELF64_PHDR_SIZE <= image_size - header_offset);
        const uint8_t *program = image + (size_t) header_offset;
        if (test_elf_read_u32(program) != LD_ELF_PT_LOAD) continue;
        uint64_t file_offset = test_elf_read_u64(program + 8U);
        uint64_t address = test_elf_read_u64(program + 16U);
        uint64_t file_size = test_elf_read_u64(program + 32U);
        if (entry < address || entry - address >= file_size) continue;
        uint64_t entry_offset = file_offset + entry - address;
        assert(entry_offset <= image_size && 4U <= image_size - entry_offset);
        result = test_elf_read_u32(image + (size_t) entry_offset);
        found = true;
        break;
    }
    free(image);
    assert(found);
    return result;
}

bool test_elf_file_contains_u32(const char *path, uint32_t value) {
    size_t size;
    uint8_t *bytes = read_test_fixture(path, &size, NULL);
    bool found = false;
    for (size_t i = 0; i + sizeof(uint32_t) <= size; i++) {
        if (test_elf_read_u32(bytes + i) == value) {
            found = true;
            break;
        }
    }
    free(bytes);
    return found;
}

int link_test_elf_inputs(const char *output_path,
                         const char *const *input_paths,
                         size_t input_count,
                         diagnostic_capture_t *capture) {
    return link_test_elf_inputs_configured(
            output_path, input_paths, input_count, LD_ARCH_ARM64, false,
            LD_DEBUG_NONE, true, capture);
}

int link_test_elf_inputs_configured(
        const char *output_path, const char *const *input_paths,
        size_t input_count, ld_arch_t arch, bool pie,
        ld_debug_mode_t debug_mode, bool remove_existing_output,
        diagnostic_capture_t *capture) {
    if (remove_existing_output) unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = arch;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = pie;
    options.debug_mode = debug_mode;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    for (size_t i = 0; i < input_count; i++)
        assert(ld_add_input(&options, input_paths[i]) == LD_OK);
    int status = ld_link(&options);
    ld_options_deinit(&options);
    return status;
}

void test_archive_append(uint8_t *archive, size_t capacity,
                         size_t *cursor, const char *name,
                         const uint8_t *payload, size_t payload_size) {
    size_t padded_size = payload_size + (payload_size & 1U);
    assert(strlen(name) <= LD_ELF_AR_NAME_SIZE);
    assert(*cursor <= capacity &&
           LD_ELF_AR_HEADER_SIZE + padded_size <= capacity - *cursor);
    uint8_t *header = archive + *cursor;
    memset(header, ' ', LD_ELF_AR_HEADER_SIZE);
    memcpy(header, name, strlen(name));
    char decimal[32];
    int decimal_length =
            snprintf(decimal, sizeof(decimal), "%zu", payload_size);
    assert(decimal_length > 0 && decimal_length <= 10);
    memcpy(header + 48U, decimal, (size_t) decimal_length);
    memcpy(header + 58U, "`\n", 2U);
    memcpy(header + LD_ELF_AR_HEADER_SIZE, payload, payload_size);
    if (payload_size & 1U) {
        header[LD_ELF_AR_HEADER_SIZE + payload_size] = '\n';
    }
    *cursor += LD_ELF_AR_HEADER_SIZE + padded_size;
}

uint8_t *make_test_elf_object(uint16_t machine, bool with_sht_rel,
                              bool executable_stack, size_t *result_size) {
    static const char symbol_names[] = "\0_start";
    static const char section_names[] =
            "\0.text\0.symtab\0.strtab\0.shstrtab\0.rel.text"
            "\0.note.GNU-stack";
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t symbol_names_offset = text_offset + sizeof(uint32_t);
    const size_t section_names_offset =
            symbol_names_offset + sizeof(symbol_names);
    const size_t symbols_offset = test_elf_align(
            section_names_offset + sizeof(section_names), 8U);
    const size_t symbol_count = 2U;
    const size_t section_table_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const uint16_t section_count =
            (uint16_t) (5U + (with_sht_rel ? 1U : 0U) +
                        (executable_stack ? 1U : 0U));
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, machine);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 4U);

    test_elf_write_u32(bytes + text_offset, 0xd65f03c0U);
    memcpy(bytes + symbol_names_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           sizeof(section_names));

    uint8_t *start_symbol = bytes + symbols_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start_symbol, 1U);
    start_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start_symbol + 6U, 1U);
    test_elf_write_u64(start_symbol + 16U, sizeof(uint32_t));

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, 1U,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, 7U,
                           LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
                           symbol_count * LD_ELF64_SYM_SIZE, 3U, 1U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, 15U,
                           LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE, 23U,
                           LD_ELF_SHT_STRTAB, 0U, section_names_offset,
                           sizeof(section_names), 0U, 0U, 1U, 0U);
    if (with_sht_rel) {
        test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE, 33U,
                               LD_ELF_SHT_REL, 0U, section_table_offset, 0U,
                               2U, 1U, 8U, 16U);
    }
    if (executable_stack) {
        size_t note_index = 5U + (with_sht_rel ? 1U : 0U);
        uint32_t note_name =
                (uint32_t) (sizeof(section_names) -
                            sizeof(".note.GNU-stack"));
        test_elf_write_section(
                sections + note_index * LD_ELF64_SHDR_SIZE, note_name,
                LD_ELF_SHT_PROGBITS, LD_ELF_SHF_EXECINSTR, 0U, 0U, 0U, 0U,
                1U, 0U);
    }

    *result_size = size;
    return bytes;
}

uint8_t *make_test_elf_weak_dynamic_object(size_t *result_size) {
    static const char symbol_names[] = "\0_start\0_DYNAMIC";
    static const char section_names[] =
            "\0.text\0.symtab\0.strtab\0.shstrtab\0.rela.text";
    const uint32_t text_name = 1U;
    const uint32_t symtab_name = 7U;
    const uint32_t strtab_name = 15U;
    const uint32_t shstrtab_name = 23U;
    const uint32_t rela_name = 33U;
    const uint32_t start_name = 1U;
    const uint32_t dynamic_name = 8U;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t symtab_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t strtab_offset =
            symtab_offset + 3U * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t rela_offset =
            test_elf_align(shstrtab_offset + sizeof(section_names), 8U);
    const size_t section_table_offset = rela_offset + LD_ELF64_RELA_SIZE;
    const uint16_t section_count = 6U;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_X86_64);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 4U);

    memcpy(bytes + text_offset, "\0\0\0\0", 4U);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, sizeof(section_names));

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *start_symbol = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start_symbol, start_name);
    start_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start_symbol + 6U, 1U);
    test_elf_write_u64(start_symbol + 16U, 4U);
    uint8_t *dynamic_symbol = start_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(dynamic_symbol, dynamic_name);
    dynamic_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_WEAK, LD_ELF_STT_OBJECT);
    test_elf_write_u16(dynamic_symbol + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *rela = bytes + rela_offset;
    test_elf_write_u64(rela, 0U);
    test_elf_write_u64(
            rela + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_X86_64_PC32));
    test_elf_write_u64(rela + 16U, UINT64_MAX - 3U); /* signed addend -4 */

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, 4U, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, symtab_name,
                           LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
                           3U * LD_ELF64_SYM_SIZE, 3U, 1U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, strtab_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE, shstrtab_name,
                           LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
                           sizeof(section_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE, rela_name,
                           LD_ELF_SHT_RELA, 0U, rela_offset,
                           LD_ELF64_RELA_SIZE, 2U, 1U, 8U,
                           LD_ELF64_RELA_SIZE);

    *result_size = size;
    return bytes;
}

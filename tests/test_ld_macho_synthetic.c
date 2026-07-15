#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *segname;
    const char *sectname;
    uint32_t flags;
    uint32_t align;
    const uint8_t *data;
    size_t size;
    const uint32_t *relocations;
    uint32_t relocation_count;
} test_macho_section_fixture_t;

typedef struct {
    uint8_t *bytes;
    size_t size;
    const ld_mach_header_64_t *header;
    const ld_section_64_t *got;
    const ld_section_64_t *stubs;
    const ld_section_64_t *thread_ptrs;
    const ld_section_64_t *text;
    const ld_section_64_t *data;
    const ld_section_64_t *thread_vars;
    const ld_symtab_command_t *symtab;
    const ld_dysymtab_command_t *dysymtab;
    const ld_dyld_info_command_t *dyld_info;
} test_macho_output_t;

static uint32_t test_arm64_relocation_word(uint32_t symbol, bool pcrel,
                                           uint8_t type) {
    return symbol | ((uint32_t) pcrel << 24U) | (2U << 25U) |
           (1U << 27U) | ((uint32_t) type << 28U);
}

static void test_copy_macho_name(char destination[16], const char *source) {
    size_t length = strlen(source);
    assert(length <= 16U);
    memcpy(destination, source, length);
}

static void test_make_macho_object(
        char path[], const test_macho_section_fixture_t *sections,
        size_t section_count, const ld_nlist_64_t *symbols,
        size_t symbol_count, const char *strings, size_t strings_size) {
    size_t segment_size = sizeof(ld_segment_command_64_t) +
                          section_count * sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t) +
                           sizeof(ld_build_version_command_t);
    size_t cursor = sizeof(ld_mach_header_64_t) + commands_size;
    size_t content_size = 0;
    size_t relocation_size = 0;
    for (size_t i = 0; i < section_count; i++) {
        content_size += sections[i].size;
        relocation_size += (size_t) sections[i].relocation_count * 8U;
    }
    size_t symbol_offset = cursor + content_size + relocation_size;
    size_t strings_offset =
            symbol_offset + symbol_count * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + strings_size;
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 3U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    segment.vmsize = content_size;
    segment.fileoff = cursor;
    segment.filesize = content_size;
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_WRITE |
                      LD_VM_PROT_EXECUTE;
    segment.initprot = segment.maxprot;
    segment.nsects = (uint32_t) section_count;
    size_t command_cursor = sizeof(header);
    memcpy(object + command_cursor, &segment, sizeof(segment));

    size_t content_cursor = cursor;
    size_t relocation_cursor = cursor + content_size;
    for (size_t i = 0; i < section_count; i++) {
        ld_section_64_t section = {0};
        test_copy_macho_name(section.segname, sections[i].segname);
        test_copy_macho_name(section.sectname, sections[i].sectname);
        section.size = sections[i].size;
        section.offset = (uint32_t) content_cursor;
        section.align = sections[i].align;
        section.reloff = (uint32_t) relocation_cursor;
        section.nreloc = sections[i].relocation_count;
        section.flags = sections[i].flags;
        memcpy(object + command_cursor + sizeof(segment) +
                       i * sizeof(section),
               &section, sizeof(section));
        if (sections[i].size) {
            memcpy(object + content_cursor, sections[i].data,
                   sections[i].size);
        }
        if (sections[i].relocation_count) {
            size_t bytes = (size_t) sections[i].relocation_count * 8U;
            memcpy(object + relocation_cursor, sections[i].relocations,
                   bytes);
            relocation_cursor += bytes;
        }
        content_cursor += sections[i].size;
    }

    command_cursor += segment_size;
    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = (uint32_t) symbol_count;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = (uint32_t) strings_size;
    memcpy(object + command_cursor, &symtab, sizeof(symtab));
    command_cursor += sizeof(symtab);

    ld_build_version_command_t build_version = {0};
    build_version.cmd = LD_LC_BUILD_VERSION;
    build_version.cmdsize = sizeof(build_version);
    build_version.platform = LD_PLATFORM_MACOS;
    build_version.minos = (11U << 16U);
    build_version.sdk = (14U << 16U);
    memcpy(object + command_cursor, &build_version, sizeof(build_version));

    memcpy(object + symbol_offset, symbols,
           symbol_count * sizeof(ld_nlist_64_t));
    memcpy(object + strings_offset, strings, strings_size);
    test_ld_write_fixture(path, object, object_size);
    free(object);
}

static test_macho_output_t test_read_macho_output(const char *path) {
    test_macho_output_t output = {0};
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    output.size = (size_t) st.st_size;
    output.bytes = malloc(output.size);
    assert(output.bytes != NULL);
    assert(read(fd, output.bytes, output.size) == st.st_size);
    assert(close(fd) == 0);
    output.header = (const ld_mach_header_64_t *) output.bytes;
    assert(output.header->magic == LD_MH_MAGIC_64);

    size_t cursor = sizeof(*output.header);
    for (uint32_t i = 0; i < output.header->ncmds; i++) {
        assert(cursor + sizeof(ld_load_command_t) <= output.size);
        const ld_load_command_t *command =
                (const ld_load_command_t *) (output.bytes + cursor);
        assert(command->cmdsize >= sizeof(*command));
        assert(cursor + command->cmdsize <= output.size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (segment + 1);
            for (uint32_t j = 0; j < segment->nsects; j++) {
                const ld_section_64_t *section = &sections[j];
                if (strncmp(section->sectname, "__got", 16) == 0) {
                    output.got = section;
                } else if (strncmp(section->sectname, "__stubs", 16) == 0) {
                    output.stubs = section;
                } else if (strncmp(section->sectname, "__thread_ptrs", 16) ==
                           0) {
                    output.thread_ptrs = section;
                } else if (strncmp(section->sectname, "__text", 16) == 0) {
                    output.text = section;
                } else if (strncmp(section->sectname, "__data", 16) == 0) {
                    output.data = section;
                } else if (strncmp(section->sectname, "__thread_vars", 16) ==
                           0) {
                    output.thread_vars = section;
                }
            }
        } else if (command->cmd == LD_LC_DYSYMTAB) {
            output.dysymtab = (const ld_dysymtab_command_t *) command;
        } else if (command->cmd == LD_LC_SYMTAB) {
            output.symtab = (const ld_symtab_command_t *) command;
        } else if (command->cmd == LD_LC_DYLD_INFO_ONLY) {
            output.dyld_info = (const ld_dyld_info_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    return output;
}

static const ld_nlist_64_t *test_find_output_symbol(
        const test_macho_output_t *output, const char *name) {
    assert(output->symtab != NULL);
    assert(output->symtab->symoff <= output->size &&
           (size_t) output->symtab->nsyms * sizeof(ld_nlist_64_t) <=
                   output->size - output->symtab->symoff);
    assert(output->symtab->stroff <= output->size &&
           output->symtab->strsize <=
                   output->size - output->symtab->stroff);
    const ld_nlist_64_t *symbols = (const ld_nlist_64_t *) (output->bytes + output->symtab->symoff);
    const char *strings =
            (const char *) output->bytes + output->symtab->stroff;
    for (uint32_t i = 0; i < output->symtab->nsyms; i++) {
        assert(symbols[i].n_strx < output->symtab->strsize);
        const char *candidate = strings + symbols[i].n_strx;
        assert(memchr(candidate, '\0',
                      output->symtab->strsize - symbols[i].n_strx) != NULL);
        if (strcmp(candidate, name) == 0) return &symbols[i];
    }
    return NULL;
}

static size_t test_count_bind_symbol(const test_macho_output_t *output,
                                     uint32_t offset, uint32_t size,
                                     const char *name) {
    assert(offset <= output->size && size <= output->size - offset);
    size_t wanted_size = strlen(name) + 1U;
    size_t count = 0;
    const uint8_t *stream = output->bytes + offset;
    for (size_t i = 0; i + wanted_size <= size; i++) {
        if (memcmp(stream + i, name, wanted_size) == 0) count++;
    }
    return count;
}

static void test_make_weak_tlv_dylib(char path[]) {
    static const char install_name[] = "/usr/lib/libWeakTlv.dylib";
    static const uint8_t export_trie[] = {
            0x00U,
            0x01U,
            '_',
            'i',
            'm',
            'p',
            'o',
            'r',
            't',
            '_',
            't',
            'l',
            's',
            0x00U,
            0x0fU,
            0x02U,
            LD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL |
                    LD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION,
            0x00U,
            0x00U,
    };
    uint32_t id_size = (uint32_t) ((sizeof(ld_dylib_command_t) +
                                    sizeof(install_name) + 7U) &
                                   ~7U);
    uint32_t commands_size = id_size + sizeof(ld_linkedit_data_command_t);
    uint32_t trie_offset =
            (uint32_t) sizeof(ld_mach_header_64_t) + commands_size;
    size_t image_size = trie_offset + sizeof(export_trie);
    uint8_t *image = calloc(1, image_size);
    assert(image != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    header.ncmds = 2U;
    header.sizeofcmds = commands_size;
    memcpy(image, &header, sizeof(header));

    size_t cursor = sizeof(header);
    ld_dylib_command_t id = {0};
    id.cmd = LD_LC_ID_DYLIB;
    id.cmdsize = id_size;
    id.name_offset = sizeof(id);
    memcpy(image + cursor, &id, sizeof(id));
    memcpy(image + cursor + sizeof(id), install_name, sizeof(install_name));
    cursor += id_size;

    ld_linkedit_data_command_t exports = {0};
    exports.cmd = LD_LC_DYLD_EXPORTS_TRIE;
    exports.cmdsize = sizeof(exports);
    exports.dataoff = trie_offset;
    exports.datasize = sizeof(export_trie);
    memcpy(image + cursor, &exports, sizeof(exports));
    memcpy(image + trie_offset, export_trie, sizeof(export_trie));
    test_ld_write_fixture(path, image, image_size);
    free(image);
}

static bool test_read_uleb(const uint8_t *data, size_t size, size_t *offset,
                           uint64_t *value) {
    uint64_t result = 0;
    for (unsigned shift = 0; shift < 64U && *offset < size; shift += 7U) {
        uint8_t byte = data[(*offset)++];
        if (shift == 63U && (byte & 0x7eU)) return false;
        result |= (uint64_t) (byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

static bool test_export_flags_at(const uint8_t *trie, size_t trie_size,
                                 size_t node_offset, char name[256],
                                 size_t name_length, const char *wanted,
                                 uint64_t *flags, unsigned depth) {
    assert(depth < 256U && node_offset < trie_size);
    size_t cursor = node_offset;
    uint64_t terminal_size;
    assert(test_read_uleb(trie, trie_size, &cursor, &terminal_size));
    assert(terminal_size <= trie_size - cursor);
    size_t terminal_end = cursor + (size_t) terminal_size;
    if (terminal_size && strcmp(name, wanted) == 0) {
        return test_read_uleb(trie, terminal_end, &cursor, flags);
    }
    cursor = terminal_end;
    assert(cursor < trie_size);
    uint8_t child_count = trie[cursor++];
    for (uint8_t child = 0; child < child_count; child++) {
        const char *edge = (const char *) trie + cursor;
        const char *terminator =
                memchr(edge, '\0', trie_size - cursor);
        assert(terminator != NULL);
        size_t edge_length = (size_t) (terminator - edge);
        assert(edge_length < sizeof(char[256]) - name_length);
        cursor += edge_length + 1U;
        uint64_t child_offset;
        assert(test_read_uleb(trie, trie_size, &cursor, &child_offset));
        memcpy(name + name_length, edge, edge_length);
        name[name_length + edge_length] = '\0';
        if (test_export_flags_at(trie, trie_size, (size_t) child_offset,
                                 name, name_length + edge_length, wanted,
                                 flags, depth + 1U)) {
            return true;
        }
        name[name_length] = '\0';
    }
    return false;
}

static uint64_t test_export_flags(const test_macho_output_t *output,
                                  const char *name) {
    assert(output->dyld_info && output->dyld_info->export_size);
    assert(output->dyld_info->export_off + output->dyld_info->export_size <=
           output->size);
    char current[256] = {0};
    uint64_t flags = UINT64_MAX;
    const uint8_t *trie = output->bytes + output->dyld_info->export_off;
    assert(test_export_flags_at(trie, output->dyld_info->export_size, 0,
                                current, 0, name, &flags, 0));
    return flags;
}

static uint64_t test_arm64_adrp_page(uint32_t instruction, uint64_t place) {
    int64_t immediate =
            (int64_t) (((instruction >> 5U) & 0x7ffffU) << 2U) |
            (int64_t) ((instruction >> 29U) & 3U);
    if (immediate & (1LL << 20U)) immediate -= 1LL << 21U;
    return (uint64_t) ((int64_t) (place & ~0xfffULL) +
                       immediate * 0x1000LL);
}

static uint64_t test_arm64_page_pair_target(uint32_t adrp, uint32_t pageoff,
                                            uint64_t place,
                                            bool load) {
    uint64_t page = test_arm64_adrp_page(adrp, place);
    uint64_t immediate = (pageoff >> 10U) & 0xfffU;
    return page + (load ? immediate * 8U : immediate);
}

static void test_link_macho_fixture(const char *object_path,
                                    const char *dylib_path,
                                    const char *output_path, int expected,
                                    test_ld_diagnostic_capture_t *capture) {
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    if (capture) {
        options.diagnostic = test_ld_capture_diagnostic;
        options.diagnostic_context = capture;
    }
    assert(ld_add_input(&options, object_path) == LD_OK);
    if (dylib_path) assert(ld_add_input(&options, dylib_path) == LD_OK);
    assert(ld_link(&options) == expected);
    ld_options_deinit(&options);
}

static void test_local_got_and_indirect_offsets(void) {
    static const uint32_t text_words[] = {
            0x94000000U,
            0x90000000U,
            0xf9400000U,
            0U,
            0xd65f03c0U,
    };
    static const uint64_t data_word = 0x123456789abcdef0ULL;
    uint32_t relocations[] = {
            0U,
            test_arm64_relocation_word(2U, true,
                                       LD_ARM64_RELOC_BRANCH26),
            4U,
            test_arm64_relocation_word(1U, true,
                                       LD_ARM64_RELOC_GOT_LOAD_PAGE21),
            8U,
            test_arm64_relocation_word(1U, false,
                                       LD_ARM64_RELOC_GOT_LOAD_PAGEOFF12),
            12U,
            test_arm64_relocation_word(1U, true,
                                       LD_ARM64_RELOC_POINTER_TO_GOT),
    };
    test_macho_section_fixture_t sections[] = {
            {
                    .segname = "__TEXT",
                    .sectname = "__text",
                    .flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                             LD_S_ATTR_SOME_INSTRUCTIONS,
                    .align = 2U,
                    .data = (const uint8_t *) text_words,
                    .size = sizeof(text_words),
                    .relocations = relocations,
                    .relocation_count = 4U,
            },
            {
                    .segname = "__DATA",
                    .sectname = "__data",
                    .align = 3U,
                    .data = (const uint8_t *) &data_word,
                    .size = sizeof(data_word),
            },
    };
    static const char strings[] = "\0_main\0_local_data\0_imported\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_SECT, .n_sect = 2U},
            {.n_strx = 19U, .n_type = LD_N_UNDF | LD_N_EXT},
    };
    char object_path[] = "/tmp/nature-ld-synthetic-got-XXXXXX";
    test_make_macho_object(object_path, sections, 2U, symbols, 3U, strings,
                           sizeof(strings));

    static const char tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: /usr/lib/libSynthetic.dylib\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    symbols: [ _imported ]\n"
            "...\n";
    char tbd_path[] = "/tmp/nature-ld-synthetic-tbd-XXXXXX";
    test_ld_write_fixture(tbd_path, tbd, sizeof(tbd) - 1U);
    char output_path[] = "/tmp/nature-ld-synthetic-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_macho_fixture(object_path, tbd_path, output_path, LD_OK, NULL);

    test_macho_output_t output = test_read_macho_output(output_path);
    assert(output.stubs && output.stubs->size == 12U);
    assert(output.got && output.got->size == 16U);
    assert(output.stubs->reserved1 == 0U);
    assert(output.got->reserved1 == 1U);
    assert(output.dysymtab && output.dysymtab->nindirectsyms == 3U);
    const uint32_t *indirect = (const uint32_t *) (output.bytes + output.dysymtab->indirectsymoff);
    assert(indirect[0] == indirect[1]);
    assert(indirect[2] != indirect[0]);

    uint64_t local_slot;
    memcpy(&local_slot, output.bytes + output.got->offset + 8U,
           sizeof(local_slot));
    assert(output.data && local_slot == output.data->addr);
    uint32_t adrp, ldr;
    memcpy(&adrp, output.bytes + output.text->offset + 4U, sizeof(adrp));
    memcpy(&ldr, output.bytes + output.text->offset + 8U, sizeof(ldr));
    assert(test_arm64_page_pair_target(adrp, ldr, output.text->addr + 4U,
                                       true) == output.got->addr + 8U);
    int32_t pointer_delta;
    memcpy(&pointer_delta, output.bytes + output.text->offset + 12U,
           sizeof(pointer_delta));
    assert((uint64_t) ((int64_t) output.text->addr + 12LL + pointer_delta) ==
           output.got->addr + 8U);

    free(output.bytes);
    unlink(object_path);
    unlink(tbd_path);
    unlink(output_path);
}

static void test_weak_dylib_provider_direct_and_got_fixups(void) {
    static const uint32_t text_words[] = {
            0x90000000U,
            0xf9400000U,
            0xd65f03c0U,
    };
    static const uint64_t data_word = 0U;
    static const uint32_t text_relocations[] = {
            0U,
            1U | (1U << 24U) | (2U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_GOT_LOAD_PAGE21 << 28U),
            4U,
            1U | (2U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 << 28U),
    };
    static const uint32_t data_relocation[] = {
            0U,
            1U | (3U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_UNSIGNED << 28U),
    };
    const test_macho_section_fixture_t sections[] = {
            {
                    .segname = "__TEXT",
                    .sectname = "__text",
                    .flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                             LD_S_ATTR_SOME_INSTRUCTIONS,
                    .align = 2U,
                    .data = (const uint8_t *) text_words,
                    .size = sizeof(text_words),
                    .relocations = text_relocations,
                    .relocation_count = 2U,
            },
            {
                    .segname = "__DATA",
                    .sectname = "__data",
                    .align = 3U,
                    .data = (const uint8_t *) &data_word,
                    .size = sizeof(data_word),
                    .relocations = data_relocation,
                    .relocation_count = 1U,
            },
    };
    static const char strings[] = "\0_main\0_weak_data\0";
    const ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_UNDF | LD_N_EXT},
    };
    char object_path[] = "/tmp/nature-ld-weak-provider-object-XXXXXX";
    test_make_macho_object(object_path, sections, 2U, symbols, 2U,
                           strings, sizeof(strings));

    static const char tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: /usr/lib/libWeakProvider.dylib\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    weak-symbols: [ _weak_data ]\n"
            "...\n";
    char tbd_path[] = "/tmp/nature-ld-weak-provider-tbd-XXXXXX";
    test_ld_write_fixture(tbd_path, tbd, sizeof(tbd) - 1U);
    char output_path[] = "/tmp/nature-ld-weak-provider-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_macho_fixture(object_path, tbd_path, output_path, LD_OK, NULL);

    test_macho_output_t output = test_read_macho_output(output_path);
    assert(output.got && output.got->size == sizeof(uint64_t));
    assert(output.dyld_info && output.dyld_info->bind_size > 1U &&
           output.dyld_info->weak_bind_size > 1U);
    assert(test_count_bind_symbol(&output, output.dyld_info->bind_off,
                                  output.dyld_info->bind_size,
                                  "_weak_data") == 2U);
    assert(test_count_bind_symbol(&output, output.dyld_info->weak_bind_off,
                                  output.dyld_info->weak_bind_size,
                                  "_weak_data") == 2U);
    const ld_nlist_64_t *weak =
            test_find_output_symbol(&output, "_weak_data");
    assert(weak != NULL && (weak->n_type & LD_N_TYPE) == LD_N_UNDF);
    assert((weak->n_desc & LD_N_REF_TO_WEAK) != 0U);
    assert((weak->n_desc & LD_N_WEAK_REF) == 0U);
    assert((output.header->flags & LD_MH_BINDS_TO_WEAK) != 0U);

    free(output.bytes);
    unlink(object_path);
    unlink(tbd_path);
    unlink(output_path);
}

static void test_local_global_and_weak_tlv(void) {
    static const uint32_t text_words[] = {
            0x90000000U,
            0xf9400000U,
            0x90000001U,
            0xf9400021U,
            0x90000002U,
            0x91000042U,
            0xd65f03c0U,
    };
    static const uint8_t descriptors[72] = {0};
    uint32_t relocations[12];
    for (uint32_t i = 0; i < 3U; i++) {
        relocations[i * 4U] = i * 8U;
        relocations[i * 4U + 1U] = test_arm64_relocation_word(
                i + 1U, true, LD_ARM64_RELOC_TLVP_LOAD_PAGE21);
        relocations[i * 4U + 2U] = i * 8U + 4U;
        relocations[i * 4U + 3U] = test_arm64_relocation_word(
                i + 1U, false, LD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12);
    }
    test_macho_section_fixture_t sections[] = {
            {
                    .segname = "__TEXT",
                    .sectname = "__text",
                    .flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                             LD_S_ATTR_SOME_INSTRUCTIONS,
                    .align = 2U,
                    .data = (const uint8_t *) text_words,
                    .size = sizeof(text_words),
                    .relocations = relocations,
                    .relocation_count = 6U,
            },
            {
                    .segname = "__DATA",
                    .sectname = "__thread_vars",
                    .flags = LD_S_THREAD_LOCAL_VARIABLES,
                    .align = 3U,
                    .data = descriptors,
                    .size = sizeof(descriptors),
            },
    };
    static const char strings[] =
            "\0_main\0_local_tls\0_global_tls\0_weak_tls\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_SECT, .n_sect = 2U},
            {.n_strx = 18U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 2U,
             .n_value = 24U},
            {.n_strx = 30U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 2U,
             .n_desc = LD_N_WEAK_DEF,
             .n_value = 48U},
    };
    char object_path[] = "/tmp/nature-ld-synthetic-tlv-XXXXXX";
    test_make_macho_object(object_path, sections, 2U, symbols, 4U, strings,
                           sizeof(strings));
    char output_path[] = "/tmp/nature-ld-synthetic-tlv-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_macho_fixture(object_path, NULL, output_path, LD_OK, NULL);

    test_macho_output_t output = test_read_macho_output(output_path);
    assert(output.thread_vars && output.thread_ptrs);
    assert(output.thread_ptrs->size == sizeof(uint64_t));
    uint64_t weak_pointer;
    memcpy(&weak_pointer, output.bytes + output.thread_ptrs->offset,
           sizeof(weak_pointer));
    assert(weak_pointer == output.thread_vars->addr + 48U);
    uint64_t global_flags = test_export_flags(&output, "_global_tls");
    uint64_t weak_flags = test_export_flags(&output, "_weak_tls");
    assert((global_flags & LD_EXPORT_SYMBOL_FLAGS_KIND_MASK) ==
           LD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL);
    assert((weak_flags & LD_EXPORT_SYMBOL_FLAGS_KIND_MASK) ==
           LD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL);
    assert((weak_flags & LD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION) != 0);

    for (uint32_t i = 0; i < 3U; i++) {
        uint32_t adrp, pageoff;
        memcpy(&adrp, output.bytes + output.text->offset + i * 8U,
               sizeof(adrp));
        memcpy(&pageoff,
               output.bytes + output.text->offset + i * 8U + 4U,
               sizeof(pageoff));
        bool uses_pointer = i == 2U;
        if (uses_pointer) {
            assert((pageoff & 0x3b000000U) == 0x39000000U);
        } else {
            assert((pageoff & 0x1f000000U) == 0x11000000U);
        }
        uint64_t expected = uses_pointer ? output.thread_ptrs->addr
                                         : output.thread_vars->addr + i * 24U;
        assert(test_arm64_page_pair_target(
                       adrp, pageoff, output.text->addr + i * 8U,
                       uses_pointer) == expected);
    }

    free(output.bytes);
    unlink(object_path);
    unlink(output_path);
}

static void test_illegal_tlv_reference(void) {
    static const uint32_t text_words[] = {
            0x90000000U,
            0xf9400000U,
            0xd65f03c0U,
    };
    static const uint64_t regular_data = 0;
    uint32_t relocations[] = {
            0U,
            test_arm64_relocation_word(1U, true,
                                       LD_ARM64_RELOC_TLVP_LOAD_PAGE21),
            4U,
            test_arm64_relocation_word(1U, false,
                                       LD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12),
    };
    test_macho_section_fixture_t sections[] = {
            {
                    .segname = "__TEXT",
                    .sectname = "__text",
                    .flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                             LD_S_ATTR_SOME_INSTRUCTIONS,
                    .align = 2U,
                    .data = (const uint8_t *) text_words,
                    .size = sizeof(text_words),
                    .relocations = relocations,
                    .relocation_count = 2U,
            },
            {
                    .segname = "__DATA",
                    .sectname = "__data",
                    .align = 3U,
                    .data = (const uint8_t *) &regular_data,
                    .size = sizeof(regular_data),
            },
    };
    static const char strings[] = "\0_main\0_regular\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_SECT, .n_sect = 2U},
    };
    char object_path[] = "/tmp/nature-ld-synthetic-illegal-tlv-XXXXXX";
    test_make_macho_object(object_path, sections, 2U, symbols, 2U, strings,
                           sizeof(strings));
    char output_path[] =
            "/tmp/nature-ld-synthetic-illegal-tlv-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_ld_diagnostic_capture_t capture = {0};
    test_link_macho_fixture(object_path, NULL, output_path,
                            LD_RELOCATION_ERROR, &capture);
    assert(strstr(capture.message,
                  "illegal thread-local variable reference") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(object_path);
}

static void test_imported_tlv_pointer(void) {
    static const uint32_t text_words[] = {
            0x90000000U,
            0x91000000U,
            0xd65f03c0U,
    };
    uint32_t relocations[] = {
            0U,
            test_arm64_relocation_word(1U, true,
                                       LD_ARM64_RELOC_TLVP_LOAD_PAGE21),
            4U,
            test_arm64_relocation_word(1U, false,
                                       LD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12),
    };
    test_macho_section_fixture_t section = {
            .segname = "__TEXT",
            .sectname = "__text",
            .flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                     LD_S_ATTR_SOME_INSTRUCTIONS,
            .align = 2U,
            .data = (const uint8_t *) text_words,
            .size = sizeof(text_words),
            .relocations = relocations,
            .relocation_count = 2U,
    };
    static const char strings[] = "\0_main\0_import_tls\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_UNDF | LD_N_EXT},
    };
    char object_path[] = "/tmp/nature-ld-synthetic-import-tlv-XXXXXX";
    test_make_macho_object(object_path, &section, 1U, symbols, 2U, strings,
                           sizeof(strings));

    static const char tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: /usr/lib/libSyntheticTlv.dylib\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    thread-local-symbols: [ _import_tls ]\n"
            "...\n";
    char tbd_path[] = "/tmp/nature-ld-synthetic-import-tlv-tbd-XXXXXX";
    test_ld_write_fixture(tbd_path, tbd, sizeof(tbd) - 1U);
    char output_path[] =
            "/tmp/nature-ld-synthetic-import-tlv-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_macho_fixture(object_path, tbd_path, output_path, LD_OK, NULL);

    test_macho_output_t output = test_read_macho_output(output_path);
    assert(output.thread_ptrs && output.thread_ptrs->size == sizeof(uint64_t));
    uint64_t pointer_value;
    memcpy(&pointer_value, output.bytes + output.thread_ptrs->offset,
           sizeof(pointer_value));
    assert(pointer_value == 0U);
    assert(output.dyld_info && output.dyld_info->bind_size != 0U);
    uint32_t adrp, ldr;
    memcpy(&adrp, output.bytes + output.text->offset, sizeof(adrp));
    memcpy(&ldr, output.bytes + output.text->offset + 4U, sizeof(ldr));
    assert((ldr & 0x3b000000U) == 0x39000000U);
    assert(test_arm64_page_pair_target(adrp, ldr, output.text->addr, true) ==
           output.thread_ptrs->addr);

    free(output.bytes);
    unlink(tbd_path);
    unlink(output_path);

    char weak_dylib_path[] =
            "/tmp/nature-ld-synthetic-weak-tlv-dylib-XXXXXX";
    test_make_weak_tlv_dylib(weak_dylib_path);
    char weak_output_path[] =
            "/tmp/nature-ld-synthetic-weak-tlv-output-XXXXXX";
    int weak_output_fd = mkstemp(weak_output_path);
    assert(weak_output_fd >= 0);
    assert(close(weak_output_fd) == 0);
    test_link_macho_fixture(object_path, weak_dylib_path, weak_output_path,
                            LD_OK, NULL);

    output = test_read_macho_output(weak_output_path);
    assert(output.thread_ptrs && output.thread_ptrs->size == sizeof(uint64_t));
    assert(output.dysymtab && output.dysymtab->nindirectsyms == 0U);
    assert(output.thread_ptrs->reserved1 == 0U);
    assert(output.dyld_info && output.dyld_info->bind_size > 1U &&
           output.dyld_info->weak_bind_size > 1U);
    assert(test_count_bind_symbol(&output, output.dyld_info->bind_off,
                                  output.dyld_info->bind_size,
                                  "_import_tls") == 1U);
    assert(test_count_bind_symbol(&output, output.dyld_info->weak_bind_off,
                                  output.dyld_info->weak_bind_size,
                                  "_import_tls") == 1U);
    const ld_nlist_64_t *weak =
            test_find_output_symbol(&output, "_import_tls");
    assert(weak != NULL && (weak->n_desc & LD_N_REF_TO_WEAK) != 0U);
    assert((weak->n_desc & LD_N_WEAK_REF) == 0U);
    assert((output.header->flags & LD_MH_BINDS_TO_WEAK) != 0U);

    free(output.bytes);
    unlink(object_path);
    unlink(weak_dylib_path);
    unlink(weak_output_path);
}

void test_ld_macho_synthetic(void) {
    test_local_got_and_indirect_offsets();
    test_weak_dylib_provider_direct_and_got_fixups();
    test_local_global_and_weak_tlv();
    test_illegal_tlv_reference();
    test_imported_tlv_pointer();
}

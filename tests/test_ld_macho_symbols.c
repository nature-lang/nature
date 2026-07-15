#include "src/ld/ld_macho_symbols.h"
#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    uint8_t *bytes;
    size_t size;
    const ld_mach_header_64_t *header;
    const ld_section_64_t *text;
    const ld_section_64_t *got;
    const ld_section_64_t *stubs;
    const ld_symtab_command_t *symtab;
    const ld_dysymtab_command_t *dysymtab;
    const ld_dyld_info_command_t *dyld_info;
} test_symbol_output_t;

static uint32_t test_symbol_branch_relocation(uint32_t symbol) {
    return symbol | (1U << 24U) | (2U << 25U) | (1U << 27U) |
           (LD_ARM64_RELOC_BRANCH26 << 28U);
}

static void test_make_symbol_object(char path[], const uint32_t *text,
                                    size_t text_size,
                                    const uint32_t *relocations,
                                    uint32_t relocation_count,
                                    const ld_nlist_64_t *symbols,
                                    uint32_t symbol_count,
                                    const char *strings,
                                    uint32_t strings_size) {
    size_t segment_size =
            sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t) +
                           sizeof(ld_build_version_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t relocation_offset = text_offset + text_size;
    size_t symbol_offset = relocation_offset +
                           (size_t) relocation_count * 8U;
    size_t strings_offset = symbol_offset +
                            (size_t) symbol_count * sizeof(ld_nlist_64_t);
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

    size_t cursor = sizeof(header);
    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, "__TEXT", sizeof("__TEXT") - 1U);
    segment.vmsize = text_size;
    segment.fileoff = text_offset;
    segment.filesize = text_size;
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.initprot = segment.maxprot;
    segment.nsects = 1U;
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = text_size;
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.reloff = (uint32_t) relocation_offset;
    section.nreloc = relocation_count;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                    LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));
    cursor += segment_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = symbol_count;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = strings_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));
    cursor += sizeof(symtab);

    ld_build_version_command_t build = {0};
    build.cmd = LD_LC_BUILD_VERSION;
    build.cmdsize = sizeof(build);
    build.platform = LD_PLATFORM_MACOS;
    build.minos = 11U << 16U;
    build.sdk = 14U << 16U;
    memcpy(object + cursor, &build, sizeof(build));

    memcpy(object + text_offset, text, text_size);
    memcpy(object + relocation_offset, relocations,
           (size_t) relocation_count * 8U);
    memcpy(object + symbol_offset, symbols,
           (size_t) symbol_count * sizeof(*symbols));
    memcpy(object + strings_offset, strings, strings_size);
    test_ld_write_fixture(path, object, object_size);
    free(object);
}

static void test_link_symbol_object(const char *object_path,
                                    const char *output_path) {
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);
}

static test_symbol_output_t test_read_symbol_output(const char *path) {
    test_symbol_output_t output = {0};
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
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= output.size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (segment + 1);
            for (uint32_t j = 0; j < segment->nsects; j++) {
                if (strncmp(sections[j].sectname, "__text", 16U) == 0) {
                    output.text = &sections[j];
                } else if (strncmp(sections[j].sectname, "__got", 16U) ==
                           0) {
                    output.got = &sections[j];
                } else if (strncmp(sections[j].sectname, "__stubs", 16U) ==
                           0) {
                    output.stubs = &sections[j];
                }
            }
        } else if (command->cmd == LD_LC_SYMTAB) {
            output.symtab = (const ld_symtab_command_t *) command;
        } else if (command->cmd == LD_LC_DYSYMTAB) {
            output.dysymtab = (const ld_dysymtab_command_t *) command;
        } else if (command->cmd == LD_LC_DYLD_INFO_ONLY) {
            output.dyld_info = (const ld_dyld_info_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    return output;
}

static const ld_nlist_64_t *test_find_output_symbol(
        const test_symbol_output_t *output, const char *name,
        uint32_t *index) {
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
        const char *symbol_name = strings + symbols[i].n_strx;
        assert(memchr(symbol_name, '\0',
                      output->symtab->strsize - symbols[i].n_strx) != NULL);
        if (strcmp(symbol_name, name) == 0) {
            if (index) *index = i;
            return &symbols[i];
        }
    }
    return NULL;
}

static bool test_symbol_read_uleb(const uint8_t *bytes, size_t size,
                                  size_t *cursor, uint64_t *value) {
    uint64_t result = 0;
    for (unsigned shift = 0; shift < 64U && *cursor < size; shift += 7U) {
        uint8_t byte = bytes[(*cursor)++];
        if (shift == 63U && (byte & 0x7eU)) return false;
        result |= (uint64_t) (byte & 0x7fU) << shift;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }
    }
    return false;
}

static bool test_export_contains_node(const uint8_t *trie, size_t trie_size,
                                      size_t node_offset, char name[256],
                                      size_t name_length, const char *wanted,
                                      unsigned depth) {
    if (depth >= 256U || node_offset >= trie_size) return false;
    size_t cursor = node_offset;
    uint64_t terminal_size;
    if (!test_symbol_read_uleb(trie, trie_size, &cursor, &terminal_size) ||
        terminal_size > trie_size - cursor) {
        return false;
    }
    if (terminal_size && strcmp(name, wanted) == 0) return true;
    cursor += (size_t) terminal_size;
    if (cursor >= trie_size) return false;
    uint8_t child_count = trie[cursor++];
    for (uint8_t child = 0; child < child_count; child++) {
        const char *edge = (const char *) trie + cursor;
        const char *terminator = memchr(edge, '\0', trie_size - cursor);
        if (!terminator) return false;
        size_t edge_length = (size_t) (terminator - edge);
        if (edge_length >= 256U - name_length) return false;
        cursor += edge_length + 1U;
        uint64_t child_offset;
        if (!test_symbol_read_uleb(trie, trie_size, &cursor,
                                   &child_offset)) {
            return false;
        }
        memcpy(name + name_length, edge, edge_length);
        name[name_length + edge_length] = '\0';
        if (test_export_contains_node(trie, trie_size,
                                      (size_t) child_offset, name,
                                      name_length + edge_length, wanted,
                                      depth + 1U)) {
            return true;
        }
        name[name_length] = '\0';
    }
    return false;
}

static bool test_export_contains(const test_symbol_output_t *output,
                                 const char *name) {
    assert(output->dyld_info != NULL && output->dyld_info->export_size != 0);
    assert(output->dyld_info->export_off <= output->size &&
           output->dyld_info->export_size <=
                   output->size - output->dyld_info->export_off);
    char current[256] = {0};
    return test_export_contains_node(
            output->bytes + output->dyld_info->export_off,
            output->dyld_info->export_size, 0, current, 0, name, 0);
}

static uint64_t test_branch_target(const test_symbol_output_t *output,
                                   uint32_t text_offset) {
    uint32_t instruction;
    assert(output->text != NULL &&
           output->text->offset + text_offset + sizeof(instruction) <=
                   output->size);
    memcpy(&instruction,
           output->bytes + output->text->offset + text_offset,
           sizeof(instruction));
    int64_t immediate = instruction & 0x03ffffffU;
    if (immediate & 0x02000000U) immediate -= 1LL << 26U;
    return (uint64_t) ((int64_t) output->text->addr + text_offset +
                       immediate * 4LL);
}

static void test_zig_symbol_rank_order(void) {
    ld_file_t direct_file = {.input_priority = 8U};
    ld_file_t archive_file = {.input_priority = 3U};
    ld_object_t direct = {.file = &direct_file};
    ld_object_t archive = {
            .file = &archive_file,
            .archive_member = true,
    };
    ld_input_symbol_t strong = {0};
    strong.entry.n_type = LD_N_SECT | LD_N_EXT;
    ld_input_symbol_t weak = strong;
    weak.entry.n_desc = LD_N_WEAK_DEF;
    ld_input_symbol_t common = strong;
    common.entry.n_type = LD_N_UNDF | LD_N_EXT;
    common.entry.n_value = 8U;

    ld_macho_symbol_rank_t direct_strong =
            ld_macho_object_symbol_rank(&direct, &strong, 0U);
    ld_macho_symbol_rank_t archive_strong =
            ld_macho_object_symbol_rank(&archive, &strong, 0U);
    ld_macho_symbol_rank_t direct_weak =
            ld_macho_object_symbol_rank(&direct, &weak, 0U);
    ld_macho_symbol_rank_t archive_weak =
            ld_macho_object_symbol_rank(&archive, &weak, 0U);
    ld_macho_symbol_rank_t direct_common =
            ld_macho_object_symbol_rank(&direct, &common, 0U);
    ld_macho_symbol_rank_t archive_common =
            ld_macho_object_symbol_rank(&archive, &common, 0U);

    assert(ld_macho_symbol_rank_better(direct_strong, archive_strong));
    assert(ld_macho_symbol_rank_better(archive_strong, direct_weak));
    assert(ld_macho_symbol_rank_better(direct_weak, archive_weak));
    assert(ld_macho_symbol_rank_better(archive_weak, direct_common));
    assert(ld_macho_symbol_rank_better(direct_common, archive_common));

    ld_dylib_input_t dylib = {.input_priority = 2U};
    ld_dylib_symbol_t dylib_export = {0};
    ld_macho_symbol_rank_t dylib_strong =
            ld_macho_dylib_symbol_rank(&dylib, &dylib_export, 0U);
    assert(ld_macho_symbol_rank_better(dylib_strong, archive_strong));
    dylib.input_priority = 9U;
    dylib_strong = ld_macho_dylib_symbol_rank(&dylib, &dylib_export, 0U);
    assert(ld_macho_symbol_rank_better(archive_strong, dylib_strong));
}

static void test_visibility_semantics(void) {
    assert(ld_macho_nlist_visibility(LD_N_SECT | LD_N_EXT, 0) ==
           LD_VISIBILITY_GLOBAL);
    assert(ld_macho_nlist_visibility(LD_N_SECT | LD_N_EXT | LD_N_PEXT,
                                     0) == LD_VISIBILITY_HIDDEN);
    assert(ld_macho_nlist_visibility(LD_N_SECT, 0) ==
           LD_VISIBILITY_LOCAL);
    assert(ld_macho_nlist_visibility(LD_N_UNDF | LD_N_EXT, 0) ==
           LD_VISIBILITY_LOCAL);
    assert(ld_macho_merge_visibility(LD_VISIBILITY_HIDDEN,
                                     LD_VISIBILITY_GLOBAL) ==
           LD_VISIBILITY_GLOBAL);

    ld_output_section_t output = {0};
    ld_symbol_t symbol = {
            .kind = LD_SYMBOL_DEFINED,
            .output = &output,
            .visibility = LD_VISIBILITY_GLOBAL,
            .weak = true,
    };
    assert(ld_macho_symbol_is_exported(&symbol));
    assert(ld_macho_symbol_is_exported_weak(&symbol));
    assert(ld_macho_symbol_needs_stub(&symbol));
    symbol.visibility = LD_VISIBILITY_HIDDEN;
    assert(!ld_macho_symbol_is_exported(&symbol));
    assert(!ld_macho_symbol_is_exported_weak(&symbol));
    assert(!ld_macho_symbol_needs_stub(&symbol));
    symbol.kind = LD_SYMBOL_IMPORT;
    assert(ld_macho_symbol_needs_stub(&symbol));
}

static void test_private_external_symbol_output(void) {
    static const uint32_t text[] = {
            0x94000000U,
            0xd65f03c0U,
            0xd65f03c0U,
    };
    uint32_t relocations[] = {
            0U,
            test_symbol_branch_relocation(1U),
    };
    static const char strings[] = "\0_main\0_hidden_weak\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
            {.n_strx = 7U,
             .n_type = LD_N_SECT | LD_N_EXT | LD_N_PEXT,
             .n_sect = 1U,
             .n_desc = LD_N_WEAK_DEF,
             .n_value = 8U},
    };
    char object_path[] = "/tmp/nature-ld-hidden-symbol-XXXXXX";
    test_make_symbol_object(object_path, text, sizeof(text), relocations,
                            1U, symbols, 2U, strings, sizeof(strings));
    char output_path[] = "/tmp/nature-ld-hidden-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_symbol_object(object_path, output_path);

    test_symbol_output_t output = test_read_symbol_output(output_path);
    assert(output.dysymtab != NULL);
    assert((output.header->flags & LD_MH_WEAK_DEFINES) == 0);
    assert((output.header->flags & LD_MH_BINDS_TO_WEAK) == 0);
    assert(output.stubs == NULL && output.got == NULL);
    assert(!test_export_contains(&output, "_hidden_weak"));
    uint32_t hidden_index = UINT32_MAX;
    const ld_nlist_64_t *hidden = test_find_output_symbol(
            &output, "_hidden_weak", &hidden_index);
    assert(hidden != NULL);
    assert((hidden->n_type & LD_N_EXT) == 0);
    assert((hidden->n_type & LD_N_PEXT) != 0);
    assert((hidden->n_type & LD_N_TYPE) == LD_N_SECT);
    assert((hidden->n_desc & LD_N_WEAK_DEF) == 0);
    assert(hidden_index >= output.dysymtab->ilocalsym &&
           hidden_index < output.dysymtab->ilocalsym +
                                  output.dysymtab->nlocalsym);
    assert(test_branch_target(&output, 0U) == output.text->addr + 8U);

    free(output.bytes);
    unlink(object_path);
    unlink(output_path);
}

static void test_exported_weak_branch_stub(void) {
    static const uint32_t text[] = {
            0x94000000U,
            0xd65f03c0U,
            0xd65f03c0U,
            0xd65f03c0U,
    };
    uint32_t relocations[] = {
            0U,
            test_symbol_branch_relocation(1U),
    };
    static const char strings[] = "\0_main\0_weak\0_hidden\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
            {.n_strx = 7U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_desc = LD_N_WEAK_DEF,
             .n_value = 8U},
            {.n_strx = 13U,
             .n_type = LD_N_SECT | LD_N_EXT | LD_N_PEXT,
             .n_sect = 1U,
             .n_value = 12U},
    };
    char object_path[] = "/tmp/nature-ld-weak-symbol-XXXXXX";
    test_make_symbol_object(object_path, text, sizeof(text), relocations,
                            1U, symbols, 3U, strings, sizeof(strings));
    char output_path[] = "/tmp/nature-ld-weak-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_symbol_object(object_path, output_path);

    test_symbol_output_t output = test_read_symbol_output(output_path);
    assert(output.dysymtab != NULL && output.dyld_info != NULL);
    assert((output.header->flags & LD_MH_WEAK_DEFINES) != 0);
    assert((output.header->flags & LD_MH_BINDS_TO_WEAK) != 0);
    assert(output.stubs != NULL && output.stubs->size == 12U);
    assert(output.got != NULL && output.got->size == sizeof(uint64_t));
    assert(test_branch_target(&output, 0U) == output.stubs->addr);
    assert(test_export_contains(&output, "_weak"));
    assert(!test_export_contains(&output, "_hidden"));

    uint32_t weak_index = UINT32_MAX;
    const ld_nlist_64_t *weak =
            test_find_output_symbol(&output, "_weak", &weak_index);
    assert(weak != NULL && (weak->n_type & LD_N_EXT) != 0);
    assert((weak->n_type & LD_N_PEXT) == 0);
    assert((weak->n_desc & LD_N_WEAK_DEF) != 0);
    assert(weak_index >= output.dysymtab->iextdefsym &&
           weak_index < output.dysymtab->iextdefsym +
                                output.dysymtab->nextdefsym);

    uint32_t hidden_index = UINT32_MAX;
    const ld_nlist_64_t *hidden =
            test_find_output_symbol(&output, "_hidden", &hidden_index);
    assert(hidden != NULL && (hidden->n_type & LD_N_EXT) == 0 &&
           (hidden->n_type & LD_N_PEXT) != 0);
    assert(hidden_index < output.dysymtab->nlocalsym);

    assert(output.dysymtab->indirectsymoff <= output.size &&
           output.dysymtab->nindirectsyms * sizeof(uint32_t) <=
                   output.size - output.dysymtab->indirectsymoff);
    const uint32_t *indirect = (const uint32_t *) (output.bytes + output.dysymtab->indirectsymoff);
    assert(output.stubs->reserved1 < output.dysymtab->nindirectsyms);
    assert(indirect[output.stubs->reserved1] == weak_index);
    assert(output.dyld_info->weak_bind_size > 1U);

    free(output.bytes);
    unlink(object_path);
    unlink(output_path);
}

static void test_unresolved_weak_import_uses_self_ordinal(void) {
    static const uint32_t text[] = {
            0x94000000U,
            0xd65f03c0U,
    };
    uint32_t relocations[] = {
            0U,
            test_symbol_branch_relocation(1U),
    };
    static const char strings[] = "\0_main\0_optional\0";
    ld_nlist_64_t symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
            {.n_strx = 7U,
             .n_type = LD_N_UNDF | LD_N_EXT,
             .n_desc = LD_N_WEAK_REF},
    };
    char object_path[] = "/tmp/nature-ld-unresolved-weak-object-XXXXXX";
    test_make_symbol_object(object_path, text, sizeof(text), relocations, 1U,
                            symbols, 2U, strings, sizeof(strings));
    char output_path[] = "/tmp/nature-ld-unresolved-weak-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    test_link_symbol_object(object_path, output_path);

    test_symbol_output_t output = test_read_symbol_output(output_path);
    const ld_nlist_64_t *optional =
            test_find_output_symbol(&output, "_optional", NULL);
    assert(optional != NULL && (optional->n_type & LD_N_TYPE) == LD_N_UNDF);
    assert((optional->n_desc & 0xff00U) == 0U);
    assert((optional->n_desc & LD_N_WEAK_REF) != 0U);
    assert(output.dyld_info != NULL && output.dyld_info->bind_size > 0U);
    assert(output.bytes[output.dyld_info->bind_off] ==
           LD_BIND_OPCODE_SET_DYLIB_SPECIAL_IMM);
    assert((output.header->flags & LD_MH_BINDS_TO_WEAK) == 0U);

    free(output.bytes);
    unlink(object_path);
    unlink(output_path);
}

static void test_dylib_export_metadata(void) {
    ld_context_t ctx = {0};
    ld_dylib_input_t dylib = {0};
    assert(ld_macho_dylib_record_symbol(&ctx, &dylib, "_tls", 4U,
                                        NULL, 0U, true, false, true,
                                        false) == LD_OK);
    const ld_dylib_symbol_t *symbol =
            ld_macho_dylib_find_symbol(&dylib, "_tls");
    assert(symbol != NULL && symbol->weak && symbol->tlv);
    assert(ld_macho_dylib_record_symbol(&ctx, &dylib, "_tls", 4U,
                                        "_renamed", 8U, false, false,
                                        true, true) == LD_OK);
    symbol = ld_macho_dylib_find_symbol(&dylib, "_tls");
    assert(symbol != NULL && !symbol->weak && symbol->tlv &&
           symbol->reexport);
    assert(strcmp(symbol->import_name, "_renamed") == 0);
    ld_macho_dylib_symbols_deinit(&dylib);
}

void test_ld_macho_symbols(void) {
    test_zig_symbol_rank_order();
    test_visibility_semantics();
    test_private_external_symbol_output();
    test_exported_weak_branch_stub();
    test_unresolved_weak_import_uses_self_ordinal();
    test_dylib_export_metadata();
}

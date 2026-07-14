#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_compact_unwind_regular_page(void) {
    uint8_t compact_data[2U * sizeof(ld_compact_unwind_entry_t)] = {0};
    ld_compact_unwind_entry_t entries_in[2] = {
            {.range_length = 0x20U, .compact_encoding = 0x02000000U},
            {.range_start = 0x20U,
             .range_length = 0x10U,
             .compact_encoding = 0x04000001U},
    };
    memcpy(compact_data, entries_in, sizeof(entries_in));

    uint8_t relocations[16] = {0};
    uint32_t relocation_address = 0;
    uint32_t relocation_word = 1U | (3U << 25U);
    memcpy(relocations, &relocation_address, sizeof(relocation_address));
    memcpy(relocations + 4U, &relocation_word, sizeof(relocation_word));
    relocation_address = sizeof(ld_compact_unwind_entry_t);
    memcpy(relocations + 8U, &relocation_address, sizeof(relocation_address));
    memcpy(relocations + 12U, &relocation_word, sizeof(relocation_word));

    ld_output_section_t text_output = {0};
    memcpy(text_output.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(text_output.sectname, "__text", sizeof("__text"));
    text_output.addr = LD_IMAGE_BASE + 0x1000U;

    ld_input_section_t sections[2] = {0};
    memcpy(sections[0].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[0].header.sectname, "__text", sizeof("__text"));
    sections[0].output = &text_output;
    memcpy(sections[1].header.segname, "__LD", sizeof("__LD"));
    memcpy(sections[1].header.sectname, "__compact_unwind", 16U);
    sections[1].header.size = sizeof(compact_data);
    sections[1].header.nreloc = 2;
    sections[1].data = compact_data;
    sections[1].relocations = relocations;

    ld_file_t file = {.path = "/tmp/ld-unwind-unit.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = sections,
            .section_count = 2,
    };
    sections[0].object = &object;
    sections[1].object = &object;
    ld_object_t *objects[] = {&object};
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    ctx.objects.items = objects;
    ctx.objects.count = 1;

    assert(ld_unwind_prepare(&ctx) == LD_OK);
    assert(ctx.unwind.count == 2);
    assert(ctx.unwind.output != NULL);
    assert(strcmp(ctx.unwind.output->sectname, "__unwind_info") == 0);
    assert(ld_unwind_emit(&ctx) == LD_OK);

    ld_unwind_info_header_t header;
    memcpy(&header, ctx.unwind.output->data, sizeof(header));
    assert(header.version == LD_UNWIND_SECTION_VERSION);
    assert(header.common_encodings_count == 0);
    assert(header.personalities_count == 0);
    assert(header.index_count == 2);

    ld_unwind_info_index_entry_t index[2];
    memcpy(index, ctx.unwind.output->data + header.index_offset, sizeof(index));
    assert(index[0].function_offset == 0x1000U);
    assert(index[1].function_offset == 0x1030U);
    assert(index[1].second_level_page_offset == 0);

    ld_unwind_info_regular_page_header_t page;
    memcpy(&page, ctx.unwind.output->data + index[0].second_level_page_offset,
           sizeof(page));
    assert(page.kind == LD_UNWIND_SECOND_LEVEL_REGULAR);
    assert(page.entry_page_offset == sizeof(page));
    assert(page.entry_count == 2);
    ld_unwind_info_regular_entry_t entries_out[2];
    memcpy(entries_out,
           ctx.unwind.output->data + index[0].second_level_page_offset +
                   page.entry_page_offset,
           sizeof(entries_out));
    assert(entries_out[0].function_offset == 0x1000U);
    assert(entries_out[0].encoding == 0x02000000U);
    assert(entries_out[1].function_offset == 0x1020U);
    assert(entries_out[1].encoding == 0x04000001U);

    free(ctx.unwind.output->data);
    free(ctx.unwind.output);
    free(ctx.outputs.items);
    free(ctx.unwind.records);
    ld_options_deinit(&options);
}

static void test_arm64_branch_island(void) {
    static const char strings[] = "\0_main\0_far\0";
    const uint64_t far_address = LD_IMAGE_BASE + 0x10000000ULL;
    size_t segment_size = sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t relocation_offset = text_offset + sizeof(uint32_t);
    size_t symbol_offset = relocation_offset + 4U * sizeof(uint32_t);
    size_t strings_offset = symbol_offset + 2U * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + sizeof(strings);
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 2U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, "__TEXT", sizeof("__TEXT") - 1U);
    segment.vmsize = sizeof(uint32_t);
    segment.fileoff = text_offset;
    segment.filesize = sizeof(uint32_t);
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.initprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.nsects = 1U;
    size_t cursor = sizeof(header);
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = sizeof(uint32_t);
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.reloff = (uint32_t) relocation_offset;
    section.nreloc = 2U;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    cursor += segment_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));

    uint32_t branch = 0x94000000U;
    memcpy(object + text_offset, &branch, sizeof(branch));
    uint32_t relocation[4] = {
            0U,
            4U | (LD_ARM64_RELOC_ADDEND << 28U),
            0U,
            1U | (1U << 24U) | (2U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_BRANCH26 << 28U),
    };
    memcpy(object + relocation_offset, relocation, sizeof(relocation));
    ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U, .n_type = LD_N_SECT | LD_N_EXT, .n_sect = 1U, .n_value = 0U},
            {.n_strx = 7U, .n_type = LD_N_ABS | LD_N_EXT, .n_value = far_address},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));

    char object_path[] = "/tmp/nature-ld-branch-object-XXXXXX";
    test_ld_write_fixture(object_path, object, object_size);
    char output_path[] = "/tmp/nature-ld-branch-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);

    int fd = open(output_path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    uint8_t *image = malloc((size_t) st.st_size);
    assert(image != NULL);
    assert(read(fd, image, (size_t) st.st_size) == st.st_size);
    assert(close(fd) == 0);
    const ld_mach_header_64_t *output_header = (const ld_mach_header_64_t *) image;
    assert(output_header->magic == LD_MH_MAGIC_64);
    const ld_section_64_t *text = NULL;
    const ld_section_64_t *islands = NULL;
    cursor = sizeof(*output_header);
    for (uint32_t command_index = 0; command_index < output_header->ncmds; command_index++) {
        assert(cursor + sizeof(ld_load_command_t) <= (size_t) st.st_size);
        const ld_load_command_t *command = (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *output_segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (output_segment + 1);
            for (uint32_t section_index = 0; section_index < output_segment->nsects;
                 section_index++) {
                if (strncmp(sections[section_index].sectname, "__text", 16) == 0) {
                    text = &sections[section_index];
                } else if (strncmp(sections[section_index].sectname, "__branch_islands", 16) == 0) {
                    islands = &sections[section_index];
                }
            }
        }
        cursor += command->cmdsize;
    }
    assert(text != NULL && islands != NULL && islands->size == 12U);
    assert(text->offset + sizeof(uint32_t) <= (size_t) st.st_size);
    assert(islands->offset + islands->size <= (size_t) st.st_size);
    memcpy(&branch, image + text->offset, sizeof(branch));
    int64_t branch_immediate = branch & 0x03ffffffU;
    if (branch_immediate & 0x02000000U) branch_immediate -= 1LL << 26U;
    assert(text->addr + (uint64_t) (branch_immediate * 4LL) == islands->addr);

    uint32_t adrp, add, br;
    memcpy(&adrp, image + islands->offset, sizeof(adrp));
    memcpy(&add, image + islands->offset + 4U, sizeof(add));
    memcpy(&br, image + islands->offset + 8U, sizeof(br));
    assert((adrp & 0x9f00001fU) == 0x90000010U);
    assert((add & 0xffc003ffU) == 0x91000210U);
    assert(br == 0xd61f0200U);
    int64_t adrp_immediate = (int64_t) (((adrp >> 5U) & 0x7ffffU) << 2U) |
                             (int64_t) ((adrp >> 29U) & 3U);
    if (adrp_immediate & (1LL << 20U)) adrp_immediate -= 1LL << 21U;
    uint64_t thunk_target = (uint64_t) ((int64_t) (islands->addr & ~0xfffULL) +
                                        adrp_immediate * 0x1000LL) +
                            ((add >> 10U) & 0xfffU);
    assert(thunk_target == far_address + 4U);
    free(image);

    /* POINTER_TO_GOT has no absolute 64-bit encoding on arm64.  A malformed
       object must fail during relocation validation before an output file is
       created, instead of silently treating the field as an image pointer. */
    ld_section_64_t *input_section =
            (ld_section_64_t *) (object + sizeof(ld_mach_header_64_t) + sizeof(ld_segment_command_64_t));
    input_section->nreloc = 1U;
    uint32_t invalid_pointer_relocation[2] = {
            0U,
            1U | (2U << 25U) | (1U << 27U) | (LD_ARM64_RELOC_POINTER_TO_GOT << 28U),
    };
    memcpy(object + relocation_offset, invalid_pointer_relocation,
           sizeof(invalid_pointer_relocation));
    char invalid_object_path[] = "/tmp/nature-ld-pointer-object-XXXXXX";
    test_ld_write_fixture(invalid_object_path, object, object_size);
    char invalid_output_path[] = "/tmp/nature-ld-pointer-output-XXXXXX";
    int invalid_output_fd = mkstemp(invalid_output_path);
    assert(invalid_output_fd >= 0);
    assert(close(invalid_output_fd) == 0);
    unlink(invalid_output_path);
    ld_options_init(&options);
    options.output_path = invalid_output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, invalid_object_path) == LD_OK);
    assert(ld_link(&options) == LD_RELOCATION_ERROR);
    assert(access(invalid_output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(invalid_object_path);
    free(object);
    unlink(object_path);
    unlink(output_path);
}

static void test_indirect_symbol_alias(void) {
    static const char strings[] = "\0_main\0_alias\0";
    size_t segment_size = sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t symbol_offset = text_offset + sizeof(uint32_t);
    size_t strings_offset = symbol_offset + 2U * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + sizeof(strings);
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 2U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, "__TEXT", sizeof("__TEXT") - 1U);
    segment.vmsize = sizeof(uint32_t);
    segment.fileoff = text_offset;
    segment.filesize = sizeof(uint32_t);
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.initprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.nsects = 1U;
    size_t cursor = sizeof(header);
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = sizeof(uint32_t);
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    cursor += segment_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));

    uint32_t ret = 0xd65f03c0U;
    memcpy(object + text_offset, &ret, sizeof(ret));
    ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U, .n_type = LD_N_SECT | LD_N_EXT, .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_INDR | LD_N_EXT, .n_value = 1U},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));

    char object_path[] = "/tmp/nature-ld-alias-object-XXXXXX";
    test_ld_write_fixture(object_path, object, object_size);
    free(object);
    char output_path[] = "/tmp/nature-ld-alias-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.entry_symbol = "alias";
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);

    int fd = open(output_path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    uint8_t *image = malloc((size_t) st.st_size);
    assert(image != NULL);
    assert(read(fd, image, (size_t) st.st_size) == st.st_size);
    assert(close(fd) == 0);

    const ld_mach_header_64_t *output_header = (const ld_mach_header_64_t *) image;
    const ld_section_64_t *text = NULL;
    const ld_entry_point_command_t *entry = NULL;
    const ld_symtab_command_t *output_symtab = NULL;
    cursor = sizeof(*output_header);
    for (uint32_t command_index = 0; command_index < output_header->ncmds; command_index++) {
        const ld_load_command_t *command = (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *output_segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (output_segment + 1);
            for (uint32_t i = 0; i < output_segment->nsects; i++) {
                if (strncmp(sections[i].sectname, "__text", 16) == 0) text = &sections[i];
            }
        } else if (command->cmd == LD_LC_MAIN) {
            entry = (const ld_entry_point_command_t *) command;
        } else if (command->cmd == LD_LC_SYMTAB) {
            output_symtab = (const ld_symtab_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    assert(text != NULL && entry != NULL && output_symtab != NULL);
    assert(entry->entryoff == text->offset);
    assert(output_symtab->symoff +
                   (uint64_t) output_symtab->nsyms * sizeof(ld_nlist_64_t) <=
           (uint64_t) st.st_size);
    assert(output_symtab->stroff + output_symtab->strsize <= (uint64_t) st.st_size);
    const ld_nlist_64_t *output_symbols =
            (const ld_nlist_64_t *) (image + output_symtab->symoff);
    const char *output_strings = (const char *) image + output_symtab->stroff;
    uint64_t main_value = 0, alias_value = 0;
    for (uint32_t i = 0; i < output_symtab->nsyms; i++) {
        assert(output_symbols[i].n_strx < output_symtab->strsize);
        const char *name = output_strings + output_symbols[i].n_strx;
        if (strcmp(name, "_main") == 0) main_value = output_symbols[i].n_value;
        if (strcmp(name, "_alias") == 0) alias_value = output_symbols[i].n_value;
    }
    assert(main_value == text->addr);
    assert(alias_value == main_value);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_macho_codegen(void) {
    test_compact_unwind_regular_page();
    test_arm64_branch_island();
    test_indirect_symbol_alias();
}

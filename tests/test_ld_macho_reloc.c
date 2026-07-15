#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_make_import32_object(char path[]) {
    static const char strings[] = "\0_main\0_import32\0";
    const size_t segment_size =
            sizeof(ld_segment_command_64_t) + 2U * sizeof(ld_section_64_t);
    const size_t commands_size = segment_size + sizeof(ld_symtab_command_t) +
                                 sizeof(ld_build_version_command_t);
    const size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    const size_t data_offset = text_offset + sizeof(uint32_t);
    const size_t relocation_offset = data_offset + 2U * sizeof(uint32_t);
    const size_t symbol_offset = relocation_offset + 2U * sizeof(uint32_t);
    const size_t strings_offset =
            symbol_offset + 2U * sizeof(ld_nlist_64_t);
    const size_t object_size = strings_offset + sizeof(strings);
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
    segment.vmsize = 3U * sizeof(uint32_t);
    segment.fileoff = text_offset;
    segment.filesize = 3U * sizeof(uint32_t);
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_WRITE |
                      LD_VM_PROT_EXECUTE;
    segment.initprot = segment.maxprot;
    segment.nsects = 2U;
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t text = {0};
    memcpy(text.sectname, "__text", sizeof("__text") - 1U);
    memcpy(text.segname, "__TEXT", sizeof("__TEXT") - 1U);
    text.size = sizeof(uint32_t);
    text.offset = (uint32_t) text_offset;
    text.align = 2U;
    text.flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                 LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &text, sizeof(text));

    ld_section_64_t data = {0};
    memcpy(data.sectname, "__data", sizeof("__data") - 1U);
    memcpy(data.segname, "__DATA", sizeof("__DATA") - 1U);
    data.addr = sizeof(uint32_t);
    data.size = 2U * sizeof(uint32_t);
    data.offset = (uint32_t) data_offset;
    data.align = 2U;
    data.reloff = (uint32_t) relocation_offset;
    data.nreloc = 1U;
    memcpy(object + cursor + sizeof(segment) + sizeof(text), &data,
           sizeof(data));
    cursor += segment_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    memcpy(object + cursor, &symtab, sizeof(symtab));
    cursor += sizeof(symtab);

    ld_build_version_command_t build = {0};
    build.cmd = LD_LC_BUILD_VERSION;
    build.cmdsize = sizeof(build);
    build.platform = LD_PLATFORM_MACOS;
    build.minos = ld_macos_version(11, 0, 0);
    build.sdk = ld_macos_version(14, 0, 0);
    memcpy(object + cursor, &build, sizeof(build));

    const uint32_t ret = 0xd65f03c0U;
    const uint32_t data_words[2] = {0x10203040U, 0xa5a5f00dU};
    memcpy(object + text_offset, &ret, sizeof(ret));
    memcpy(object + data_offset, data_words, sizeof(data_words));
    const uint32_t relocation[2] = {
            0U,
            1U | (2U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_UNSIGNED << 28U),
    };
    memcpy(object + relocation_offset, relocation, sizeof(relocation));
    const ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
            {.n_strx = 7U,
             .n_type = LD_N_UNDF | LD_N_EXT,
             .n_sect = 0U,
             .n_value = 0U},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));
    test_ld_write_fixture(path, object, object_size);
    free(object);
}

static void test_imported_unsigned32_is_not_pointer_bind(void) {
    static const char tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: /usr/lib/libImport32.dylib\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    symbols: [ _import32 ]\n"
            "...\n";
    char object_path[] = "/tmp/nature-ld-import32-object-XXXXXX";
    char tbd_path[] = "/tmp/nature-ld-import32-tbd-XXXXXX";
    char output_path[] = "/tmp/nature-ld-import32-output-XXXXXX";
    test_make_import32_object(object_path);
    test_ld_write_fixture(tbd_path, tbd, sizeof(tbd) - 1U);
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);

    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_add_input(&options, tbd_path) == LD_OK);
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

    const ld_mach_header_64_t *header =
            (const ld_mach_header_64_t *) image;
    assert(header->magic == LD_MH_MAGIC_64);
    const ld_section_64_t *output_data = NULL;
    const ld_dyld_info_command_t *dyld_info = NULL;
    size_t cursor = sizeof(*header);
    for (uint32_t command_index = 0; command_index < header->ncmds;
         command_index++) {
        assert(cursor + sizeof(ld_load_command_t) <= (size_t) st.st_size);
        const ld_load_command_t *command =
                (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (segment + 1);
            for (uint32_t section_index = 0;
                 section_index < segment->nsects; section_index++) {
                if (strncmp(sections[section_index].segname, "__DATA", 16U) ==
                            0 &&
                    strncmp(sections[section_index].sectname, "__data", 16U) ==
                            0) {
                    output_data = &sections[section_index];
                }
            }
        } else if (command->cmd == LD_LC_DYLD_INFO_ONLY) {
            dyld_info = (const ld_dyld_info_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    assert(output_data != NULL && output_data->size >= 8U);
    assert(output_data->offset <= (uint64_t) st.st_size &&
           output_data->size <=
                   (uint64_t) st.st_size - output_data->offset);
    uint32_t value;
    uint32_t canary;
    memcpy(&value, image + output_data->offset, sizeof(value));
    memcpy(&canary, image + output_data->offset + sizeof(value),
           sizeof(canary));
    assert(value == 0x10203040U);
    assert(canary == 0xa5a5f00dU);

    /* An empty classic bind stream may be encoded either as zero bytes or as
       a single BIND_OPCODE_DONE.  It must not contain an 8-byte pointer bind
       for the four-byte relocation above. */
    assert(dyld_info != NULL);
    assert(dyld_info->bind_size == 0U ||
           (dyld_info->bind_size == 1U &&
            dyld_info->bind_off < (uint64_t) st.st_size &&
            image[dyld_info->bind_off] == 0U));

    free(image);
    unlink(object_path);
    unlink(tbd_path);
    unlink(output_path);
}

static void test_make_exported_weak_pointer_object(char path[]) {
    static const char strings[] = "\0_main\0_weak_data\0";
    const size_t segment_size =
            sizeof(ld_segment_command_64_t) + 2U * sizeof(ld_section_64_t);
    const size_t commands_size = segment_size + sizeof(ld_symtab_command_t) +
                                 sizeof(ld_build_version_command_t);
    const size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    const size_t data_offset = text_offset + sizeof(uint32_t);
    const size_t relocation_offset = data_offset + 2U * sizeof(uint64_t);
    const size_t symbol_offset = relocation_offset + 2U * sizeof(uint32_t);
    const size_t strings_offset =
            symbol_offset + 2U * sizeof(ld_nlist_64_t);
    const size_t object_size = strings_offset + sizeof(strings);
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
    segment.vmsize = sizeof(uint32_t) + 2U * sizeof(uint64_t);
    segment.fileoff = text_offset;
    segment.filesize = segment.vmsize;
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_WRITE |
                      LD_VM_PROT_EXECUTE;
    segment.initprot = segment.maxprot;
    segment.nsects = 2U;
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t text = {0};
    memcpy(text.sectname, "__text", sizeof("__text") - 1U);
    memcpy(text.segname, "__TEXT", sizeof("__TEXT") - 1U);
    text.size = sizeof(uint32_t);
    text.offset = (uint32_t) text_offset;
    text.align = 2U;
    text.flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                 LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &text, sizeof(text));

    ld_section_64_t data = {0};
    memcpy(data.sectname, "__data", sizeof("__data") - 1U);
    memcpy(data.segname, "__DATA", sizeof("__DATA") - 1U);
    data.addr = sizeof(uint32_t);
    data.size = 2U * sizeof(uint64_t);
    data.offset = (uint32_t) data_offset;
    data.align = 3U;
    data.reloff = (uint32_t) relocation_offset;
    data.nreloc = 1U;
    memcpy(object + cursor + sizeof(segment) + sizeof(text), &data,
           sizeof(data));
    cursor += segment_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    memcpy(object + cursor, &symtab, sizeof(symtab));
    cursor += sizeof(symtab);

    ld_build_version_command_t build = {0};
    build.cmd = LD_LC_BUILD_VERSION;
    build.cmdsize = sizeof(build);
    build.platform = LD_PLATFORM_MACOS;
    build.minos = ld_macos_version(11, 0, 0);
    build.sdk = ld_macos_version(14, 0, 0);
    memcpy(object + cursor, &build, sizeof(build));

    const uint32_t ret = 0xd65f03c0U;
    const uint64_t data_words[2] = {0U, 0x1122334455667788ULL};
    memcpy(object + text_offset, &ret, sizeof(ret));
    memcpy(object + data_offset, data_words, sizeof(data_words));
    const uint32_t relocation[2] = {
            0U,
            1U | (3U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_UNSIGNED << 28U),
    };
    memcpy(object + relocation_offset, relocation, sizeof(relocation));
    const ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
            {.n_strx = 7U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 2U,
             .n_desc = LD_N_WEAK_DEF,
             .n_value = sizeof(uint32_t) + sizeof(uint64_t)},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));
    test_ld_write_fixture(path, object, object_size);
    free(object);
}

static void test_exported_weak_pointer_keeps_local_initial_value(void) {
    char object_path[] = "/tmp/nature-ld-weak-pointer-object-XXXXXX";
    char output_path[] = "/tmp/nature-ld-weak-pointer-output-XXXXXX";
    test_make_exported_weak_pointer_object(object_path);
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

    const ld_mach_header_64_t *header =
            (const ld_mach_header_64_t *) image;
    assert(header->magic == LD_MH_MAGIC_64);
    const ld_section_64_t *output_data = NULL;
    const ld_dyld_info_command_t *dyld_info = NULL;
    size_t cursor = sizeof(*header);
    for (uint32_t command_index = 0; command_index < header->ncmds;
         command_index++) {
        assert(cursor + sizeof(ld_load_command_t) <= (size_t) st.st_size);
        const ld_load_command_t *command =
                (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (segment + 1);
            for (uint32_t section_index = 0;
                 section_index < segment->nsects; section_index++) {
                if (strncmp(sections[section_index].segname, "__DATA", 16U) ==
                            0 &&
                    strncmp(sections[section_index].sectname, "__data", 16U) ==
                            0) {
                    output_data = &sections[section_index];
                }
            }
        } else if (command->cmd == LD_LC_DYLD_INFO_ONLY) {
            dyld_info = (const ld_dyld_info_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    assert(output_data != NULL && output_data->size >= 16U);
    assert(output_data->offset <= (uint64_t) st.st_size &&
           output_data->size <=
                   (uint64_t) st.st_size - output_data->offset);
    uint64_t pointer;
    memcpy(&pointer, image + output_data->offset, sizeof(pointer));
    assert(pointer == output_data->addr + sizeof(uint64_t));
    assert(dyld_info != NULL && dyld_info->weak_bind_size > 0U);
    assert((header->flags & LD_MH_WEAK_DEFINES) != 0U);
    assert((header->flags & LD_MH_BINDS_TO_WEAK) != 0U);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_macho_reloc(void) {
    test_imported_unsigned32_is_not_pointer_bind();
    test_exported_weak_pointer_keeps_local_initial_value();
}

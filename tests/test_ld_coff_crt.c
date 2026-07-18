#include "src/binary/coff/coff_writer.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NATURE_SOURCE_DIR
#error "NATURE_SOURCE_DIR must name the Nature source tree"
#endif

typedef struct {
    char message[4096];
} diagnostic_capture_t;

static void diagnostic(void *context, ld_diag_level_t level,
                       const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static void make_main_object(const char *path) {
    coff_object_t *object = coff_object_create("nature-crt-smoke.c");
    assert(object);
    coff_section_t *text = NULL;
    assert(coff_object_add_section(
                   object, ".text$main",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &text) == COFF_WRITER_OK);
    static const uint8_t code[] = {
            0xb8, 0x25, 0x00, 0x00, 0x00, /* mov eax, 37 */
            0xc3, /* ret */
    };
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_object_add_defined_symbol(
                   object, "main", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    assert(coff_object_write_file(object, path) == COFF_WRITER_OK);
    coff_object_destroy(object);
}

typedef struct {
    char name[LD_COFF_NAME_SIZE + 1U];
    uint32_t virtual_size;
    uint32_t rva;
} pe_section_t;

static bool range_ok(size_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= (uint64_t) size - offset;
}

static uint16_t read_u16(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    assert(range_ok(size, offset, 2U));
    return (uint16_t) bytes[offset] |
           (uint16_t) ((uint16_t) bytes[offset + 1U] << 8U);
}

static uint32_t read_u32(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    assert(range_ok(size, offset, 4U));
    return (uint32_t) bytes[offset] |
           (uint32_t) bytes[offset + 1U] << 8U |
           (uint32_t) bytes[offset + 2U] << 16U |
           (uint32_t) bytes[offset + 3U] << 24U;
}

static uint64_t read_u64(const uint8_t *bytes, size_t size,
                         uint64_t offset) {
    return (uint64_t) read_u32(bytes, size, offset) |
           (uint64_t) read_u32(bytes, size, offset + 4U) << 32U;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t) length);
    assert(bytes);
    assert(fread(bytes, 1U, (size_t) length, file) == (size_t) length);
    assert(fclose(file) == 0);
    *size = (size_t) length;
    return bytes;
}

static const pe_section_t *find_section(const pe_section_t *sections,
                                        uint16_t count, const char *name) {
    for (uint16_t i = 0U; i < count; i++)
        if (strcmp(sections[i].name, name) == 0) return &sections[i];
    return NULL;
}

static void assert_directory_in_section(const uint8_t *image,
                                        size_t image_size,
                                        uint64_t directory_table,
                                        unsigned directory_index,
                                        const pe_section_t *section) {
    assert(section);
    uint32_t rva = read_u32(image, image_size,
                            directory_table + directory_index * 8U);
    uint32_t size = read_u32(image, image_size,
                             directory_table + directory_index * 8U + 4U);
    assert(rva >= section->rva && size > 0U);
    uint64_t directory_end = (uint64_t) rva + size;
    uint64_t section_end =
            (uint64_t) section->rva + section->virtual_size;
    assert(directory_end >= rva && directory_end <= section_end);
}

static void validate_pe_image(const char *path) {
    size_t image_size = 0U;
    uint8_t *image = read_file(path, &image_size);
    assert(image_size >= LD_PE_OFFSET + 4U + LD_PE_COFF_HEADER_SIZE +
                                 LD_PE_OPTIONAL_HEADER64_SIZE);
    assert(image[0] == 'M' && image[1] == 'Z');
    uint32_t pe_offset = read_u32(image, image_size, 0x3cU);
    assert(pe_offset == LD_PE_OFFSET);
    assert(memcmp(image + pe_offset, "PE\0\0", 4U) == 0);

    uint64_t coff = (uint64_t) pe_offset + 4U;
    assert(read_u16(image, image_size, coff) == LD_COFF_MACHINE_AMD64);
    uint16_t section_count = read_u16(image, image_size, coff + 2U);
    assert(section_count > 0U);
    assert(read_u32(image, image_size, coff + 4U) == 0U);
    assert(read_u16(image, image_size, coff + 16U) ==
           LD_PE_OPTIONAL_HEADER64_SIZE);

    uint64_t optional = coff + LD_PE_COFF_HEADER_SIZE;
    assert(read_u16(image, image_size, optional) ==
           LD_PE_OPTIONAL_MAGIC_PE32_PLUS);
    assert(read_u64(image, image_size, optional + 24U) ==
           LD_PE_IMAGE_BASE64);
    assert(read_u32(image, image_size, optional + 32U) ==
           LD_PE_SECTION_ALIGNMENT);
    assert(read_u32(image, image_size, optional + 36U) ==
           LD_PE_FILE_ALIGNMENT);
    assert(read_u16(image, image_size, optional + 68U) ==
           LD_PE_SUBSYSTEM_WINDOWS_CUI);
    uint16_t dll_characteristics =
            read_u16(image, image_size, optional + 70U);
    assert((dll_characteristics & LD_PE_DLL_HIGH_ENTROPY_VA) != 0U);
    assert((dll_characteristics & LD_PE_DLL_DYNAMIC_BASE) != 0U);
    assert((dll_characteristics & LD_PE_DLL_NX_COMPAT) != 0U);
    assert((dll_characteristics & LD_PE_DLL_TERMINAL_SERVER_AWARE) != 0U);
    assert(read_u64(image, image_size, optional + 72U) ==
           LD_PE_STACK_RESERVE);
    assert(read_u64(image, image_size, optional + 80U) ==
           LD_PE_STACK_COMMIT);
    assert(read_u64(image, image_size, optional + 88U) ==
           LD_PE_HEAP_RESERVE);
    assert(read_u64(image, image_size, optional + 96U) ==
           LD_PE_HEAP_COMMIT);
    assert(read_u32(image, image_size, optional + 108U) ==
           LD_PE_DIRECTORY_COUNT);

    uint64_t section_table = optional + LD_PE_OPTIONAL_HEADER64_SIZE;
    assert(range_ok(image_size, section_table,
                    (uint64_t) section_count * LD_PE_SECTION_HEADER_SIZE));
    pe_section_t *sections = calloc(section_count, sizeof(*sections));
    assert(sections);
    for (uint16_t i = 0U; i < section_count; i++) {
        uint64_t header =
                section_table + (uint64_t) i * LD_PE_SECTION_HEADER_SIZE;
        memcpy(sections[i].name, image + header, LD_COFF_NAME_SIZE);
        sections[i].name[LD_COFF_NAME_SIZE] = '\0';
        sections[i].virtual_size = read_u32(image, image_size, header + 8U);
        sections[i].rva = read_u32(image, image_size, header + 12U);
    }

    const pe_section_t *rdata = find_section(sections, section_count, ".rdata");
    const pe_section_t *pdata = find_section(sections, section_count, ".pdata");
    const pe_section_t *xdata = find_section(sections, section_count, ".xdata");
    const pe_section_t *idata = find_section(sections, section_count, ".idata");
    const pe_section_t *tls = find_section(sections, section_count, ".tls");
    const pe_section_t *reloc = find_section(sections, section_count, ".reloc");
    assert(rdata && pdata && xdata && idata && tls && reloc);
    assert(pdata->virtual_size > 0U && pdata->virtual_size % 12U == 0U);
    assert(xdata->virtual_size > 0U && tls->virtual_size > 0U &&
           reloc->virtual_size > 0U);

    uint64_t directories = optional + 112U;
    assert_directory_in_section(image, image_size, directories,
                                LD_PE_DIRECTORY_IMPORT, idata);
    assert_directory_in_section(image, image_size, directories,
                                LD_PE_DIRECTORY_EXCEPTION, pdata);
    assert_directory_in_section(image, image_size, directories,
                                LD_PE_DIRECTORY_BASERELOC, reloc);
    assert_directory_in_section(image, image_size, directories,
                                LD_PE_DIRECTORY_TLS, rdata);
    assert_directory_in_section(image, image_size, directories,
                                LD_PE_DIRECTORY_IAT, idata);
    assert(read_u32(image, image_size,
                    directories + LD_PE_DIRECTORY_TLS * 8U + 4U) == 40U);

    free(sections);
    free(image);
}

int main(void) {
    static const char *names[] = {
            "crt2.obj",
            NULL,
            "libmingw32.lib",
            "compiler_rt.lib",
            "ucrtbase.lib",
            "api-ms-win-crt-conio-l1-1-0.lib",
            "api-ms-win-crt-convert-l1-1-0.lib",
            "api-ms-win-crt-environment-l1-1-0.lib",
            "api-ms-win-crt-filesystem-l1-1-0.lib",
            "api-ms-win-crt-heap-l1-1-0.lib",
            "api-ms-win-crt-locale-l1-1-0.lib",
            "api-ms-win-crt-math-l1-1-0.lib",
            "api-ms-win-crt-multibyte-l1-1-0.lib",
            "api-ms-win-crt-private-l1-1-0.lib",
            "api-ms-win-crt-process-l1-1-0.lib",
            "api-ms-win-crt-runtime-l1-1-0.lib",
            "api-ms-win-crt-stdio-l1-1-0.lib",
            "api-ms-win-crt-string-l1-1-0.lib",
            "api-ms-win-crt-time-l1-1-0.lib",
            "api-ms-win-crt-utility-l1-1-0.lib",
            "advapi32.lib",
            "bcrypt.lib",
            "crypt32.lib",
            "dbghelp.lib",
            "iphlpapi.lib",
            "kernel32.lib",
            "ntdll.lib",
            "ole32.lib",
            "psapi.lib",
            "secur32.lib",
            "shell32.lib",
            "user32.lib",
            "userenv.lib",
            "version.lib",
            "winmm.lib",
            "ws2_32.lib",
    };
    const size_t count = sizeof(names) / sizeof(*names);
    char **inputs = calloc(count, sizeof(*inputs));
    assert(inputs);
    char main_object[] = "/tmp/nature-ld-coff-crt-main.obj";
    make_main_object(main_object);
    names[1] = main_object;
    for (size_t i = 0; i < count; i++) {
        if (i == 1U) {
            inputs[i] = main_object;
            continue;
        }
        size_t length = strlen(NATURE_SOURCE_DIR) + strlen(names[i]) + 23U;
        inputs[i] = malloc(length);
        assert(inputs[i]);
        int written = snprintf(inputs[i], length, "%s/lib/windows_amd64/%s",
                               NATURE_SOURCE_DIR, names[i]);
        assert(written > 0 && (size_t) written < length);
    }

    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = "/tmp/nature-ld-coff-crt.exe";
    options.entry_symbol = "mainCRTStartup";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = count;
    options.inputs.capacity = count;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);

    validate_pe_image(options.output_path);
    for (size_t i = 0; i < count; i++)
        if (i != 1U) free(inputs[i]);
    free(inputs);
    return 0;
}

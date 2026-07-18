#include "src/ld/ld_coff_internal.h"

#include <assert.h>
#include <stdio.h>
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

static bool relocation_supported(uint16_t type) {
    switch (type) {
        case LD_COFF_REL_AMD64_ABSOLUTE:
        case LD_COFF_REL_AMD64_ADDR64:
        case LD_COFF_REL_AMD64_ADDR32:
        case LD_COFF_REL_AMD64_ADDR32NB:
        case LD_COFF_REL_AMD64_REL32:
        case LD_COFF_REL_AMD64_REL32_1:
        case LD_COFF_REL_AMD64_REL32_2:
        case LD_COFF_REL_AMD64_REL32_3:
        case LD_COFF_REL_AMD64_REL32_4:
        case LD_COFF_REL_AMD64_REL32_5:
        case LD_COFF_REL_AMD64_SECTION:
        case LD_COFF_REL_AMD64_SECREL:
            return true;
        default:
            return false;
    }
}

static void check_object_capabilities(ld_coff_context_t *context,
                                      const char *input_name) {
    for (size_t i = 0; i < context->object_count; i++) {
        ld_coff_object_t *object = context->objects[i];
        if (object->import_object) continue;
        for (size_t j = 0; j < object->section_count; j++) {
            ld_coff_section_t *section = &object->sections[j];
            if ((section->characteristics & LD_COFF_SCN_LNK_COMDAT) != 0U) {
                if (section->comdat_selection <
                            LD_COFF_COMDAT_NODUPLICATES ||
                    section->comdat_selection > LD_COFF_COMDAT_LARGEST) {
                    fprintf(stderr,
                            "%s: %s(%s) uses unsupported COMDAT selection "
                            "%u\n",
                            input_name, object->display_name, section->name,
                            section->comdat_selection);
                    assert(false);
                }
                if (section->comdat_selection ==
                            LD_COFF_COMDAT_ASSOCIATIVE &&
                    (section->associative_section == 0U ||
                     section->associative_section > object->section_count)) {
                    fprintf(stderr,
                            "%s: %s(%s) has invalid associative parent %u\n",
                            input_name, object->display_name, section->name,
                            section->associative_section);
                    assert(false);
                }
            }
            for (size_t k = 0; k < section->relocation_count; k++) {
                if (!relocation_supported(section->relocations[k].type)) {
                    fprintf(stderr,
                            "%s: %s(%s) uses unsupported AMD64 relocation "
                            "0x%04x\n",
                            input_name, object->display_name, section->name,
                            section->relocations[k].type);
                    assert(false);
                }
            }
            if (strcmp(section->name, ".drectve") == 0) {
                int status = ld_coff_parse_directives(context, section);
                if (status != LD_OK)
                    fprintf(stderr, "%s: %s directive scan failed\n",
                            input_name, object->display_name);
                assert(status == LD_OK);
            }
        }
    }
}

static bool object_has_section(const ld_coff_object_t *object,
                               const char *name) {
    for (size_t i = 0; i < object->section_count; i++)
        if (strcmp(object->sections[i].name, name) == 0) return true;
    return false;
}

static void check_runtime_aco_unwind(const ld_coff_context_t *context) {
    bool found = false;
    for (size_t i = 0; i < context->object_count; i++) {
        const ld_coff_object_t *object = context->objects[i];
        if (!object->display_name ||
            !strstr(object->display_name, "acosw.S.obj"))
            continue;
        found = true;
        assert(object_has_section(object, ".pdata"));
        assert(object_has_section(object, ".xdata"));
    }
    assert(found);
}

static void check_input(const char *name) {
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/lib/windows_amd64/%s",
                          NATURE_SOURCE_DIR, name);
    assert(length > 0 && (size_t) length < sizeof(path));

    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;

    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);
    int status = ld_coff_load_input(&context, path);
    if (status != LD_OK)
        fprintf(stderr, "sysroot input %s failed: %s\n", name,
                capture.message);
    assert(status == LD_OK);
    assert(context.object_count != 0U);
    check_object_capabilities(&context, name);
    if (strcmp(name, "libruntime.a") == 0)
        check_runtime_aco_unwind(&context);
    ld_coff_context_deinit(&context);
}

int main(void) {
    static const char *inputs[] = {
            "crt2.obj",
            "libruntime.a",
            "libmingw32.lib",
            "compiler_rt.lib",
            "libuv.a",
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
    for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++)
        check_input(inputs[i]);
    return 0;
}

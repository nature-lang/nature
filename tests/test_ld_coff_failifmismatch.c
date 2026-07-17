#include "src/ld/ld.h"
#include "src/ld/ld_coff_internal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char message[4096];
} diagnostic_capture_t;

static void diagnostic(void *context, ld_diag_level_t level,
                       const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static int parse(ld_coff_context_t *context, const char *source,
                 const char *directives) {
    ld_coff_object_t object = {0};
    object.display_name = (char *) source;
    ld_coff_section_t section = {0};
    section.object = &object;
    section.data = (const uint8_t *) directives;
    section.data_size = (uint32_t) strlen(directives);
    return ld_coff_parse_directives(context, &section);
}

static void test_matching_values(void) {
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);

    assert(parse(&context, "first.obj",
                 "/FAILIFMISMATCH:\"RuntimeLibrary=MT_StaticRelease\"") ==
           LD_OK);
    assert(parse(&context, "second.obj",
                 "-failifmismatch:RuntimeLibrary=MT_StaticRelease") == LD_OK);
    assert(parse(&context, "third.obj",
                 "/FAILIFMISMATCH:Version=1=compatible") == LD_OK);
    assert(context.mismatch_count == 2U);
    assert(strcmp(context.mismatches[0].key, "RuntimeLibrary") == 0);
    assert(strcmp(context.mismatches[0].value, "MT_StaticRelease") == 0);
    assert(strcmp(context.mismatches[0].source, "second.obj") == 0);
    assert(strcmp(context.mismatches[1].key, "Version") == 0);
    assert(strcmp(context.mismatches[1].value, "1=compatible") == 0);
    assert(capture.message[0] == '\0');

    ld_coff_context_deinit(&context);
}

static void test_conflicting_values(void) {
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);

    assert(parse(&context, "runtime-debug.obj",
                 "/FAILIFMISMATCH:\"RuntimeLibrary=Debug\"") == LD_OK);
    assert(parse(&context, "runtime-release.obj",
                 "/FAILIFMISMATCH:\"RuntimeLibrary=Release\"") ==
           LD_INVALID_INPUT);
    assert(strstr(capture.message,
                  "mismatch detected for 'RuntimeLibrary'"));
    assert(strstr(capture.message,
                  ">>> runtime-debug.obj has value Debug"));
    assert(strstr(capture.message,
                  ">>> runtime-release.obj has value Release"));
    assert(context.mismatch_count == 1U);
    assert(strcmp(context.mismatches[0].value, "Debug") == 0);

    ld_coff_context_deinit(&context);
}

static void test_malformed_argument(const char *argument) {
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);

    assert(parse(&context, "malformed.obj", argument) == LD_INVALID_INPUT);
    assert(strstr(capture.message, "/FAILIFMISMATCH"));
    assert(strstr(capture.message, "malformed.obj"));
    assert(context.mismatch_count == 0U);

    ld_coff_context_deinit(&context);
}

int main(void) {
    test_matching_values();
    test_conflicting_values();
    test_malformed_argument("/FAILIFMISMATCH:missing-equals");
    test_malformed_argument("/FAILIFMISMATCH:=missing-key");
    test_malformed_argument("/FAILIFMISMATCH:missing-value=");
    test_malformed_argument("/FAILIFMISMATCH");
    return 0;
}

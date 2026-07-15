#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_script.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_script_parser(void) {
    static const char source[] =
            "/* GNU ld script */\n"
            "OUTPUT_FORMAT(elf64-littleaarch64)\n"
            "OUTPUT_ARCH(aarch64);\n"
            "SEARCH_DIR(\"=/usr/lib\")\n"
            "GROUP ( first.a, AS_NEEDED ( -lm 'second path.a' ) )\n"
            "INPUT(../third\\ path.o)\n";
    ld_elf_script_t script;
    ld_elf_script_error_t error;
    assert(ld_elf_script_parse((const uint8_t *) source,
                               sizeof(source) - 1U, LD_ARCH_ARM64,
                               &script, &error) == LD_ELF_SCRIPT_OK);
    assert(error.message == NULL);
    assert(script.search_dir_count == 1U);
    assert(strcmp(script.search_dirs[0], "=/usr/lib") == 0);
    assert(script.input_count == 4U);
    assert(strcmp(script.inputs[0].path, "first.a") == 0);
    assert(!script.inputs[0].as_needed);
    assert(strcmp(script.inputs[1].path, "-lm") == 0);
    assert(script.inputs[1].as_needed);
    assert(strcmp(script.inputs[2].path, "second path.a") == 0);
    assert(script.inputs[2].as_needed);
    assert(strcmp(script.inputs[3].path, "../third path.o") == 0);
    assert(!script.inputs[3].as_needed);
    ld_elf_script_deinit(&script);

    static const char riscv_source[] =
            "OUTPUT_FORMAT(elf64-littleriscv) GROUP(libc.a)";
    assert(ld_elf_script_parse((const uint8_t *) riscv_source,
                               sizeof(riscv_source) - 1U,
                               LD_ARCH_RISCV64, &script,
                               &error) == LD_ELF_SCRIPT_OK);
    assert(script.input_count == 1U);
    ld_elf_script_deinit(&script);
}

static void expect_script_error(const uint8_t *source, size_t size,
                                ld_arch_t arch,
                                ld_elf_script_result_t expected,
                                const char *message) {
    ld_elf_script_t script;
    ld_elf_script_error_t error;
    assert(ld_elf_script_parse(source, size, arch, &script, &error) ==
           expected);
    assert(error.message != NULL);
    assert(strstr(error.message, message) != NULL);
    assert(script.inputs == NULL && script.input_count == 0U);
    assert(script.search_dirs == NULL && script.search_dir_count == 0U);
}

static void test_script_parser_failures(void) {
    static const char empty[] = " /* only a comment */ ";
    expect_script_error((const uint8_t *) empty, sizeof(empty) - 1U,
                        LD_ARCH_ARM64, LD_ELF_SCRIPT_INVALID,
                        "empty linker script");

    static const char bad_comment[] = "/* unterminated";
    expect_script_error((const uint8_t *) bad_comment,
                        sizeof(bad_comment) - 1U, LD_ARCH_ARM64,
                        LD_ELF_SCRIPT_INVALID, "unterminated block comment");

    static const char bad_command[] = "SECTIONS { }";
    expect_script_error((const uint8_t *) bad_command,
                        sizeof(bad_command) - 1U, LD_ARCH_ARM64,
                        LD_ELF_SCRIPT_INVALID, "unsupported");

    static const char missing_close[] = "GROUP(one.a";
    expect_script_error((const uint8_t *) missing_close,
                        sizeof(missing_close) - 1U, LD_ARCH_ARM64,
                        LD_ELF_SCRIPT_INVALID, "unterminated");

    static const char wrong_arch[] =
            "OUTPUT_FORMAT(elf64-x86-64) GROUP(one.a)";
    expect_script_error((const uint8_t *) wrong_arch,
                        sizeof(wrong_arch) - 1U, LD_ARCH_ARM64,
                        LD_ELF_SCRIPT_UNSUPPORTED_ARCH, "does not match");

    static const uint8_t embedded_nul[] = {
            'G', 'R', 'O', 'U', 'P', '(', 'a', '\0', 'b', ')'};
    expect_script_error(embedded_nul, sizeof(embedded_nul), LD_ARCH_ARM64,
                        LD_ELF_SCRIPT_INVALID, "NUL byte");
}

static void write_named_fixture(const char *path, const void *bytes,
                                size_t size) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    assert(fd >= 0);
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = write(fd, (const uint8_t *) bytes + offset,
                              size - offset);
        assert(count > 0);
        offset += (size_t) count;
    }
    assert(close(fd) == 0);
}

static void test_script_relative_and_library_inputs(void) {
    char directory[] = "/tmp/nature-ld-elf-script-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char object_path[PATH_MAX];
    char script_path[PATH_MAX];
    char output_path[PATH_MAX];
    int length = snprintf(object_path, sizeof(object_path), "%s/libentry.a",
                          directory);
    assert(length > 0 && (size_t) length < sizeof(object_path));
    length = snprintf(script_path, sizeof(script_path), "%s/libwrapper.a",
                      directory);
    assert(length > 0 && (size_t) length < sizeof(script_path));
    length = snprintf(output_path, sizeof(output_path), "%s/output",
                      directory);
    assert(length > 0 && (size_t) length < sizeof(output_path));

    size_t object_size;
    uint8_t *object = make_test_elf_object(
            LD_ELF_EM_AARCH64, false, false, &object_size);
    write_named_fixture(object_path, object, object_size);
    free(object);

    static const char source[] =
            "OUTPUT_FORMAT(elf64-littleaarch64)\n"
            "SEARCH_DIR(.)\n"
            "GROUP(-lentry)\n";
    write_named_fixture(script_path, source, sizeof(source) - 1U);

    diagnostic_capture_t capture = {0};
    const char *inputs[] = {script_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == UINT32_C(0xd65f03c0));

    unlink(output_path);
    unlink(script_path);
    unlink(object_path);
    assert(rmdir(directory) == 0);
}

static void test_script_cycle_and_missing_input(void) {
    char cycle_path[] = "/tmp/nature-ld-elf-script-cycle-XXXXXX";
    int fd = mkstemp(cycle_path);
    assert(fd >= 0);
    assert(close(fd) == 0);
    char source[PATH_MAX + 32U];
    int length = snprintf(source, sizeof(source), "GROUP(%s)\n", cycle_path);
    assert(length > 0 && (size_t) length < sizeof(source));
    write_named_fixture(cycle_path, source, (size_t) length);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t context;
    ld_elf_context_init(&context, &options);
    assert(ld_elf_load_input(&context, cycle_path) == LD_INVALID_INPUT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "script cycle") != NULL);
    ld_elf_context_deinit(&context);
    ld_options_deinit(&options);
    unlink(cycle_path);

    char missing_path[] = "/tmp/nature-ld-elf-script-missing-XXXXXX";
    fd = mkstemp(missing_path);
    assert(fd >= 0);
    static const char missing[] = "INPUT(no-such-input.o)\n";
    assert(write(fd, missing, sizeof(missing) - 1U) ==
           (ssize_t) (sizeof(missing) - 1U));
    assert(close(fd) == 0);

    memset(&capture, 0, sizeof(capture));
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_init(&context, &options);
    assert(ld_elf_load_input(&context, missing_path) == LD_IO_ERROR);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "no-such-input.o") != NULL);
    assert(strstr(capture.message, missing_path) != NULL);
    ld_elf_context_deinit(&context);
    ld_options_deinit(&options);
    unlink(missing_path);
}

void test_ld_elf_script(void) {
    test_script_parser();
    test_script_parser_failures();
    test_script_relative_and_library_inputs();
    test_script_cycle_and_missing_input();
}

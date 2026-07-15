#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_archive.h"
#include "src/ld/ld_elf_internal.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_archive_write_header(uint8_t *header, const char *name,
                                      size_t member_size) {
    assert(header && name && strlen(name) <= LD_ELF_AR_NAME_SIZE);
    memset(header, ' ', LD_ELF_AR_HEADER_SIZE);
    memcpy(header, name, strlen(name));
    char decimal[32];
    int length = snprintf(decimal, sizeof(decimal), "%zu", member_size);
    assert(length > 0 && length <= 10);
    memcpy(header + 48U, decimal, (size_t) length);
    memcpy(header + 58U, "`\n", 2U);
}

static void test_thin_archive_append(uint8_t *archive, size_t capacity,
                                     size_t *cursor, const char *name,
                                     size_t external_size) {
    assert(archive && cursor && *cursor <= capacity);
    assert(LD_ELF_AR_HEADER_SIZE <= capacity - *cursor);
    test_archive_write_header(archive + *cursor, name, external_size);
    *cursor += LD_ELF_AR_HEADER_SIZE;
}

static void write_exact_fixture(const char *path, const void *bytes,
                                size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = write(fd, (const uint8_t *) bytes + offset,
                              size - offset);
        if (count < 0 && errno == EINTR) continue;
        assert(count > 0);
        offset += (size_t) count;
    }
    assert(close(fd) == 0);
}

static void make_fixture_directory(char path[]) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);
    assert(mkdir(path, 0700) == 0);
}

static void test_elf_archive_wire_decoder(void) {
    uint8_t thin[LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_HEADER_SIZE] = {0};
    memcpy(thin, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    test_archive_write_header(thin + LD_ELF_AR_MAGIC_SIZE, "member.o/",
                              12345U);

    ld_elf_archive_record_t record;
    assert(ld_elf_archive_record_at(
                   thin, sizeof(thin), LD_ELF_AR_MAGIC_SIZE, true,
                   &record) == LD_ELF_ARCHIVE_OK);
    assert(record.kind == LD_ELF_ARCHIVE_MEMBER_REGULAR);
    assert(!record.payload_embedded);
    assert(record.payload_size == 12345U);
    assert(record.next_offset == sizeof(thin));
    assert(ld_elf_archive_record_at(thin, sizeof(thin), sizeof(thin), true,
                                    &record) == LD_ELF_ARCHIVE_END);
    assert(ld_elf_archive_record_at(
                   thin, sizeof(thin), LD_ELF_AR_MAGIC_SIZE, false,
                   &record) == LD_ELF_ARCHIVE_MEMBER_OUT_OF_RANGE);

    static const char gnu_names[] = "first-member.o/\nsecond-member.o/\n";
    test_archive_write_header(thin + LD_ELF_AR_MAGIC_SIZE, "/16", 12345U);
    thin[LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_NAME_SIZE - 1U] = '/';
    assert(ld_elf_archive_record_at(
                   thin, sizeof(thin), LD_ELF_AR_MAGIC_SIZE, true,
                   &record) == LD_ELF_ARCHIVE_OK);
    char *member_name = NULL;
    size_t object_offset = 0U;
    size_t object_size = 0U;
    assert(ld_elf_archive_member_name(
                   thin, sizeof(thin), &record, gnu_names,
                   sizeof(gnu_names) - 1U, true, &member_name,
                   &object_offset, &object_size) == LD_ELF_ARCHIVE_OK);
    assert(strcmp(member_name, "second-member.o") == 0);
    assert(object_size == 12345U);
    free(member_name);

    char *path = NULL;
    assert(ld_elf_archive_resolve_member_path(
                   "/tmp/archive-dir/libthin.a", "../objects/member.o",
                   &path) == LD_ELF_ARCHIVE_OK);
    assert(strcmp(path, "/tmp/archive-dir/../objects/member.o") == 0);
    free(path);
    path = NULL;
    assert(ld_elf_archive_resolve_member_path(
                   "/tmp/archive-dir/libthin.a", "/opt/objects/member.o",
                   &path) == LD_ELF_ARCHIVE_OK);
    assert(strcmp(path, "/opt/objects/member.o") == 0);
    free(path);
}

static void test_elf_archive_names(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_AARCH64, false, false,
                                           &object_size);
    static const char gnu_member_name[] = "gnu_long_archive_member.o";
    static const char gnu_names[] = "gnu_long_archive_member.o/\n";
    size_t capacity = LD_ELF_AR_MAGIC_SIZE +
                      2U * LD_ELF_AR_HEADER_SIZE + sizeof(gnu_names) +
                      object_size + 4U;
    uint8_t *archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t cursor = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, capacity, &cursor, "//",
                        (const uint8_t *) gnu_names,
                        sizeof(gnu_names) - 1U);
    test_archive_append(archive, capacity, &cursor, "/0", object,
                        object_size);
    char gnu_path[] = "/tmp/nature-ld-gnu-archive-XXXXXX";
    write_fixture(gnu_path, archive, cursor);
    free(archive);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, gnu_path) == LD_OK);
    assert(ctx.archives.count == 1U && ctx.objects.count == 1U);
    assert(ctx.archives.items[0]->member_count == 1U);
    assert(!ctx.archives.items[0]->thin);
    assert(ctx.archives.items[0]->selected_member_count == 0U);
    ld_elf_object_t *member = ctx.archives.items[0]->members[0];
    assert(strcmp(member->member_name, gnu_member_name) == 0);
    assert(member->archive_member && member->lazy && !member->selected);
    ld_elf_context_deinit(&ctx);
    unlink(gnu_path);

    static const char bsd_member_name[] = "bsd_long_archive_member.o";
    capacity = LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_HEADER_SIZE +
               sizeof(bsd_member_name) + object_size + 4U;
    archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    cursor = LD_ELF_AR_MAGIC_SIZE;
    char extended_name[LD_ELF_AR_NAME_SIZE + 1U];
    int name_length = snprintf(extended_name, sizeof(extended_name), "#1/%zu",
                               sizeof(bsd_member_name) - 1U);
    assert(name_length > 0 &&
           (unsigned) name_length <= LD_ELF_AR_NAME_SIZE);
    size_t bsd_payload_size = sizeof(bsd_member_name) - 1U + object_size;
    uint8_t *bsd_payload = malloc(bsd_payload_size);
    assert(bsd_payload != NULL);
    memcpy(bsd_payload, bsd_member_name, sizeof(bsd_member_name) - 1U);
    memcpy(bsd_payload + sizeof(bsd_member_name) - 1U, object, object_size);
    test_archive_append(archive, capacity, &cursor, extended_name, bsd_payload,
                        bsd_payload_size);
    free(bsd_payload);
    free(object);
    char bsd_path[] = "/tmp/nature-ld-bsd-archive-XXXXXX";
    write_fixture(bsd_path, archive, cursor);
    free(archive);

    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, bsd_path) == LD_OK);
    assert(ctx.archives.count == 1U && ctx.objects.count == 1U);
    assert(ctx.archives.items[0]->member_count == 1U);
    member = ctx.archives.items[0]->members[0];
    assert(strcmp(member->member_name, bsd_member_name) == 0);
    assert(member->archive_member && member->lazy && !member->selected);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(bsd_path);
}

static void test_elf_thin_archive_link(void) {
    char directory[] = "/tmp/nature-ld-thin-link-XXXXXX";
    make_fixture_directory(directory);
    char object_directory[PATH_MAX];
    int length = snprintf(object_directory, sizeof(object_directory),
                          "%s/objects", directory);
    assert(length > 0 && (size_t) length < sizeof(object_directory));
    assert(mkdir(object_directory, 0700) == 0);

    static const char member_name[] =
            "objects/provider_with_a_name_longer_than_fifteen.o";
    char object_path[PATH_MAX];
    length = snprintf(object_path, sizeof(object_path), "%s/%s", directory,
                      member_name);
    assert(length > 0 && (size_t) length < sizeof(object_path));
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_AARCH64, false, false,
                                           &object_size);
    write_exact_fixture(object_path, object, object_size);
    free(object);

    size_t names_size = sizeof(member_name) + 2U;
    char *names = malloc(names_size);
    assert(names != NULL);
    int names_length = snprintf(names, names_size, "%s/\n", member_name);
    assert(names_length > 0 && (size_t) names_length < names_size);
    size_t capacity = LD_ELF_AR_MAGIC_SIZE +
                      2U * LD_ELF_AR_HEADER_SIZE + names_size + 2U;
    uint8_t *archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t cursor = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, capacity, &cursor, "//",
                        (const uint8_t *) names, (size_t) names_length);
    free(names);
    test_thin_archive_append(archive, capacity, &cursor, "/0",
                             object_size + 17U);

    char archive_path[PATH_MAX];
    length = snprintf(archive_path, sizeof(archive_path), "%s/libthin.a",
                      directory);
    assert(length > 0 && (size_t) length < sizeof(archive_path));
    write_exact_fixture(archive_path, archive, cursor);
    free(archive);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, archive_path) == LD_OK);
    assert(ctx.archives.count == 1U && ctx.archives.items[0]->thin);
    assert(ctx.objects.count == 1U && ctx.archives.items[0]->member_count == 1U);
    ld_elf_object_t *member = ctx.archives.items[0]->members[0];
    assert(strcmp(member->member_name, member_name) == 0);
    assert(strcmp(member->file->path, object_path) == 0);
    assert(strstr(member->display_name, "libthin.a(") != NULL);
    assert(member->lazy && !member->selected);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);

    char output_path[PATH_MAX];
    length = snprintf(output_path, sizeof(output_path), "%s/output",
                      directory);
    assert(length > 0 && (size_t) length < sizeof(output_path));
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {archive_path};
    int status = link_test_elf_inputs(output_path, inputs, 1U, &capture);
    if (status != LD_OK) {
        fprintf(stderr, "thin archive link failed (%d): %s\n", status,
                capture.message);
    }
    assert(status == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == 0xd65f03c0U);

    unlink(output_path);
    unlink(archive_path);
    unlink(object_path);
    rmdir(object_directory);
    rmdir(directory);
}

static void test_elf_thin_archive_missing_member(void) {
    static const char names[] = "missing/member.o/\n";
    size_t capacity = LD_ELF_AR_MAGIC_SIZE +
                      2U * LD_ELF_AR_HEADER_SIZE + sizeof(names) + 2U;
    uint8_t *archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t cursor = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, capacity, &cursor, "//",
                        (const uint8_t *) names, sizeof(names) - 1U);
    test_thin_archive_append(archive, capacity, &cursor, "/0", 4096U);
    char archive_path[] = "/tmp/nature-ld-thin-missing-XXXXXX";
    write_fixture(archive_path, archive, cursor);
    free(archive);

    static const char output_path[] = "/tmp/nature-ld-thin-missing-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {archive_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) ==
           LD_IO_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, archive_path) != NULL);
    assert(strstr(capture.message, "missing/member.o") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(archive_path);
}

static void test_elf_thin_archive_architecture_mismatch(void) {
    char directory[] = "/tmp/nature-ld-thin-arch-XXXXXX";
    make_fixture_directory(directory);
    static const char member_name[] = "wrong-architecture.o";
    char object_path[PATH_MAX];
    int length = snprintf(object_path, sizeof(object_path), "%s/%s",
                          directory, member_name);
    assert(length > 0 && (size_t) length < sizeof(object_path));
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_X86_64, false, false,
                                           &object_size);
    write_exact_fixture(object_path, object, object_size);
    free(object);

    static const char names[] = "wrong-architecture.o/\n";
    size_t capacity = LD_ELF_AR_MAGIC_SIZE +
                      2U * LD_ELF_AR_HEADER_SIZE + sizeof(names) + 2U;
    uint8_t *archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t cursor = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, capacity, &cursor, "//",
                        (const uint8_t *) names, sizeof(names) - 1U);
    test_thin_archive_append(archive, capacity, &cursor, "/0", object_size);
    char archive_path[PATH_MAX];
    length = snprintf(archive_path, sizeof(archive_path), "%s/libthin.a",
                      directory);
    assert(length > 0 && (size_t) length < sizeof(archive_path));
    write_exact_fixture(archive_path, archive, cursor);
    free(archive);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, archive_path) == LD_UNSUPPORTED);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "architecture mismatch") != NULL);
    assert(strstr(capture.message, "libthin.a(wrong-architecture.o)") !=
           NULL);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);

    unlink(archive_path);
    unlink(object_path);
    rmdir(directory);
}

static void expect_invalid_thin_archive(const uint8_t *archive,
                                        size_t archive_size,
                                        const char *diagnostic) {
    char archive_path[] = "/tmp/nature-ld-thin-invalid-XXXXXX";
    write_fixture(archive_path, archive, archive_size);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, archive_path) == LD_INVALID_INPUT);
    assert(capture.count > 0U);
    assert(strstr(capture.message, diagnostic) != NULL);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(archive_path);
}

static void test_elf_thin_archive_malformed(void) {
    static const char names[] = "long-member-name.o/\n";
    size_t capacity = LD_ELF_AR_MAGIC_SIZE +
                      2U * LD_ELF_AR_HEADER_SIZE + sizeof(names) + 2U;
    uint8_t *archive = calloc(1, capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t cursor = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, capacity, &cursor, "//",
                        (const uint8_t *) names, sizeof(names) - 1U);
    test_thin_archive_append(archive, capacity, &cursor, "/1", 100U);
    expect_invalid_thin_archive(archive, cursor, "points into an entry");
    free(archive);

    uint8_t truncated[LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_HEADER_SIZE + 3U] = {0};
    memcpy(truncated, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    test_archive_write_header(truncated + LD_ELF_AR_MAGIC_SIZE, "//", 4U);
    memcpy(truncated + LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_HEADER_SIZE, "abc",
           3U);
    expect_invalid_thin_archive(truncated, sizeof(truncated),
                                "extends past end of file");

    uint8_t bsd[LD_ELF_AR_MAGIC_SIZE + LD_ELF_AR_HEADER_SIZE] = {0};
    memcpy(bsd, LD_ELF_AR_THIN_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    test_archive_write_header(bsd + LD_ELF_AR_MAGIC_SIZE, "#1/8", 100U);
    expect_invalid_thin_archive(
            bsd, sizeof(bsd),
            "BSD extended names are not supported in a thin archive");
}

void test_ld_elf_archive(void) {
    test_elf_archive_wire_decoder();
    test_elf_archive_names();
    test_elf_thin_archive_link();
    test_elf_thin_archive_missing_member();
    test_elf_thin_archive_architecture_mismatch();
    test_elf_thin_archive_malformed();
}

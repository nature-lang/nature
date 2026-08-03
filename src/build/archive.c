#include "archive.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    BUILD_ARCHIVE_HEADER_SIZE = 60,
    BUILD_ARCHIVE_COPY_BUFFER_SIZE = 64 * 1024,
};

static const char build_archive_magic[] = "!<arch>\n";

static bool build_archive_fail(char *error, size_t capacity,
                               const char *format, ...) {
    if (error && capacity) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, capacity, format, args);
        va_end(args);
        error[capacity - 1U] = '\0';
    }
    return false;
}

static const char *build_archive_basename(const char *path) {
    const char *name = path;
    for (const char *cursor = path; cursor && *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
    }
    return name;
}

static bool build_archive_field(char *header, size_t offset, size_t width,
                                const char *value) {
    size_t length = strlen(value);
    if (length > width) return false;
    memset(header + offset, ' ', width);
    memcpy(header + offset, value, length);
    return true;
}

static bool build_archive_write_all(FILE *file, const void *bytes,
                                    size_t size) {
    const uint8_t *cursor = bytes;
    while (size) {
        size_t written = fwrite(cursor, 1U, size, file);
        if (!written) return false;
        cursor += written;
        size -= written;
    }
    return true;
}

static bool build_archive_write_member(FILE *archive, const char *path,
                                       uint8_t *copy_buffer,
                                       char *error, size_t error_capacity) {
    const char *name = build_archive_basename(path);
    if (!name || !*name)
        return build_archive_fail(error, error_capacity,
                                  "archive member path has no file name: '%s'",
                                  path ? path : "(null)");

    FILE *input = fopen(path, "rb");
    if (!input)
        return build_archive_fail(error, error_capacity,
                                  "cannot open archive member '%s': %s", path,
                                  strerror(errno));
    if (fseek(input, 0, SEEK_END) != 0) {
        int saved = errno;
        fclose(input);
        return build_archive_fail(error, error_capacity,
                                  "cannot size archive member '%s': %s", path,
                                  strerror(saved));
    }
    long file_length = ftell(input);
    if (file_length < 0 || fseek(input, 0, SEEK_SET) != 0) {
        int saved = errno ? errno : EIO;
        fclose(input);
        return build_archive_fail(error, error_capacity,
                                  "cannot size archive member '%s': %s", path,
                                  strerror(saved));
    }

    size_t name_length = strlen(name);
    uint64_t payload_size = (uint64_t) file_length + name_length;
    if (payload_size > UINT64_C(9999999999)) {
        fclose(input);
        return build_archive_fail(error, error_capacity,
                                  "archive member '%s' is too large", path);
    }

    char encoded_name[32];
    char encoded_size[32];
    int name_count = snprintf(encoded_name, sizeof(encoded_name), "#1/%zu",
                              name_length);
    int size_count = snprintf(encoded_size, sizeof(encoded_size), "%llu",
                              (unsigned long long) payload_size);
    char header[BUILD_ARCHIVE_HEADER_SIZE];
    memset(header, ' ', sizeof(header));
    bool valid_header = name_count > 0 &&
                        (size_t) name_count < sizeof(encoded_name) &&
                        size_count > 0 &&
                        (size_t) size_count < sizeof(encoded_size) &&
                        build_archive_field(header, 0, 16, encoded_name) &&
                        build_archive_field(header, 16, 12, "0") &&
                        build_archive_field(header, 28, 6, "0") &&
                        build_archive_field(header, 34, 6, "0") &&
                        build_archive_field(header, 40, 8, "100644") &&
                        build_archive_field(header, 48, 10, encoded_size);
    if (!valid_header) {
        fclose(input);
        return build_archive_fail(error, error_capacity,
                                  "archive member name is too long: '%s'", name);
    }
    header[58] = '`';
    header[59] = '\n';

    bool success = build_archive_write_all(archive, header, sizeof(header)) &&
                   build_archive_write_all(archive, name, name_length);
    while (success) {
        size_t read_count = fread(copy_buffer, 1U,
                                  BUILD_ARCHIVE_COPY_BUFFER_SIZE, input);
        if (read_count &&
            !build_archive_write_all(archive, copy_buffer, read_count)) {
            success = false;
            break;
        }
        if (read_count < BUILD_ARCHIVE_COPY_BUFFER_SIZE) {
            if (ferror(input)) success = false;
            break;
        }
    }
    int saved = 0;
    if (!success) saved = errno ? errno : EIO;
    if (fclose(input) != 0) {
        if (!saved) saved = errno ? errno : EIO;
        success = false;
    }
    if (success && (payload_size & 1U)) {
        const char padding = '\n';
        success = build_archive_write_all(archive, &padding, 1U);
        if (!success) saved = errno ? errno : EIO;
    }
    if (!success)
        return build_archive_fail(error, error_capacity,
                                  "cannot write archive member '%s': %s", path,
                                  strerror(saved));
    return true;
}

bool build_archive_write(const char *path,
                         const char *const *member_paths,
                         size_t member_count,
                         char *error,
                         size_t error_capacity) {
    if (error && error_capacity) error[0] = '\0';
    if (!path || !*path || (!member_paths && member_count))
        return build_archive_fail(error, error_capacity,
                                  "invalid archive writer arguments");

    FILE *archive = fopen(path, "wb");
    if (!archive)
        return build_archive_fail(error, error_capacity,
                                  "cannot create archive '%s': %s", path,
                                  strerror(errno));
    uint8_t *copy_buffer = malloc(BUILD_ARCHIVE_COPY_BUFFER_SIZE);
    bool success = copy_buffer &&
                   build_archive_write_all(archive, build_archive_magic,
                                           sizeof(build_archive_magic) - 1U);
    if (!copy_buffer)
        build_archive_fail(error, error_capacity,
                           "out of memory writing archive '%s'", path);
    for (size_t i = 0; success && i < member_count; i++) {
        if (!member_paths[i] || !*member_paths[i]) {
            success = build_archive_fail(
                    error, error_capacity,
                    "archive member %zu has an empty path", i);
            break;
        }
        success = build_archive_write_member(archive, member_paths[i],
                                             copy_buffer, error,
                                             error_capacity);
    }
    free(copy_buffer);
    if (fclose(archive) != 0 && success)
        success = build_archive_fail(error, error_capacity,
                                     "cannot finalize archive '%s': %s", path,
                                     strerror(errno));
    if (!success) remove(path);
    return success;
}

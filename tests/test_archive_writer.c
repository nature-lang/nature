#include "src/build/archive.h"
#include "src/build/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_fixture(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

static unsigned char *read_fixture(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    unsigned char *bytes = malloc((size_t) length + 1U);
    assert(bytes);
    assert(fread(bytes, 1U, (size_t) length, file) == (size_t) length);
    assert(fclose(file) == 0);
    *size = (size_t) length;
    return bytes;
}

static size_t decimal_field(const unsigned char *bytes, size_t length) {
    size_t result = 0;
    for (size_t i = 0; i < length && bytes[i] != ' '; i++) {
        assert(bytes[i] >= '0' && bytes[i] <= '9');
        result = result * 10U + (size_t) (bytes[i] - '0');
    }
    return result;
}

int main(void) {
    char *directory = temp_dir();
    assert(directory);
    char first[1024], second[1024], archive_a[1024], archive_b[1024];
    snprintf(first, sizeof(first), "%s/a.obj", directory);
    snprintf(second, sizeof(second),
             "%s/member-with-a-name-longer-than-fifteen.obj", directory);
    snprintf(archive_a, sizeof(archive_a), "%s/first.a", directory);
    snprintf(archive_b, sizeof(archive_b), "%s/second.a", directory);
    static const unsigned char first_data[] = {0x64, 0x86, 0x01};
    static const unsigned char second_data[] = {1, 2, 3, 4};
    write_fixture(first, first_data, sizeof(first_data));
    write_fixture(second, second_data, sizeof(second_data));
    const char *members[] = {first, second};
    char error[512];
    assert(build_archive_write(archive_a, members, 2U, error,
                               sizeof(error)));
    assert(build_archive_write(archive_b, members, 2U, error,
                               sizeof(error)));

    size_t size_a, size_b;
    unsigned char *bytes_a = read_fixture(archive_a, &size_a);
    unsigned char *bytes_b = read_fixture(archive_b, &size_b);
    assert(size_a == size_b && memcmp(bytes_a, bytes_b, size_a) == 0);
    assert(size_a >= 8U && memcmp(bytes_a, "!<arch>\n", 8U) == 0);

    size_t offset = 8U;
    const char *expected_names[] = {
            "a.obj", "member-with-a-name-longer-than-fifteen.obj"};
    const unsigned char *expected_data[] = {first_data, second_data};
    const size_t expected_sizes[] = {sizeof(first_data), sizeof(second_data)};
    for (size_t i = 0; i < 2U; i++) {
        assert(offset + 60U <= size_a);
        const unsigned char *header = bytes_a + offset;
        assert(memcmp(header, "#1/", 3U) == 0);
        assert(memcmp(header + 16U, "0", 1U) == 0);
        assert(memcmp(header + 40U, "100644", 6U) == 0);
        assert(header[58] == '`' && header[59] == '\n');
        size_t payload_size = decimal_field(header + 48U, 10U);
        size_t name_size = strlen(expected_names[i]);
        assert(payload_size == name_size + expected_sizes[i]);
        offset += 60U;
        assert(offset + payload_size <= size_a);
        assert(memcmp(bytes_a + offset, expected_names[i], name_size) == 0);
        assert(memcmp(bytes_a + offset + name_size, expected_data[i],
                      expected_sizes[i]) == 0);
        offset += payload_size;
        if (offset & 1U) {
            assert(offset < size_a);
            assert(bytes_a[offset] == '\n');
            offset++;
        }
    }
    assert(offset == size_a);
    free(bytes_a);
    free(bytes_b);

    const char *missing[] = {"this-archive-member-does-not-exist"};
    assert(!build_archive_write(archive_a, missing, 1U, error,
                                sizeof(error)));
    assert(strstr(error, "cannot open archive member"));
    free(directory);
    return 0;
}

#include "src/build/windows_linker.h"
#include "src/build/config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void touch_file(const char *path, int mode) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fclose(file) == 0);
    assert(chmod(path, (mode_t) mode) == 0);
}

static void make_directory(const char *path) {
#ifdef __WINDOWS
    assert(_mkdir(path) == 0);
#else
    assert(mkdir(path, 0700) == 0);
#endif
}

static char *read_stream(FILE *file) {
    assert(fflush(file) == 0);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t) length + 1U, 1U);
    assert(text);
    assert(fread(text, 1U, (size_t) length, file) == (size_t) length);
    return text;
}

static void test_command_search(const char *directory) {
    char first[512], second[512], executable[512], windows_path[1100];
    snprintf(first, sizeof(first), "%s/first", directory);
    snprintf(second, sizeof(second), "%s/second", directory);
    make_directory(first);
    make_directory(second);
    /* Match PATHEXT exactly so this Windows-path fixture is deterministic on
     * case-sensitive Unix filesystems as well as Windows. */
    snprintf(executable, sizeof(executable), "%s/lld-link.EXE", second);
    touch_file(executable, 0600);
    snprintf(windows_path, sizeof(windows_path), "\"%s\";%s", first,
             second);
    assert(build_command_exists_in_path("lld-link", windows_path,
                                        ".EXE;.CMD", true));
    assert(!build_command_exists_in_path("missing", windows_path,
                                         ".EXE;.CMD", true));
    assert(build_command_exists_in_path(executable, NULL, ".EXE", true));

    snprintf(executable, sizeof(executable), "%s/ld", first);
    touch_file(executable, 0700);
    snprintf(windows_path, sizeof(windows_path), "%s:%s", second, first);
    assert(build_command_exists_in_path("ld", windows_path, NULL, false));
}

static void test_response_libraries(const char *directory) {
    char optional[512];
    snprintf(optional, sizeof(optional), "%s/libmbedtls.a", directory);
    touch_file(optional, 0600);
    FILE *response = tmpfile();
    assert(response);
    assert(windows_linker_write_default_libraries(response, directory));
    char *text = read_stream(response);
    assert(strstr(text, "libruntime.a\nlibuv.a\nlibmbedtls.a\n"));
    assert(!strstr(text, "libmbedx509.a\n"));
    assert(!strstr(text, "libmbedcrypto.a\n"));
    assert(strstr(text, "libmingw32.lib\ncompiler_rt.lib\nucrtbase.lib\n"));
    assert(strstr(text, "kernel32.lib\nntdll.lib\n"));
    free(text);
    assert(fclose(response) == 0);
}

int main(void) {
    char *directory = temp_dir();
    assert(directory);
    test_command_search(directory);
    test_response_libraries(directory);
    free(directory);
    return 0;
}

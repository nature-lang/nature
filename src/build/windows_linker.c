#include "windows_linker.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const windows_sysroot_library_t WINDOWS_SYSROOT_LIBRARIES[] = {
        {"libruntime.a", true},
        {"libuv.a", true},
        {"libmbedtls.a", false},
        {"libmbedx509.a", false},
        {"libmbedcrypto.a", false},
        {"libmingw32.lib", true},
        {"compiler_rt.lib", true},
        {"ucrtbase.lib", true},
        {"api-ms-win-crt-conio-l1-1-0.lib", true},
        {"api-ms-win-crt-convert-l1-1-0.lib", true},
        {"api-ms-win-crt-environment-l1-1-0.lib", true},
        {"api-ms-win-crt-filesystem-l1-1-0.lib", true},
        {"api-ms-win-crt-heap-l1-1-0.lib", true},
        {"api-ms-win-crt-locale-l1-1-0.lib", true},
        {"api-ms-win-crt-math-l1-1-0.lib", true},
        {"api-ms-win-crt-multibyte-l1-1-0.lib", true},
        {"api-ms-win-crt-private-l1-1-0.lib", true},
        {"api-ms-win-crt-process-l1-1-0.lib", true},
        {"api-ms-win-crt-runtime-l1-1-0.lib", true},
        {"api-ms-win-crt-stdio-l1-1-0.lib", true},
        {"api-ms-win-crt-string-l1-1-0.lib", true},
        {"api-ms-win-crt-time-l1-1-0.lib", true},
        {"api-ms-win-crt-utility-l1-1-0.lib", true},
        {"kernel32.lib", true},
        {"ntdll.lib", true},
        {"advapi32.lib", true},
        {"user32.lib", true},
        {"shell32.lib", true},
        {"ws2_32.lib", true},
        {"bcrypt.lib", true},
        {"iphlpapi.lib", true},
        {"userenv.lib", true},
        {"psapi.lib", true},
        {"dbghelp.lib", true},
        {"ole32.lib", true},
        {"secur32.lib", true},
        {"crypt32.lib", true},
        {"winmm.lib", true},
        {"version.lib", true},
};

const windows_sysroot_library_t *windows_sysroot_libraries(size_t *count) {
    if (count)
        *count = sizeof(WINDOWS_SYSROOT_LIBRARIES) /
                 sizeof(WINDOWS_SYSROOT_LIBRARIES[0]);
    return WINDOWS_SYSROOT_LIBRARIES;
}

static bool regular_file(const char *path, bool require_executable) {
    struct stat info;
    if (!path || stat(path, &info) != 0 || (info.st_mode & S_IFREG) == 0)
        return false;
#ifdef __WINDOWS
    (void) require_executable;
    return true;
#else
    return !require_executable || access(path, X_OK) == 0;
#endif
}

static bool has_path_separator(const char *command) {
    return strchr(command, '/') != NULL || strchr(command, '\\') != NULL;
}

static bool has_file_extension(const char *command) {
    const char *base = command;
    for (const char *cursor = command; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1U;
    return strchr(base, '.') != NULL;
}

static bool try_windows_extensions(const char *base, const char *pathext) {
    if (regular_file(base, false)) return true;
    if (has_file_extension(base)) return false;
    if (!pathext || !*pathext) pathext = ".COM;.EXE;.BAT;.CMD";
    const char *cursor = pathext;
    while (true) {
        const char *end = strchr(cursor, ';');
        size_t extension_length = end ? (size_t) (end - cursor)
                                      : strlen(cursor);
        if (extension_length != 0U &&
            strlen(base) <= SIZE_MAX - extension_length - 1U) {
            size_t length = strlen(base) + extension_length;
            char *candidate = malloc(length + 1U);
            if (!candidate) return false;
            memcpy(candidate, base, strlen(base));
            memcpy(candidate + strlen(base), cursor, extension_length);
            candidate[length] = '\0';
            bool found = regular_file(candidate, false);
            free(candidate);
            if (found) return true;
        }
        if (!end) break;
        cursor = end + 1U;
    }
    return false;
}

static bool try_command_candidate(const char *candidate, const char *pathext,
                                  bool windows_host) {
    return windows_host ? try_windows_extensions(candidate, pathext)
                        : regular_file(candidate, true);
}

bool build_command_exists_in_path(const char *command, const char *path,
                                  const char *pathext, bool windows_host) {
    if (!command || !*command) return false;
    if (has_path_separator(command))
        return try_command_candidate(command, pathext, windows_host);
    if (try_command_candidate(command, pathext, windows_host)) return true;
    if (!path) return false;

    char separator = windows_host ? ';' : ':';
    const char *cursor = path;
    while (true) {
        const char *end = strchr(cursor, separator);
        size_t directory_length = end ? (size_t) (end - cursor)
                                      : strlen(cursor);
        const char *directory = cursor;
        if (directory_length >= 2U && directory[0] == '"' &&
            directory[directory_length - 1U] == '"') {
            directory++;
            directory_length -= 2U;
        }
        if (directory_length == 0U) {
            directory = ".";
            directory_length = 1U;
        }
        size_t command_length = strlen(command);
        if (directory_length <= SIZE_MAX - command_length - 2U) {
            size_t length = directory_length + 1U + command_length;
            char *candidate = malloc(length + 1U);
            if (!candidate) return false;
            memcpy(candidate, directory, directory_length);
            candidate[directory_length] = '/';
            memcpy(candidate + directory_length + 1U, command,
                   command_length + 1U);
            bool found = try_command_candidate(candidate, pathext,
                                               windows_host);
            free(candidate);
            if (found) return true;
        }
        if (!end) break;
        cursor = end + 1U;
    }
    return false;
}

bool build_command_exists(const char *command) {
#ifdef __WINDOWS
    return build_command_exists_in_path(command, getenv("PATH"),
                                        getenv("PATHEXT"), true);
#else
    return build_command_exists_in_path(command, getenv("PATH"), NULL,
                                        false);
#endif
}

static bool sysroot_contains(const char *sysroot, const char *name) {
    size_t root_length = strlen(sysroot);
    size_t name_length = strlen(name);
    if (root_length > SIZE_MAX - name_length - 2U) return false;
    char *path = malloc(root_length + name_length + 2U);
    if (!path) return false;
    memcpy(path, sysroot, root_length);
    path[root_length] = '/';
    memcpy(path + root_length + 1U, name, name_length + 1U);
    bool found = regular_file(path, false);
    free(path);
    return found;
}

bool windows_linker_write_default_libraries(FILE *response,
                                            const char *sysroot) {
    if (!response || !sysroot) return false;
    size_t count;
    const windows_sysroot_library_t *libraries =
            windows_sysroot_libraries(&count);
    for (size_t i = 0; i < count; i++) {
        if (!libraries[i].required &&
            !sysroot_contains(sysroot, libraries[i].name))
            continue;
        if (fprintf(response, "%s\n", libraries[i].name) < 0) return false;
    }
    return true;
}

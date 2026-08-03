#include "windows_utf8.h"

#ifdef __WINDOWS

#include "utils/helper.h"

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAW data;
} rt_windows_find_iterator_t;

typedef struct {
    wchar_t *block;
    wchar_t *current;
} rt_windows_env_iterator_t;

static void rt_windows_set_conversion_error(void) {
    errno = EILSEQ;
    SetLastError(ERROR_NO_UNICODE_TRANSLATION);
}

static wchar_t *rt_windows_utf8_to_utf16(const char *input) {
    if (!input) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                                     NULL, 0);
    if (length <= 0) {
        rt_windows_set_conversion_error();
        return NULL;
    }
    if ((size_t) length > SIZE_MAX / sizeof(wchar_t)) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    wchar_t *result = malloc((size_t) length * sizeof(wchar_t));
    if (!result) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, result,
                            length) != length) {
        free(result);
        rt_windows_set_conversion_error();
        return NULL;
    }
    return result;
}

static int64_t rt_windows_utf16_to_utf8(const wchar_t *input, int length,
                                        char *output, size_t capacity) {
    if (!input || !output || capacity == 0U || capacity > INT_MAX) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    const bool nul_terminated = length < 0;
    int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input,
                                       length, NULL, 0, NULL, NULL);
    if (required <= 0) {
        rt_windows_set_conversion_error();
        return -1;
    }
    if ((nul_terminated && (size_t) required > capacity) ||
        (!nul_terminated && (size_t) required >= capacity)) {
        errno = ERANGE;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return -1;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, length,
                            output, (int) capacity, NULL, NULL) != required) {
        rt_windows_set_conversion_error();
        return -1;
    }
    if (nul_terminated) return required - 1;
    output[required] = '\0';
    return required;
}

int32_t rt_windows_open_mode(int32_t mode) {
    int32_t windows_mode = 0;
    if ((mode & 0444) != 0) windows_mode |= _S_IREAD;
    if ((mode & 0222) != 0) windows_mode |= _S_IWRITE;
    return windows_mode;
}

int32_t rt_windows_open_utf8(const char *path, int32_t flags, int32_t mode) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int result = _wopen(wide, flags | _O_BINARY,
                        rt_windows_open_mode(mode));
    free(wide);
    return result;
}

int32_t rt_windows_stat_utf8(const char *path, void *result) {
    if (!result) {
        errno = EINVAL;
        return -1;
    }
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wstat64(wide, (struct _stat64 *) result);
    free(wide);
    return status;
}

int32_t rt_windows_mkdir_utf8(const char *path) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wmkdir(wide);
    free(wide);
    return status;
}

int32_t rt_windows_rmdir_utf8(const char *path) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wrmdir(wide);
    free(wide);
    return status;
}

int32_t rt_windows_unlink_utf8(const char *path) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wunlink(wide);
    free(wide);
    return status;
}

int32_t rt_windows_rename_utf8(const char *old_path, const char *new_path) {
    wchar_t *wide_old = rt_windows_utf8_to_utf16(old_path);
    if (!wide_old) return -1;
    wchar_t *wide_new = rt_windows_utf8_to_utf16(new_path);
    if (!wide_new) {
        free(wide_old);
        return -1;
    }
    int status = _wrename(wide_old, wide_new);
    free(wide_new);
    free(wide_old);
    return status;
}

int32_t rt_windows_chmod_utf8(const char *path, int32_t mode) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wchmod(wide, rt_windows_open_mode(mode));
    free(wide);
    return status;
}

int32_t rt_windows_chdir_utf8(const char *path) {
    wchar_t *wide = rt_windows_utf8_to_utf16(path);
    if (!wide) return -1;
    int status = _wchdir(wide);
    free(wide);
    return status;
}

int64_t rt_windows_getcwd_utf8(char *output, size_t capacity) {
    DWORD required = GetCurrentDirectoryW(0, NULL);
    if (required == 0U) return -1;
    if ((size_t) required > SIZE_MAX / sizeof(wchar_t)) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }

    wchar_t *wide = malloc((size_t) required * sizeof(wchar_t));
    if (!wide) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    DWORD length = GetCurrentDirectoryW(required, wide);
    if (length == 0U || length >= required) {
        free(wide);
        return -1;
    }
    int64_t result =
            rt_windows_utf16_to_utf8(wide, (int) length, output, capacity);
    free(wide);
    return result;
}

static int64_t rt_windows_module_filename_utf8(char *output,
                                               size_t capacity) {
    DWORD wide_capacity = 512U;
    while (wide_capacity <= 32768U) {
        wchar_t *wide = malloc((size_t) wide_capacity * sizeof(wchar_t));
        if (!wide) {
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return -1;
        }
        SetLastError(ERROR_SUCCESS);
        DWORD length = GetModuleFileNameW(NULL, wide, wide_capacity);
        DWORD error = GetLastError();
        if (length == 0U) {
            free(wide);
            return -1;
        }
        if (length < wide_capacity && error != ERROR_INSUFFICIENT_BUFFER) {
            int64_t result = rt_windows_utf16_to_utf8(
                    wide, (int) length, output, capacity);
            free(wide);
            return result;
        }
        free(wide);
        wide_capacity *= 2U;
    }
    errno = ERANGE;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return -1;
}

static int64_t rt_windows_final_path_utf8(const char *path, char *output,
                                          size_t capacity) {
    wchar_t *wide_path = rt_windows_utf8_to_utf16(path);
    if (!wide_path) return -1;

    HANDLE handle = CreateFileW(wide_path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE |
                                        FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide_path);
    if (handle == INVALID_HANDLE_VALUE) return -1;

    DWORD required = GetFinalPathNameByHandleW(handle, NULL, 0, 0);
    if (required == 0U || required == UINT32_MAX ||
        (size_t) required + 1U > SIZE_MAX / sizeof(wchar_t)) {
        DWORD error = GetLastError();
        CloseHandle(handle);
        SetLastError(error);
        return -1;
    }
    wchar_t *wide = malloc(((size_t) required + 1U) * sizeof(wchar_t));
    if (!wide) {
        CloseHandle(handle);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    DWORD length = GetFinalPathNameByHandleW(handle, wide, required + 1U, 0);
    DWORD error = GetLastError();
    CloseHandle(handle);
    if (length == 0U || length > required) {
        free(wide);
        SetLastError(error);
        return -1;
    }

    const wchar_t *normalized = wide;
    DWORD normalized_length = length;
    wchar_t *unc = NULL;
    if (length >= 8U && wcsncmp(wide, L"\\\\?\\UNC\\", 8U) == 0) {
        normalized_length = length - 6U;
        unc = malloc(((size_t) normalized_length + 1U) * sizeof(wchar_t));
        if (!unc) {
            free(wide);
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return -1;
        }
        unc[0] = L'\\';
        unc[1] = L'\\';
        memcpy(unc + 2U, wide + 8U,
               ((size_t) length - 8U) * sizeof(wchar_t));
        unc[normalized_length] = L'\0';
        normalized = unc;
    } else if (length >= 4U && wcsncmp(wide, L"\\\\?\\", 4U) == 0) {
        normalized = wide + 4U;
        normalized_length = length - 4U;
    }

    int64_t result = rt_windows_utf16_to_utf8(
            normalized, (int) normalized_length, output, capacity);
    free(unc);
    free(wide);
    return result;
}

int64_t rt_windows_readlink_utf8(const char *path, char *output,
                                 size_t capacity) {
    if (path && strcmp(path, "/proc/self/exe") == 0)
        return rt_windows_module_filename_utf8(output, capacity);
    return rt_windows_final_path_utf8(path, output, capacity);
}

int64_t rt_windows_get_env_utf8(const char *key, char *output,
                                size_t capacity) {
    if (!output || capacity == 0U) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    output[0] = '\0';
    wchar_t *wide_key = rt_windows_utf8_to_utf16(key);
    if (!wide_key) return -1;

    SetLastError(ERROR_SUCCESS);
    DWORD required = GetEnvironmentVariableW(wide_key, NULL, 0);
    DWORD error = GetLastError();
    if (required == 0U) {
        free(wide_key);
        if (error == ERROR_SUCCESS || error == ERROR_ENVVAR_NOT_FOUND) return 0;
        SetLastError(error);
        return -1;
    }
    if ((size_t) required > SIZE_MAX / sizeof(wchar_t)) {
        free(wide_key);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }

    wchar_t *wide_value = malloc((size_t) required * sizeof(wchar_t));
    if (!wide_value) {
        free(wide_key);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    DWORD length =
            GetEnvironmentVariableW(wide_key, wide_value, required);
    error = GetLastError();
    free(wide_key);
    if (length >= required) {
        free(wide_value);
        SetLastError(error == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : error);
        return -1;
    }
    int64_t result = rt_windows_utf16_to_utf8(
            wide_value, (int) length, output, capacity);
    free(wide_value);
    return result;
}

int32_t rt_windows_set_env_utf8(const char *key, const char *value) {
    wchar_t *wide_key = rt_windows_utf8_to_utf16(key);
    if (!wide_key) return 0;
    wchar_t *wide_value = rt_windows_utf8_to_utf16(value);
    if (!wide_value) {
        free(wide_key);
        return 0;
    }
    BOOL result = SetEnvironmentVariableW(wide_key, wide_value);
    free(wide_value);
    free(wide_key);
    return result != FALSE;
}

static int32_t rt_windows_env_entry(rt_windows_env_iterator_t *iterator,
                                    char *entry, size_t capacity) {
    return rt_windows_utf16_to_utf8(iterator->current, -1, entry, capacity) <
                           0
                   ? -1
                   : 1;
}

void *rt_windows_env_first_utf8(char *entry, size_t capacity) {
    rt_windows_env_iterator_t *iterator = calloc(1U, sizeof(*iterator));
    if (!iterator) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    iterator->block = GetEnvironmentStringsW();
    if (!iterator->block) {
        free(iterator);
        return NULL;
    }
    iterator->current = iterator->block;
    if (*iterator->current == L'\0' ||
        rt_windows_env_entry(iterator, entry, capacity) < 0) {
        DWORD error = GetLastError();
        FreeEnvironmentStringsW(iterator->block);
        free(iterator);
        SetLastError(error);
        return NULL;
    }
    return iterator;
}

int32_t rt_windows_env_next_utf8(void *iterator_value, char *entry,
                                 size_t capacity) {
    rt_windows_env_iterator_t *iterator = iterator_value;
    if (!iterator || !iterator->current) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    iterator->current += wcslen(iterator->current) + 1U;
    if (*iterator->current == L'\0') return 0;
    return rt_windows_env_entry(iterator, entry, capacity);
}

int32_t rt_windows_env_close_utf8(void *iterator_value) {
    rt_windows_env_iterator_t *iterator = iterator_value;
    if (!iterator) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL released = FreeEnvironmentStringsW(iterator->block);
    free(iterator);
    return released != FALSE;
}

static int32_t rt_windows_find_name(rt_windows_find_iterator_t *iterator,
                                    char *name, size_t capacity) {
    int64_t length = rt_windows_utf16_to_utf8(iterator->data.cFileName, -1,
                                              name, capacity);
    if (length < 0) return -1;
    return 1;
}

void *rt_windows_find_first_utf8(const char *pattern, char *name,
                                 size_t capacity) {
    wchar_t *wide = rt_windows_utf8_to_utf16(pattern);
    if (!wide) return NULL;
    rt_windows_find_iterator_t *iterator = calloc(1U, sizeof(*iterator));
    if (!iterator) {
        free(wide);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    iterator->handle = FindFirstFileW(wide, &iterator->data);
    free(wide);
    if (iterator->handle == INVALID_HANDLE_VALUE) {
        free(iterator);
        return NULL;
    }
    if (rt_windows_find_name(iterator, name, capacity) < 0) {
        DWORD error = GetLastError();
        FindClose(iterator->handle);
        free(iterator);
        SetLastError(error);
        return NULL;
    }
    return iterator;
}

int32_t rt_windows_find_next_utf8(void *iterator_value, char *name,
                                  size_t capacity) {
    rt_windows_find_iterator_t *iterator = iterator_value;
    if (!iterator) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (!FindNextFileW(iterator->handle, &iterator->data)) {
        return GetLastError() == ERROR_NO_MORE_FILES ? 0 : -1;
    }
    return rt_windows_find_name(iterator, name, capacity);
}

int32_t rt_windows_find_close_utf8(void *iterator_value) {
    rt_windows_find_iterator_t *iterator = iterator_value;
    if (!iterator) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL closed = FindClose(iterator->handle);
    free(iterator);
    return closed != FALSE;
}

#endif

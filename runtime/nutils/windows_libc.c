#ifdef __WINDOWS

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <tlhelp32.h>

typedef struct {
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;
    int32_t tm_mon;
    int32_t tm_year;
    int32_t tm_wday;
    int32_t tm_yday;
    int32_t tm_isdst;
    int64_t tm_gmtoff;
    const char *tm_zone;
} rt_windows_tm_t;

_Static_assert(sizeof(time_t) == sizeof(int64_t),
               "windows_amd64 runtime requires 64-bit time_t");
_Static_assert(offsetof(rt_windows_tm_t, tm_gmtoff) == 40,
               "Nature tm UTC offset must follow nine UCRT integer fields");
_Static_assert(offsetof(rt_windows_tm_t, tm_zone) == 48,
               "Nature tm zone pointer layout changed");

static _Thread_local rt_windows_tm_t rt_windows_gmtime_result;
static _Thread_local rt_windows_tm_t rt_windows_localtime_result;

int getppid(void) {
    DWORD current_pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        errno = ESRCH;
        return -1;
    }

    PROCESSENTRY32W entry = {0};
    entry.dwSize = sizeof(entry);
    int parent_pid = -1;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == current_pid) {
                parent_pid = (int) entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (parent_pid <= 0) errno = ESRCH;
    return parent_pid;
}

static int rt_windows_validate_env_name(const char *name) {
    if (!name || name[0] == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (rt_windows_validate_env_name(name) != 0 || !value) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name) != NULL) return 0;

    errno_t error = _putenv_s(name, value);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

int unsetenv(const char *name) {
    if (rt_windows_validate_env_name(name) != 0) return -1;

    errno_t error = _putenv_s(name, "");
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

static void rt_windows_copy_tm(rt_windows_tm_t *destination,
                               const struct tm *source, int64_t gmtoff,
                               const char *zone) {
    destination->tm_sec = source->tm_sec;
    destination->tm_min = source->tm_min;
    destination->tm_hour = source->tm_hour;
    destination->tm_mday = source->tm_mday;
    destination->tm_mon = source->tm_mon;
    destination->tm_year = source->tm_year;
    destination->tm_wday = source->tm_wday;
    destination->tm_yday = source->tm_yday;
    destination->tm_isdst = source->tm_isdst;
    destination->tm_gmtoff = gmtoff;
    destination->tm_zone = zone;
}

static int64_t rt_windows_days_from_civil(int64_t year, unsigned month,
                                          unsigned day) {
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned) (year - era * 400);
    unsigned shifted_month = month > 2 ? month - 3 : month + 9;
    unsigned day_of_year = (153 * shifted_month + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
                          year_of_era / 100 + day_of_year;
    return era * 146097 + (int64_t) day_of_era - 719468;
}

static int64_t rt_windows_tm_as_utc_seconds(const struct tm *value) {
    int64_t days = rt_windows_days_from_civil(
            (int64_t) value->tm_year + 1900, (unsigned) value->tm_mon + 1,
            (unsigned) value->tm_mday);
    return days * 86400 + (int64_t) value->tm_hour * 3600 +
           (int64_t) value->tm_min * 60 + value->tm_sec;
}

rt_windows_tm_t *rt_windows_gmtime(const int64_t *timestamp) {
    if (!timestamp) return NULL;
    time_t native_timestamp = (time_t) *timestamp;
    struct tm *value = gmtime(&native_timestamp);
    if (!value) return NULL;
    rt_windows_copy_tm(&rt_windows_gmtime_result, value, 0, "UTC");
    return &rt_windows_gmtime_result;
}

rt_windows_tm_t *rt_windows_localtime(const int64_t *timestamp) {
    if (!timestamp) return NULL;
    time_t native_timestamp = (time_t) *timestamp;
    struct tm *value = localtime(&native_timestamp);
    if (!value) return NULL;

    int64_t gmtoff = rt_windows_tm_as_utc_seconds(value) - *timestamp;
    rt_windows_copy_tm(&rt_windows_localtime_result, value, gmtoff, "LOCAL");
    return &rt_windows_localtime_result;
}

#endif

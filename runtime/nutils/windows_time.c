#ifdef __WINDOWS

#include <errno.h>
#include <stdint.h>

typedef struct {
    uint32_t low;
    uint32_t high;
} rt_windows_filetime_t;

typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} rt_windows_timespec_t;

__declspec(dllimport) void __stdcall GetSystemTimePreciseAsFileTime(
        rt_windows_filetime_t *filetime);
__declspec(dllimport) int __stdcall QueryPerformanceCounter(int64_t *value);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(int64_t *value);

enum {
    RT_CLOCK_REALTIME = 0,
    RT_CLOCK_MONOTONIC = 1,
    RT_CLOCK_MONOTONIC_RAW = 4,
    RT_CLOCK_BOOTTIME = 7,
};

static int rt_windows_clock_supported(int clock_id) {
    return clock_id == RT_CLOCK_REALTIME || clock_id == RT_CLOCK_MONOTONIC ||
           clock_id == RT_CLOCK_MONOTONIC_RAW || clock_id == RT_CLOCK_BOOTTIME;
}

int clock_gettime(int clock_id, rt_windows_timespec_t *result) {
    if (!result || !rt_windows_clock_supported(clock_id)) {
        errno = EINVAL;
        return -1;
    }

    if (clock_id == RT_CLOCK_REALTIME) {
        rt_windows_filetime_t filetime;
        GetSystemTimePreciseAsFileTime(&filetime);
        uint64_t ticks = ((uint64_t) filetime.high << 32U) | filetime.low;
        ticks -= UINT64_C(116444736000000000);
        result->tv_sec = (int64_t) (ticks / UINT64_C(10000000));
        result->tv_nsec =
                (int64_t) ((ticks % UINT64_C(10000000)) * UINT64_C(100));
        return 0;
    }

    int64_t counter;
    int64_t frequency;
    if (!QueryPerformanceCounter(&counter) ||
        !QueryPerformanceFrequency(&frequency) || frequency <= 0) {
        errno = EINVAL;
        return -1;
    }
    result->tv_sec = counter / frequency;
    result->tv_nsec = (int64_t) (((uint64_t) (counter % frequency) *
                                  UINT64_C(1000000000)) /
                                 (uint64_t) frequency);
    return 0;
}

int clock_getres(int clock_id, rt_windows_timespec_t *result) {
    if (!result || !rt_windows_clock_supported(clock_id)) {
        errno = EINVAL;
        return -1;
    }

    result->tv_sec = 0;
    if (clock_id == RT_CLOCK_REALTIME) {
        result->tv_nsec = 100;
        return 0;
    }

    int64_t frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency <= 0) {
        errno = EINVAL;
        return -1;
    }
    result->tv_nsec = (int64_t) ((UINT64_C(1000000000) +
                                  (uint64_t) frequency - 1U) /
                                 (uint64_t) frequency);
    if (result->tv_nsec == 0) result->tv_nsec = 1;
    return 0;
}

#endif

#include <stdint.h>
#include <stdarg.h>
#include <string.h>

typedef struct {
    int32_t a;
    int32_t b;
} pair32_t;

typedef struct {
    int64_t a;
    int64_t b;
} pair64_t;

typedef int64_t (*nature_mixed_callback_t)(int64_t, double, int64_t, double,
                                           int64_t, double, int64_t, double);

int64_t c_mixed_slots(int64_t a, double b, int64_t c, double d, int64_t e,
                      double f, int64_t g, double h) {
    return a + (int64_t) b + c + (int64_t) d + e + (int64_t) f + g +
           (int64_t) h;
}

int64_t c_variadic_mixed(int32_t count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int64_t result = 0;
    for (int32_t i = 0; i < count; i++) {
        if ((i & 1) == 0)
            result += (int64_t) va_arg(arguments, double);
        else
            result += va_arg(arguments, int64_t);
    }
    va_end(arguments);
    return result;
}

int64_t c_pair32(pair32_t value) {
    return (int64_t) value.a * 1000 + value.b;
}

int64_t c_pair64(pair64_t value) {
    return value.a * 1000 + value.b;
}

pair64_t c_make_pair64(int64_t a, int64_t b) {
    pair64_t result = {a, b};
    return result;
}

int64_t c_call_nature(nature_mixed_callback_t callback) {
    return callback(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0);
}

int64_t c_check_xmm_nonvolatile(nature_mixed_callback_t callback) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    static const uint64_t expected[2] = {
            UINT64_C(0x0123456789abcdef),
            UINT64_C(0xfedcba9876543210),
    };
    uint64_t actual[2] = {0, 0};
    __asm__ volatile("movdqu %0, %%xmm6" : : "m"(expected[0]) : "xmm6");
    int64_t result = callback(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0);
    __asm__ volatile("movdqu %%xmm6, %0" : "=m"(actual[0]));
    return memcmp(expected, actual, sizeof(expected)) == 0 ? result : -1;
#else
    return callback(1, 2.0, 3, 4.0, 5, 6.0, 7, 8.0);
#endif
}

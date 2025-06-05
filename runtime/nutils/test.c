#include "test.h"

#include "process.h"
#include <stdlib.h>

static void __attribute__((noinline)) test_sleep_yield() {
    char *str = "sleep wait gc";
    char *str2 = "sleep wait gc";
    char *str3 = "sleep wait gc";
    DEBUGF("sleep wait gc completed")
    rt_coroutine_sleep(1000);
    DEBUGF("sleep completed")
}

static void __attribute__((noinline)) test_nest1() {
    int a = 12;
    int b = 24;
    int c = 32;
    void *d = malloc(10);
    test_sleep_yield();
}

void __attribute__((noinline)) test_gc_sleep_yield() {
    int a = 12;
    int b = 24;
    int c = 32;
    void *d = malloc(10);
    test_nest1();
    int e[12] = {0};
    free(d);
}

void init_safepoint(int64_t v) {
    tls_safepoint = v;
}

int64_t get_safepoint() {
    return tls_safepoint;
}

void test_arm64_abi_draw_line_ex(vector2_t v1, vector2_t v2) {
    printf("v1 %f, %f\n", v1.x, v1.y);
    printf("v2 %f, %f\n", v1.x, v2.y);
}
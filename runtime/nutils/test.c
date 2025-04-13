#include "test.h"

#include "process.h"
#include <stdlib.h>

static void __attribute__((noinline)) test_sleep_yield() {
    char *str = "sleep wait gc";
    char *str2 = "sleep wait gc";
    char *str3 = "sleep wait gc";
    TDEBUGF("sleep wait gc completed")
    rt_coroutine_sleep(1000);
    TDEBUGF("sleep completed")
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

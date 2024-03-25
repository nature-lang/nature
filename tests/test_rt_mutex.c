#include <stdio.h>

#include "runtime/rt_mutex.h"
#include "test_runtime.h"

#define MAIN_NAME test_main

rt_mutex_t m;

void *print_hello(void *arg) {
    rt_mutex_lock(&m);
    DEBUGF("[print_hello] hello")
    sleep(2);
    DEBUGF("[print_hello] will unlock")
    rt_mutex_unlock(&m);
    return NULL;
}

void *print_world(void *arg) {
    rt_mutex_lock(&m);
    DEBUGF("[print_world] world")
    sleep(2);
    DEBUGF("[print_world] will unlock")
    return NULL;
}

static void test_basic() {

    //
    //    pthread_create(&th1, NULL, print_hello, NULL);
    //    pthread_create(&th2, NULL, print_world, NULL);
    //
    //    pthread_join(th1, NULL);
    //    pthread_join(th2, NULL);
}

void user_main() {
    DEBUGF("hello world, in main co");
}


int main(void) {
    runtime_main(0, NULL);
}
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>

#include "tests/test.h"

#define BUFFER_SIZE 1024

ATOMIC int32_t pid;

// 线程函数必须是这种形式：void *function(void *arg)
static void *thread_kill(void *arg) {
    // 等待主线程准备就绪
    sleep(1);

    pid = atomic_load(&pid);
    assert(pid > 0);
    kill(pid, SIGUSR1);
    usleep(100 * 1000);
    kill(pid, SIGUSR2);
    usleep(100 * 1000);
    kill(pid, SIGTERM);
    usleep(100 * 1000);
    kill(pid, SIGINT);
    usleep(100 * 1000);
    kill(pid, SIGUSR1);
    usleep(100 * 1000);
    kill(pid, SIGUSR2);
    usleep(100 * 1000);
    kill(pid, SIGKILL);

    return NULL;
}

int main(int argc, char *argv[]) {
    feature_test_build();
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, thread_kill, NULL);
    assert(ret == 0);
    char *raw = exec_output_with_pid(&pid);
    assert_string_equal(raw, dsprintf("received signal:  %d\n"
                                      "received signal:  %d\n"
                                      "received signal:  %d\n"
                                      "received signal:  %d\n"
                                      "received signal:  %d\n"
                                      "received signal:  %d\n", SIGUSR1, SIGUSR2, SIGTERM, SIGINT, SIGUSR1, SIGUSR2));
    return 0;
}

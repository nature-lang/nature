#ifndef RUNTIME_SIGNAL_H
#define RUNTIME_SIGNAL_H

#include "runtime/processor.h"
#include "utils/type.h"
#include "runtime/rt_mutex.h"
#include "runtime/rt_chan.h"
#include <signal.h>
#include <stdatomic.h>
#include <uv.h>

extern ATOMIC int64_t signal_recv;
extern int64_t signal_mask;
extern pthread_mutex_t signal_locker;
extern struct sc_map_64 signal_handlers;

extern coroutine_t *signal_loop_co;

extern int64_t sig_ref[NSIG];

static const int64_t all_signals[] = {
        SIGHUP, // 默认行为是终止进程
        SIGINT, // 默认行为是终止进程
        SIGUSR1,
        SIGUSR2,
        SIGTERM, // 默认行为是终止进程
};

void signal_notify(n_chan_t *ch, n_vec_t *signals);

static inline bool signal_intercepted(int sig) {
    return (signal_mask & (1 << sig)) != 0;
}

static inline void signal_handle(int sig) {
    pthread_mutex_lock(&signal_locker);
    DEBUGF("[runtime.signal_handle] signal %d received", sig);
    // 判断信号是否 mask
    if (signal_intercepted(sig)) {
        int64_t recv = atomic_load(&signal_recv);

        recv |= 1 << sig;

        // 设置 recv 的值
        atomic_store(&signal_recv, recv);

//        if (signal_loop_co) {
//            // TODO 如果已经存在，则避免重复写入
//            co_ready(signal_loop_co);
//        }
        DEBUGF("[runtime.signal_handle] signal %d received, current signal_recv %ld", sig, signal_recv);
        pthread_mutex_unlock(&signal_locker);
        return;
    }

    pthread_mutex_unlock(&signal_locker);

    if (sig == SIGHUP || sig == SIGINT || sig == SIGTERM) {
        // 触发信号默认行为
        // signal(sig, SIG_DFL);
        // raise(sig);
        exit(128 + sig);
    }
}

static inline void signal_init() {
    pthread_mutex_init(&signal_locker, NULL);

    sc_map_init_64(&signal_handlers, 0, 0);

    struct sigaction act;
    memset(&act, 0, sizeof act);
    sigemptyset(&act.sa_mask);
    act.sa_handler = signal_handle;
    act.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &act, NULL);
    for (int64_t i = 0; i < sizeof(all_signals) / sizeof(all_signals[0]); i++) {
        if (sigaction((int) all_signals[i], &act, NULL) < 0) {
            DEBUGF("[runtime.signal_init] cannot install signal %ld handler: %s.\n", all_signals[i], strerror(errno));
        }
    }
}

static inline void signal_process(int64_t sig) {
    // 获取信号处理函数, sc for
    int64_t key;
    int64_t mask;
    sc_map_foreach(&signal_handlers, key, mask) {
            n_chan_t *ch = (n_chan_t *) key;
            if (mask & (1 << sig)) {
                bool result = rt_chan_send(ch, &sig, true);
                DEBUGF("[runtime.signal_process] signal %ld mask, will send to channel %p, send result = %d", sig, ch,
                        result);
            }
        }
}

// yield wait signal 到来，然后等待 signal_handle 唤醒即可
static inline void signal_loop() {
    while (true) {
        if (atomic_load(&signal_recv) == 0) {
            goto YIELD;
        }

        pthread_mutex_lock(&signal_locker);

        // 遍历 all_signals 并判断是否存在就绪的信号，如何存在则进行信号处理(no block try send)
        for (int64_t i = 0; i < sizeof(all_signals) / sizeof(all_signals[0]); i++) {
            int64_t sig = all_signals[i];
            // 检查该信号是否被接收
            if (atomic_load(&signal_recv) & (1 << sig)) {
                // 清除该信号的接收标志
                atomic_fetch_and(&signal_recv, ~(1 << sig));
                DEBUGF("[runtime.signal_loop] signal %ld clean, current signal_recv %ld", sig, signal_recv);

                signal_process(sig);
            }
        }
        pthread_mutex_unlock(&signal_locker);

        // 等待信号事件就绪
        YIELD:
//        co_yield_waiting(co, NULL, NULL);
        rt_coroutine_sleep(50);
    }
}


#endif //SIGNAL_H

#include "rt_signal.h"

#include "vec.h"

// sig to ch list
ATOMIC int64_t signal_recv = 0;

int64_t signal_mask = 0;

pthread_mutex_t signal_locker = PTHREAD_MUTEX_INITIALIZER;

struct sc_map_64 signal_handlers = {0};

int64_t sig_ref[NSIG] = {0};

coroutine_t *signal_loop_co = NULL;

void signal_notify(n_chan_t *ch, n_vec_t *signals) {
    pthread_mutex_lock(&signal_locker);

    // 判断 signal_handlers 是否有对应的 handle
    uint64_t mask = sc_map_get_64(&signal_handlers, (uint64_t) ch);

    // 如果 signals 为空，则监听所有信号
    if (signals->length == 0) {
        for (int i = 0; i < sizeof(all_signals) / sizeof(all_signals[0]); i++) {
            int64_t sig = all_signals[i];
            if (sig < 0 || sig > NSIG - 1) {
                continue;
            }

            mask |= 1 << sig;
            sig_ref[sig]++;

            // set mask
            signal_mask |= 1 << sig;
        }
    } else {
        // 原有逻辑：处理指定的信号
        for (int i = 0; i < signals->length; i++) {
            size_t sig;
            rt_vec_access(signals, i, &sig);
            if (sig < 0 || sig > NSIG - 1) {
                continue;
            }

            mask |= 1 << sig;
            sig_ref[sig]++;

            // set mask
            signal_mask |= 1 << sig;
        }
    }

    sc_map_put_64(&signal_handlers, (uint64_t) ch, mask);

    if (!signal_loop_co) {
        signal_loop_co = rt_coroutine_new(signal_loop, FLAG(CO_FLAG_RTFN), NULL, NULL);
        DEBUGF("[signal_notify] pid %d builtin coroutine %p signal loop create success", getpid(), signal_loop_co);
        rt_coroutine_dispatch(signal_loop_co);
    }

    pthread_mutex_unlock(&signal_locker);
}

void signal_stop(n_chan_t *ch) {
    pthread_mutex_lock(&signal_locker);

    // 清理 mask 中的标志，也就是 ref - 1, ref = 0 就清空 mask 掩码
    ATOMIC uint64_t mask = sc_map_get_64(&signal_handlers, (uint64_t) ch);
    sc_map_del_64(&signal_handlers, (uint64_t) ch);
    for (int64_t i = 0; i < sizeof(all_signals) / sizeof(all_signals[0]); i++) {
        int64_t sig = all_signals[i];
        // 检查该信号是否被接收
        if (atomic_load(&mask) & (1 << sig)) {
            sig_ref[sig]--;
            if (sig_ref[sig] == 0) {
                signal_mask &= ~(1 << sig);
            }
        }
    }

    pthread_mutex_unlock(&signal_locker);
}

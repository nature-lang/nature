#include "runtime/nutils/rt_signal.h"

#include <stdio.h>
#include <unistd.h>

atomic_ullong signal_recv = 0;
atomic_ullong signal_mask = 0;
pthread_mutex_t signal_locker = PTHREAD_MUTEX_INITIALIZER;

static void check(bool success, const char *message) {
    if (!success) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(void) {
    struct sigaction action = {0};
    action.sa_handler = signal_handle;
    check(sigemptyset(&action.sa_mask) == 0, "cannot initialize signal mask");
    check(sigaction(SIGWINCH, &action, NULL) == 0, "cannot install SIGWINCH handler");
    check(sigaction(SIGUSR1, &action, NULL) == 0, "cannot install SIGUSR1 handler");

    uint64_t expected = (1ULL << SIGWINCH) | (1ULL << SIGUSR1);
    atomic_store_explicit(&signal_mask, expected, memory_order_relaxed);

    // A real signal interrupts the thread while it owns the registry mutex.
    // The old handler tries to acquire it again and hangs until the watchdog.
    alarm(3);
    check(pthread_mutex_lock(&signal_locker) == 0, "cannot lock signal registry");
    check(raise(SIGWINCH) == 0, "cannot raise SIGWINCH");
    check(raise(SIGUSR1) == 0, "cannot raise SIGUSR1");
    check(pthread_mutex_unlock(&signal_locker) == 0, "cannot unlock signal registry");
    alarm(0);

    check(atomic_exchange_explicit(&signal_recv, 0, memory_order_relaxed) == expected,
          "handler did not preserve both pending signals");
    check(atomic_load_explicit(&signal_recv, memory_order_relaxed) == 0,
          "pending signals were not consumed");

    puts("signal handler returns while registry mutex is held");
    return 0;
}

#ifndef NATURE_UTILS_SAFE_MUTEX_H
#define NATURE_UTILS_SAFE_MUTEX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "runtime/runtime.h"
#include "utils/bitmap.h"

static inline mutex_t *safe_mutex_new(bool recursive) {
    mutex_t *mutex = SAFE_NEW(mutex_t);
    if (recursive) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mutex->locker, &attr);
        pthread_mutexattr_destroy(&attr);
        return mutex;
    }

    pthread_mutex_init(&mutex->locker, NULL);
    return mutex;
}

#endif // NATURE_BITMAP_H

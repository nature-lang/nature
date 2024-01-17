#include "mutex.h"

mutex_t *mutex_new(bool recursive) {
    mutex_t *mutex = NEW(mutex_t);
    if (recursive) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        pthread_mutex_init(mutex, &attr);

        //        pthread_mutexattr_destroy(&attr);
        return mutex;
    }

    pthread_mutex_init(mutex, NULL);
    return mutex;
}

int mutex_lock(mutex_t *mutex) {
    return pthread_mutex_lock(mutex);
}

int mutex_trylock(mutex_t *mutex) {
    return pthread_mutex_trylock(mutex);
}

int mutex_unlock(mutex_t *mutex) {
    return pthread_mutex_unlock(mutex);
}

int mutex_destroy(mutex_t *mutex) {
    int result = pthread_mutex_destroy(mutex);
    free(mutex);
    return result;
}
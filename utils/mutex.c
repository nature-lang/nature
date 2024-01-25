#include "mutex.h"

mutex_t *mutex_new(bool recursive) {
    mutex_t *mutex = NEW(mutex_t);
    if (recursive) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        pthread_mutex_init(&mutex->locker, &attr);

        //        pthread_mutexattr_destroy(&attr);
        return mutex;
    }

    pthread_mutex_init(&mutex->locker, NULL);
    return mutex;
}

void mutex_init(mutex_t *mutex, bool recursive) {
    if (recursive) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

        pthread_mutex_init(&mutex->locker, &attr);
        return;
    }

    pthread_mutex_init(&mutex->locker, NULL);
}

int mutex_lock(mutex_t *mutex) {
    int result = pthread_mutex_lock(&mutex->locker);
    mutex->locker_count++;
    return result;
}

int mutex_trylock(mutex_t *mutex) {
    return pthread_mutex_trylock(&mutex->locker);
}

int mutex_unlock(mutex_t *mutex) {
    mutex->unlocker_count++;
    return pthread_mutex_unlock(&mutex->locker);
}

int mutex_destroy(mutex_t *mutex) {
    int result = pthread_mutex_destroy(&mutex->locker);
    free(mutex);
    return result;
}


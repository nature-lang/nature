#include "mutex.h"

mutex_t *mutex_new() {
    mutex_t *mutex = NEW(mutex_t);
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
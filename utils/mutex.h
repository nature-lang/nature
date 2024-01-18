#ifndef NATURE_MUTEX_H
#define NATURE_MUTEX_H

#include <pthread.h>

#include "helper.h"

typedef struct {
    pthread_mutex_t locker;
    int locker_count; // 加锁次数
    int unlocker_count; // 解锁次数
} mutex_t;

mutex_t *mutex_new(bool recursive);

int mutex_lock(mutex_t *mutex);

int mutex_trylock(mutex_t *mutex);

int mutex_unlock(mutex_t *mutex);

int mutex_destroy(mutex_t *mutex);

#endif // NATURE_MUTEX_H

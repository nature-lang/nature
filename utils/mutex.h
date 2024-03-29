#ifndef NATURE_MUTEX_H
#define NATURE_MUTEX_H

#include <pthread.h>

#include "helper.h"

typedef struct {
    pthread_mutex_t locker;
    int locker_count;   // 加锁次数
    int unlocker_count; // 解锁次数
} mutex_t;

mutex_t *mutex_new(bool recursive);

void mutex_init(mutex_t *mutex, bool recursive);

int mutex_lock(mutex_t *mutex);

int mutex_trylock(mutex_t *mutex);

// 每一次 times 都会等待 1ms
int mutex_times_trylock(mutex_t *mutex, int times);

int mutex_unlock(mutex_t *mutex);

int mutex_destroy(mutex_t *mutex);

#endif // NATURE_MUTEX_H

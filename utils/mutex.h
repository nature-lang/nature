#ifndef NATURE_MUTEX_H
#define NATURE_MUTEX_H

#include <pthread.h>

#include "helper.h"

typedef pthread_mutex_t mutex_t;

mutex_t *mutex_new();

int mutex_lock(mutex_t *mutex);

int mutex_trylock(mutex_t *mutex);

int mutex_unlock(mutex_t *mutex);

int mutex_destroy(mutex_t *mutex);

#endif // NATURE_MUTEX_H

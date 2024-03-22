#ifndef NATURE_MUTEX_H
#define NATURE_MUTEX_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MUTEX_LOCKED 1 << 0
#define MUTEX_STARVING 2 << 1

typedef struct {
    int64_t state;
    int64_t *waits;
} rt_mutex_t;

void rt_mutex_lock(rt_mutex_t *mutex);

bool rt_can_spin(int64_t count);
void rt_do_spin();

#endif//NATURE_MUTEX_H

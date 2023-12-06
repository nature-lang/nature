#ifndef NATURE_COROUTINE_H
#define NATURE_COROUTINE_H

#include "basic.h"

void co_init();

void co_run();

/**
 * 读取当前线程的 processor_t
 */
void co_yield ();

#endif // NATURE_COROUTINE_H

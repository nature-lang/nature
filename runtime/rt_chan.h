#ifndef NATURE_RT_CHAN_H
#define NATURE_RT_CHAN_H

#include "stdlib.h"
#include "stdint.h"
#include "rt_mutex.h"
#include "processor.h"

n_chan_t *rt_chan_new(int64_t rhash, int64_t ele_rhash, int64_t buf_len);

n_bool_result_t rt_chan_send(n_chan_t *chan, void *msg_ptr, bool try);

n_bool_result_t rt_chan_recv(n_chan_t *chan, void *msg_ptr, bool try);

n_void_result_t rt_chan_close(n_chan_t *chan);

bool rt_chan_is_closed(n_chan_t *chan);

bool rt_chan_is_successful(n_chan_t *chan);

#endif //NATURE_RT_CHAN_H

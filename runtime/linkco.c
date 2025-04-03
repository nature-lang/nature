#include "linkco.h"
#include "runtime/rtype.h"

linkco_t *rti_acquire_linkco() {
    n_processor_t *p = processor_get();
    assert(p);

    if (p->linkco_count == 0) {
        mutex_lock(&global_linkco_locker);

        while (p->linkco_count < (P_LINKCO_CACHE_MAX / 2) && global_linkco_cache != NULL) {
            linkco_t *linkco = global_linkco_cache;
            global_linkco_cache = linkco->next;
            linkco->next = NULL;
            TDEBUGF("[rti_acquire_linkco] from global cache linkco %p, prev %p, next %p to p_index: %d, count %d",
                    linkco, linkco->prev, linkco->next, p->index, p->linkco_count);


            //            p->linkco_cache[p->linkco_count++] = linkco;
            rti_write_barrier_ptr(&p->linkco_cache[p->linkco_count++], linkco, true);
        }

        // 持续借取，直到超过当前 local_cache 的容量的一半
        mutex_unlock(&global_linkco_locker);

        // 全局借取失败，需要自己 new 一个
        if (p->linkco_count == 0) {
            linkco_t *linkco = rti_gc_malloc(linkco_rtype.size, &linkco_rtype);
            TDEBUGF("[rti_acquire_linkco] p: %d gc_malloc linkco %p", p->index, linkco);
            //            p->linkco_cache[p->linkco_count++] = linkco;
            rti_write_barrier_ptr(&p->linkco_cache[p->linkco_count++], linkco, false);
        }
    }

    linkco_t *linkco = p->linkco_cache[--p->linkco_count];
    TDEBUGF("[rti_acquire_linkco] p: %d, count %d linkco from cache  %p, prev %p, next %p",
            p->index, p->linkco_count, linkco, linkco->prev, linkco->next);
    assertf(linkco->prev == NULL, "%p", linkco);
    assertf(linkco->next == NULL, "%p", linkco);

    return linkco;
}

void rti_release_linkco(linkco_t *linkco) {
    assertf(linkco->prev == NULL, "%p", linkco);
    assertf(linkco->next == NULL, "%p", linkco);
    linkco->co = NULL;
    linkco->data = NULL;

    n_processor_t *p = processor_get();
    assert(p);

    if (p->linkco_count == P_LINKCO_CACHE_MAX) {
        // 归还一半到 global 中
        linkco_t *first = NULL;
        linkco_t *last = NULL;
        while (p->linkco_count > (P_LINKCO_CACHE_MAX / 2)) {
            linkco_t *temp = p->linkco_cache[--p->linkco_count];

            TDEBUGF("[rti_release_linkco] release linkco to p %d, count %d ,linkco %p pcache to global", p->index, p->linkco_count, temp);

            if (first == NULL) {
                first = temp;
            } else {
                last->next = temp;
            }

            last = temp;
        }

        mutex_lock(&global_linkco_locker);
        last->next = global_linkco_cache;
        global_linkco_cache = first;
        mutex_unlock(&global_linkco_locker);
    }

    //    p->linkco_cache[p->linkco_count++] = linkco;
    rti_write_barrier_ptr(&p->linkco_cache[p->linkco_count++], linkco, true);
    TDEBUGF("[rti_release_linkco] release linkco to p %d, count %d write_barrier from cache %p", p->index, p->linkco_count, linkco);
}

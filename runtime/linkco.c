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
            p->linkco_cache[p->linkco_count++] = linkco;
        }

        // 持续借取，直到超过当前 local_cache 的容量的一半
        mutex_unlock(&global_linkco_locker);

        // 全局借取失败，需要自己 new 一个
        if (p->linkco_count == 0) {
            linkco_t *linkco = rti_gc_malloc(linkco_rtype.size, &linkco_rtype);
            p->linkco_cache[p->linkco_count++] = linkco;
        }
    }

    linkco_t *linkco = p->linkco_cache[--p->linkco_count];
    assert(linkco->prev == NULL);
    assert(linkco->next == NULL);

    return linkco;
}

void rti_release_linkco(linkco_t *linkco) {
    assert(linkco->prev == NULL);
    assert(linkco->next == NULL);
    linkco->co = NULL;
    linkco->data = NULL;

    n_processor_t *p = processor_get();
    assert(p);

    if (p->linkco_count == P_LINKCO_CACHE_MAX) {
        // 归还一半都 global 中
        linkco_t *first = NULL;
        linkco_t *last = NULL;
        while (p->linkco_count > (P_LINKCO_CACHE_MAX / 2)) {
            linkco_t *temp = p->linkco_cache[--p->linkco_count];

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

    p->linkco_cache[p->linkco_count++] = linkco;
    int a = 12;
}

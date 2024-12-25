#include "gcbits.h"

#include <stdatomic.h>
#include "rt_mutex.h"
#include "uv.h"

gc_bits_arenas_t gc_bits_arenas;

void gcbits_areas_init() {
    pthread_mutex_init(&gc_bits_arenas.locker, NULL);
    gc_bits_arenas.free_list = NULL;
    gc_bits_arenas.next_list = NULL;

    gc_bits_arenas.current_list = NULL;
    gc_bits_arenas.previous_list = NULL;
}

gc_bits *gcbits_new(uintptr_t bytes_size) {
    TRACEF("[gcbits_new] bytes_size=%ld", bytes_size);
    uintptr_t blocks_needed = (bytes_size + 63) / 64;
    uintptr_t bytes_needed = blocks_needed * 8;

    gc_bits *p = gcbits_try_alloc(gc_bits_arenas.next_list, bytes_needed);
    if (p != NULL) {
        return p;
    }
    TRACEF("[gcbits_new] try alloc failed, bytes_needed=%ld", bytes_needed);

    pthread_mutex_lock(&gc_bits_arenas.locker);

    // 读取锁后再次尝试
    p = gcbits_try_alloc(gc_bits_arenas.next_list, bytes_needed);
    if (p != NULL) {
        pthread_mutex_unlock(&gc_bits_arenas.locker);
        return p;
    }

    TRACEF("[gcbits_new] try alloc second failed, bytes_needed=%ld", bytes_needed);

    gc_bits_arena_t *fresh = gcbits_arena_new();

    // arena_new 可能会释放锁，所以这里再次尝试
    p = gcbits_try_alloc(gc_bits_arenas.next_list, bytes_needed);
    if (p != NULL) {
        // 归还 fresh 到 free 中 插入到头部，free_list 始终指向第一个可用的元素
        fresh->next = gc_bits_arenas.free_list;
        gc_bits_arenas.free_list = fresh;

        pthread_mutex_unlock(&gc_bits_arenas.locker);
        return p;
    }

    TRACEF("[gcbits_new] try alloc third failed, bytes_needed=%ld", bytes_needed);

    p = gcbits_try_alloc(fresh, bytes_needed);
    assert(p && "gcbits_new: failed to alloc");

    // fresh arena 已经被使用，所以需要放到 next_list 中 头部
    //  arenas.next_list -> [fresh] - [used] - [used] - [used]

    fresh->next = gc_bits_arenas.next_list;
    gc_bits_arenas.next_list = fresh;

    pthread_mutex_unlock(&gc_bits_arenas.locker);
    return p;
}

gc_bits *gcbits_try_alloc(gc_bits_arena_t *arena, uintptr_t bytes_size) {
    if (arena == NULL) {
        return NULL;
    }

    TRACEF("[gcbits_try_alloc] arena=%p, free_index=%ld, bytes_size=%ld", arena, arena->free_index, bytes_size);
    uintptr_t current = atomic_load_explicit(&arena->free_index, memory_order_acquire);
    if ((current + bytes_size) > GCBITS_CHUNK_PAYLOAD_BYTES) {
        return NULL;
    }

    // 原子递增，即使有多个线程竞争，也可以保证分配的内存不会重复，因为 free_index 原子递增
    // new_start 是在多线程操作中抢到的新起始点
    uintptr_t new_start = atomic_fetch_add(&arena->free_index, bytes_size);
    if (new_start + bytes_size > GCBITS_CHUNK_PAYLOAD_BYTES) {
        return NULL;
    }

    return &arena->bits[new_start];
}

gc_bits_arena_t *gcbits_arena_new() {
    gc_bits_arena_t *result = NULL;

    if (gc_bits_arenas.free_list == NULL) {
        pthread_mutex_unlock(&gc_bits_arenas.locker);
        result = sys_memory_map(NULL, GCBITS_CHUNK_BYTES);
        assert(result && "gcbits_arena_new: failed to map memory");

        memset(result, 0, GCBITS_CHUNK_BYTES);

        pthread_mutex_lock(&gc_bits_arenas.locker);
    } else {
        result = gc_bits_arenas.free_list;
        gc_bits_arenas.free_list = gc_bits_arenas.free_list->next;

        // clear result
        memset(result, 0, GCBITS_CHUNK_BYTES);
    }

    result->next = NULL;

    uintptr_t bits_addr = (uintptr_t)&result->bits[0];
    if ((offsetof(gc_bits_arena_t, bits) & 7) == 0) {
        result->free_index = 0;
    } else {
        result->free_index = 8 - (bits_addr & 7);
    }

    return result;
}

// 需要在 gc 结尾 flush span 之后再调用该方法
void gcbits_arenas_epoch() {
    pthread_mutex_lock(&gc_bits_arenas.locker);

    if (gc_bits_arenas.previous_list) {
        // previous 中的 arena 经过了 2 次 gc 之后已经不被任何 mspan 引用， 现在可以重复利用
        if (gc_bits_arenas.free_list == NULL) {
            gc_bits_arenas.free_list = gc_bits_arenas.previous_list;
        } else {
            // 将 previous_list 插入到 free_list 前面, 需要找到 previous_list 的最后一个元素才能进行拼接
            gc_bits_arena_t *last = gc_bits_arenas.previous_list;
            while (last->next != NULL) {
                last = last->next;
            }

            last->next = gc_bits_arenas.free_list;
            gc_bits_arenas.free_list = gc_bits_arenas.previous_list;
        }
    }

    gc_bits_arenas.previous_list = gc_bits_arenas.current_list;
    gc_bits_arenas.current_list = gc_bits_arenas.next_list;
    gc_bits_arenas.next_list = NULL;

    pthread_mutex_unlock(&gc_bits_arenas.locker);
}

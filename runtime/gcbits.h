#ifndef NATURE_GCBITS_H
#define NATURE_GCBITS_H

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#define GCBITS_CHUNK_BYTES 65536
#define GCBITS_CHUNK_HEADER_BYTES 16
#define GCBITS_CHUNK_PAYLOAD_BYTES (GCBITS_CHUNK_BYTES - GCBITS_CHUNK_HEADER_BYTES)

typedef uint8_t gc_bits;

typedef struct {
    uintptr_t free;
    uintptr_t next;
} gc_bits_header_t;

typedef struct gc_bits_arena_t {
   uintptr_t free_index; // 指向 bits 下一个可用
    struct gc_bits_arena_t* next; // arenas 链表构成
    gc_bits bits[GCBITS_CHUNK_PAYLOAD_BYTES];
} gc_bits_arena_t;

typedef struct {
    pthread_mutex_t locker;
    gc_bits_arena_t* free_list; // free 链表
    gc_bits_arena_t* next_list;

    gc_bits_arena_t* current_list;
    gc_bits_arena_t* previous_list;
} gc_bits_arenas_t;

extern gc_bits_arenas_t gc_bits_arenas;

// memory init 时调用
void gcbits_areas_init();

gc_bits* gcbits_new(uintptr_t bytes_size);

gc_bits* gcbits_try_alloc(gc_bits_arena_t* arena, uintptr_t bytes_size);

/**
 * 基于 gcbits 声明周期进行 bits 的回收与重用
 */
void gcbits_arenas_epoch();

/**
 * may unlock
 * @return
 */
gc_bits_arena_t* gcbits_arena_new();

#endif // NATURE_GCBITS_H

#include "runtime/memory.h"
#include "runtime/nutils/vec.h"
#include "runtime/processor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t main_is_fn = 0;
char rt_strtable_data = 0;
uint8_t rt_data = 0;
uint64_t rt_symdef_count = 0;
symdef_t rt_symdef_data = {0};
uint64_t rt_fndef_count = 0;
fndef_t rt_fndef_data = {0};
uint64_t rt_caller_count = 0;
caller_t rt_caller_data = {0};
uint64_t rt_rtype_count = 0;
rtype_t rt_rtype_data = {0};
uint64_t rt_errset_count = 0;
uint64_t rt_errset_offset = 0;

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static n_vec_t vector(void *data, uint64_t length, uint64_t element_size) {
    return (n_vec_t) {.data = data, .length = length, .capacity = length, .element_size = element_size};
}

static void expect_pointer(rt_linked_fixalloc_t *queue, void *pointer) {
    check(rt_linked_fixalloc_pop(queue) == pointer, "wrong referent shaded");
}

static void expect_empty(rt_linked_fixalloc_t *queue) {
    check(rt_linked_fixalloc_empty(queue), "unexpected referent shaded");
}

static void check_pointer_barrier(n_processor_t *processor, coroutine_t *coroutine,
                                  void *old_pointer, void *new_pointer, void *marked_pointer) {
    void *slot = old_pointer;
    gc_barrier_stop();
    rti_write_barrier_ptr(&slot, new_pointer, false);
    check(slot == new_pointer, "barrier-disabled store failed");
    expect_empty(&global_gc_worklist);

    gc_barrier_start();
    coroutine->gc_black = 0;
    slot = old_pointer;
    rti_write_barrier_ptr(&slot, new_pointer, false);
    check(slot == new_pointer, "grey-stack store failed");
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_pointer(&global_gc_worklist, new_pointer);
    expect_empty(&global_gc_worklist);

    coroutine->gc_black = memory->gc_count;
    slot = old_pointer;
    rti_write_barrier_ptr(&slot, new_pointer, false);
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_empty(&global_gc_worklist);

    coroutine->gc_black = 0;
    processor->gc_work_finished = 0;
    slot = old_pointer;
    rti_write_barrier_ptr(&slot, new_pointer, false);
    expect_pointer(&processor->gc_worklist, old_pointer);
    expect_pointer(&processor->gc_worklist, new_pointer);
    expect_empty(&processor->gc_worklist);
    expect_empty(&global_gc_worklist);
    processor->gc_work_finished = memory->gc_count;

    uv_key_set(&tls_processor_key, NULL);
    uv_key_set(&tls_coroutine_key, NULL);
    slot = old_pointer;
    rti_write_barrier_ptr(&slot, new_pointer, false);
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_pointer(&global_gc_worklist, new_pointer);
    expect_empty(&global_gc_worklist);
    uv_key_set(&tls_processor_key, processor);
    uv_key_set(&tls_coroutine_key, coroutine);

    slot = old_pointer;
    rti_write_barrier_ptr(&slot, marked_pointer, true);
    check(slot == marked_pointer, "pooled-object store failed");
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_empty(&global_gc_worklist);
    mspan_t *span = span_of((addr_t) marked_pointer);
    uint64_t index = ((addr_t) marked_pointer - span->base) / span->obj_size;
    check(bitmap_test(span->gcmark_bits, index), "pooled object was not marked");
    rti_write_barrier_ptr(&slot, NULL, false);
    check(slot == NULL, "null store failed");
    expect_empty(&global_gc_worklist);
}

static void check_typed_barrier(coroutine_t *coroutine, void *old_pointer, void *new_pointer, void *numeric) {
    rtype_t mixed = GC_RTYPE(TYPE_STRUCT, 3, TYPE_GC_NOSCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, mixed.hash, &mixed);
    uint64_t source[3] = {(uint64_t) numeric, (uint64_t) new_pointer, (uint64_t) numeric};
    uint64_t destination[3] = {(uint64_t) numeric, (uint64_t) old_pointer, (uint64_t) numeric};
    coroutine->gc_black = 0;
    rti_write_barrier_rtype(destination, source, &mixed);
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_pointer(&global_gc_worklist, new_pointer);
    expect_empty(&global_gc_worklist);
    check(memcmp(destination, source, sizeof(source)) == 0, "typed copy failed");

    coroutine->gc_black = memory->gc_count;
    destination[1] = (uint64_t) old_pointer;
    rti_write_barrier_rtype(destination, source, &mixed);
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_empty(&global_gc_worklist);

    coroutine->gc_black = 0;
    rti_write_barrier_rtype(destination, destination, &mixed);
    expect_empty(&global_gc_worklist);
    rti_shade_rtype(destination, &mixed);
    memset(destination, 0, sizeof(destination));
    expect_pointer(&global_gc_worklist, new_pointer);
    expect_empty(&global_gc_worklist);

    uint8_t bits[16] = {0};
    bitmap_set(bits, 65);
    uint8_t *saved_data = rt_data_ptr;
    rt_data_ptr = bits;
    rtype_t wide = {
            .storage_size = 66 * POINTER_SIZE,
            .last_ptr = 66 * POINTER_SIZE,
            .malloc_gc_bits_offset = 0,
    };
    uint64_t wide_source[66] = {0};
    uint64_t wide_destination[66] = {0};
    wide_source[0] = wide_destination[0] = (uint64_t) numeric;
    wide_source[65] = (uint64_t) new_pointer;
    wide_destination[65] = (uint64_t) old_pointer;
    rti_write_barrier_rtype(wide_destination, wide_source, &wide);
    expect_pointer(&global_gc_worklist, old_pointer);
    expect_pointer(&global_gc_worklist, new_pointer);
    expect_empty(&global_gc_worklist);
    check(memcmp(wide_destination, wide_source, sizeof(wide_source)) == 0, "external bitmap copy failed");
    rt_data_ptr = saved_data;
}

static void check_vec_barrier(coroutine_t *coroutine, void *first, void *second, void *third) {
    coroutine->gc_black = 0;
    rtype_t pointer = GC_RTYPE(TYPE_PTR, 1, TYPE_GC_SCAN);
    sc_map_put_64v(&rt_rtype_map, pointer.hash, &pointer);
    void *values[3] = {first, second, third};
    n_vec_t source = vector(values, 2, sizeof(void *));
    n_vec_t destination = vector(values + 1, 2, sizeof(void *));
    check(rt_vec_copy(&destination, &source, pointer.hash) == 2, "overlap copy count");
    expect_pointer(&global_gc_worklist, second);
    expect_pointer(&global_gc_worklist, first);
    expect_pointer(&global_gc_worklist, third);
    expect_pointer(&global_gc_worklist, second);
    expect_empty(&global_gc_worklist);
    check(values[0] == first && values[1] == first && values[2] == second, "right overlap corrupted");

    source = vector(values + 1, 2, sizeof(void *));
    destination = vector(values, 2, sizeof(void *));
    check(rt_vec_copy(&destination, &source, pointer.hash) == 2, "reverse overlap count");
    expect_pointer(&global_gc_worklist, first);
    expect_pointer(&global_gc_worklist, first);
    expect_pointer(&global_gc_worklist, first);
    expect_pointer(&global_gc_worklist, second);
    expect_empty(&global_gc_worklist);
    check(values[0] == first && values[1] == second && values[2] == second, "left overlap corrupted");

    uint64_t numbers[2] = {(uint64_t) first, (uint64_t) second};
    uint64_t zeros[2] = {0};
    destination = vector(numbers, 2, sizeof(uint64_t));
    source = vector(zeros, 2, sizeof(uint64_t));
    check(rt_vec_copy(&destination, &source, uint64_rtype.hash) == 2, "numeric copy count");
    expect_empty(&global_gc_worklist);
    numbers[0] = (uint64_t) first;
    numbers[1] = (uint64_t) second;
    destination = vector(numbers, sizeof(numbers), 1);
    source = vector(zeros, sizeof(zeros), 1);
    check(rt_vec_copy(&destination, &source, string_element_rtype.hash) == sizeof(numbers), "byte copy count");
    expect_empty(&global_gc_worklist);

    void *appended[2] = {NULL, NULL};
    void *incoming[2] = {first, second};
    destination = vector(appended, 0, sizeof(void *));
    destination.capacity = 2;
    source = vector(incoming, 2, sizeof(void *));
    rt_vec_append(&destination, &source, pointer.hash);
    expect_pointer(&global_gc_worklist, first);
    expect_pointer(&global_gc_worklist, second);
    expect_empty(&global_gc_worklist);
    check(destination.length == 2 && appended[0] == first && appended[1] == second, "append failed");

    destination.length = 0;
    check(rt_vec_copy(&destination, &source, 0) == 0, "empty copy must skip type lookup");
    gc_barrier_stop();
    destination.length = 2;
    check(rt_vec_copy(&destination, &source, 0) == 2, "disabled barrier must skip type lookup");
    expect_empty(&global_gc_worklist);
}

#ifdef __DARWIN
void test_main(void) __asm("_main.main");
#else
void test_main(void) __asm("main.main");
#endif

void test_main(void) {
    memory_init();
    check(uv_key_create(&tls_processor_key) == 0, "processor TLS initialization failed");
    check(uv_key_create(&tls_coroutine_key) == 0, "coroutine TLS initialization failed");
    n_processor_t *processor = calloc(1, sizeof(n_processor_t));
    check(processor != NULL, "processor allocation failed");
    coroutine_t coroutine = {0};
    uv_key_set(&tls_processor_key, processor);
    uv_key_set(&tls_coroutine_key, &coroutine);
    memory->gc_count = 1;
    processor->gc_work_finished = memory->gc_count;
    rt_linked_fixalloc_init(&global_gc_worklist);
    rt_linked_fixalloc_init(&processor->gc_worklist);

    void *first = rti_gc_malloc(16, NULL);
    void *second = rti_gc_malloc(16, NULL);
    void *third = rti_gc_malloc(16, NULL);
    void *marked = rti_gc_malloc(16, NULL);
    check_pointer_barrier(processor, &coroutine, first, second, marked);
    check_typed_barrier(&coroutine, first, second, third);
    check_vec_barrier(&coroutine, first, second, third);
    puts("hybrid write barrier checks passed");
}

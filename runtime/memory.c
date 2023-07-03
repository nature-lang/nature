#include "memory.h"

rtype_t *rt_find_rtype(uint64_t index) {
    return &rt_rtype_ptr[index];
}

uint64_t rt_rtype_heap_out_size(uint64_t index) {
    return rtype_heap_out_size(rt_find_rtype(index), POINTER_SIZE);
}

void fndefs_deserialize() {
    rt_fndef_ptr = &rt_fndef_data;

    DEBUGF("[fndefs_deserialize] rt_fndef_ptr addr: %p", rt_fndef_ptr);

    uint8_t *gc_bits_offset = ((uint8_t *) rt_fndef_ptr) + rt_fndef_count * sizeof(fndef_t);
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *f = &rt_fndef_ptr[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size, POINTER_SIZE);
        f->gc_bits = gc_bits_offset;

        DEBUGF("[fndefs_deserialize] name=%s, base=0x%lx, size=%lu, stack=%lu,"
               "fn_runtime_stack=0x%lx, fn_runtime_reg=0x%lx, gc_bits(%lu)=%s",
               f->name,
               f->base,
               f->size,
               f->stack_size,
               f->fn_runtime_stack,
               f->fn_runtime_reg,
               gc_bits_size,
               bitmap_to_str(f->gc_bits, f->stack_size / POINTER_SIZE));

        gc_bits_offset += gc_bits_size;
    }
}

/**
 * 链接器已经将 rt_type_data 和 rt_type_count 赋值完毕，
 * rt_type_data 应该直接可以使用,
 * 接下来也只需要对 gc bits 进行重定位即可
 */
void rtypes_deserialize() {
    rt_rtype_ptr = &rt_rtype_data;
    DEBUGF("[rtypes_deserialize] rt_fndef_ptr addr: %p", rt_rtype_ptr);


    uint8_t *gc_bits_offset = (uint8_t *) (rt_rtype_ptr + rt_rtype_count);
    uint64_t count = 0;
    for (int i = 0; i < rt_rtype_count; ++i) {
        rtype_t *r = &rt_rtype_ptr[i];
        uint64_t gc_bits_size = calc_gc_bits_size(r->size, POINTER_SIZE);

        r->gc_bits = gc_bits_offset;
        gc_bits_offset += gc_bits_size;
        count++;
    }
    DEBUGF("[rtypes_deserialize] count=%lu", count);
}

void symdefs_deserialize() {
    rt_symdef_ptr = &rt_symdef_data;
    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        DEBUGF("[runtime.symdefs_deserialize] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx",
               s.name, s.base, s.size, s.need_gc, fetch_int_value(s.base, s.size));
    }
}

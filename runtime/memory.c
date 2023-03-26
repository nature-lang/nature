#include "memory.h"

rtype_t *rt_find_rtype(uint64_t index) {
    return &rt_rtype_data[index];
}

uint64_t rt_rtype_heap_out_size(uint64_t index) {
    return rtype_heap_out_size(rt_find_rtype(index));
}

void fndefs_deserialize() {
    byte *gc_bits_offset = ((byte *) rt_fndef_data) + rt_fndef_count * sizeof(fndef_t);
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *f = &rt_fndef_data[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size);

        f->gc_bits = gc_bits_offset;

        gc_bits_offset += gc_bits_size;
    }
}

/**
 * 链接器已经将 rt_type_data 和 rt_type_count 赋值完毕，
 * rt_type_data 应该直接可以使用,
 * 接下来也只需要对 gc bits 进行重定位即可
 */
void rtypes_deserialize() {
    byte *gc_bits_offset = (byte *) (rt_rtype_data + rt_rtype_count);
    for (int i = 0; i < rt_rtype_count; ++i) {
        rtype_t *r = &rt_rtype_data[i];
        uint64_t gc_bits_size = calc_gc_bits_size(r->size);

        r->gc_bits = gc_bits_offset;
        gc_bits_offset += gc_bits_size;
    }
}

void symdefs_deserialize() {
    rt_symdef_data = rt_symdef_data;
}

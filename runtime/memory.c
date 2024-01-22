#include "memory.h"

#include "stdlib.h"

uint64_t remove_total_bytes = 0;    // 当前回收到物理内存中的总空间
uint64_t allocated_total_bytes = 0; // 当前分配的总空间
uint64_t allocated_bytes = 0;       // 当前分配的内存空间
uint64_t next_gc_bytes = 0;         // 下一次 gc 的内存量
bool gc_barrier;                    // gc 屏障开启标识
uint8_t gc_stage;                   // gc 阶段
mutex_t *gc_stage_locker;

memory_t *memory;

rtype_t *rt_find_rtype(uint32_t rtype_hash) {
    PREEMPT_LOCK();
    char *str = itoa(rtype_hash);
    rtype_t *result = table_get(rt_rtype_table, str);
    free(str);

    PREEMPT_UNLOCK();
    return result;
}

uint64_t rt_rtype_out_size(uint32_t rtype_hash) {
    safe_assertf(rtype_hash > 0, "rtype_hash empty");

    rtype_t *rtype = rt_find_rtype(rtype_hash);

    safe_assertf(rtype, "cannot find rtype by hash %d", rtype_hash);

    return rtype_out_size(rtype, POINTER_SIZE);
}

void fndefs_deserialize() {
    rt_fndef_ptr = &rt_fndef_data;

    DEBUGF("[fndefs_deserialize] rt_fndef_ptr addr: %p", rt_fndef_ptr);

    uint8_t *gc_bits_offset = ((uint8_t *) rt_fndef_ptr) + rt_fndef_count * sizeof(fndef_t);
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *f = &rt_fndef_ptr[i];
        uint64_t gc_bits_size = calc_gc_bits_size(f->stack_size, POINTER_SIZE);
        f->gc_bits = gc_bits_offset;

        DEBUGF(
                "[fndefs_deserialize] name=%s, base=0x%lx, size=%lu, stack_size=%lu,"
                "fn_runtime_stack=0x%lx, fn_runtime_reg=0x%lx, gc_bits(%lu)=%s",
                f->name, f->base, f->size, f->stack_size, f->fn_runtime_stack, f->fn_runtime_reg, gc_bits_size,
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
    RDEBUGF("[rtypes_deserialize] start");

    rt_rtype_table = table_new();

    rtype_t *rt_rtype_ptr = &rt_rtype_data;

    uint8_t *gc_bits_offset = (uint8_t *) (rt_rtype_ptr + rt_rtype_count);

    // gc bits
    uint64_t count = 0;
    for (int i = 0; i < rt_rtype_count; ++i) {
        rtype_t *r = &rt_rtype_ptr[i];
        uint64_t gc_bits_size = calc_gc_bits_size(r->size, POINTER_SIZE);

        r->gc_bits = gc_bits_offset;
        gc_bits_offset += gc_bits_size;
        count++;
    }

    // elements
    for (int i = 0; i < rt_rtype_count; ++i) {
        rtype_t *r = &rt_rtype_ptr[i];

        if (r->length > 0) {
            uint64_t size = r->length * sizeof(uint64_t);
            r->element_hashes = (uint64_t *) gc_bits_offset;
            gc_bits_offset += size;
        }

        // rtype 已经组装完毕，现在加入到 rtype table 中
        char *str = safe_itoa(r->hash);
        bool is_new_key = table_set(rt_rtype_table, str, r);
        RDEBUGF("[rtypes_deserialize] hash=%s to table success, kind=%s, is_new_key=%d", str, type_kind_str[r->kind],
                is_new_key);
        safe_free(str);
    }

    RDEBUGF("[rtypes_deserialize] count=%lu", count);
}

void symdefs_deserialize() {
    rt_symdef_ptr = &rt_symdef_data;
    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        DEBUGF("[runtime.symdefs_deserialize] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx",
               s.name, s.base,
               s.size, s.need_gc, fetch_int_value(s.base, s.size));
    }
}

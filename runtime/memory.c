#include "memory.h"

#include <stdlib.h>


uint64_t remove_total_bytes = 0; // 当前回收到物理内存中的总空间
uint64_t allocated_total_bytes = 0; // 当前分配的总空间
int64_t allocated_bytes = 0; // 当前分配的内存空间
uint64_t next_gc_bytes = 0; // 下一次 gc 的内存量
bool gc_barrier; // gc 屏障开启标识

uint8_t gc_stage; // gc 阶段
mutex_t gc_stage_locker;

memory_t *memory;

void callers_deserialize() {
    sc_map_init_64v(&rt_caller_map, rt_caller_count * 2, 0);

    rt_caller_ptr = &rt_caller_data;
    DEBUGF("[runtime.callers_deserialize] count=%lu", rt_caller_count);

    // 生成 table 数据，可以通过 ret_addr 快速定位到 caller, caller 可以快速定位到 fn
    for (int i = 0; i < rt_caller_count; ++i) {
        caller_t *caller = &rt_caller_ptr[i];
        assert(((uint64_t) caller->data) >= 0);
        fndef_t *f = &rt_fndef_ptr[(uint64_t) caller->data];
        caller->offset += f->base; // ret addr
        caller->data = f;

        sc_map_put_64v(&rt_caller_map, caller->offset, caller);
        DEBUGF("[runtime.callers_deserialize] call '%s' ret_addr=%p, in fn=%s(%p), file=%s",
               STRTABLE(caller->target_name_offset),
               (void *) caller->offset, STRTABLE(f->name_offset),
               (void *) f->base,
               STRTABLE(f->relpath_offset));
    }
}

void fndefs_deserialize() {
    rt_fndef_ptr = &rt_fndef_data;
    // debug
    //    for (int i = 0; i < rt_fndef_count; ++i) {
    //        fndef_t *fn = &rt_fndef_ptr[i];
    //        TDEBUGF("[fndefs_deserialize] fn base %p, name %s ", (void *) fn->base, STRTABLE(fn->name_offset));
    //    }

    DEBUGF("[fndefs_deserialize] rt_fndef_ptr addr: %p", rt_fndef_ptr);
}

/**
 * 链接器已经将 rt_type_data 和 rt_type_count 赋值完毕，
 * rt_type_data 应该直接可以使用,
 * 接下来也只需要对 gc bits 进行重定位即可
 */
void rtypes_deserialize() {
    RDEBUGF("[rtypes_deserialize] start");
    sc_map_init_64v(&rt_rtype_map, rt_rtype_count * 2, 0);
    builtin_rtype_init();

    rtype_t *rt_rtype_ptr = &rt_rtype_data;

    // element_hashs
    for (int i = 0; i < rt_rtype_count; ++i) {
        rtype_t *r = &rt_rtype_ptr[i];
        //  常用 rtype 注册覆盖
        if (r->kind == TYPE_UINT8) {
            string_element_rtype = *r;
        } else if (r->kind == TYPE_STRING) {
            string_rtype = *r;
            string_ref_rtype = *r;
            std_arg_rtype = *r;
            os_env_rtype = *r;
        } else if (r->kind == TYPE_VEC) {
            vec_rtype = *r;
        } else if (r->kind == TYPE_GC_FN) {
            fn_rtype = *r;
        } else if (str_equal(STRTABLE(r->ident_offset), THROWABLE_IDENT)) {
            throwable_rtype = *r;
        }

        // rtype 已经组装完毕，现在加入到 rtype table 中
        sc_map_put_64v(&rt_rtype_map, r->hash, r);
        RDEBUGF("[rtypes_deserialize] hash=%ld to table success, kind=%s", r->hash, type_kind_str[r->kind]);
    }
}

void symdefs_deserialize() {
    rt_symdef_ptr = &rt_symdef_data;
    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        DEBUGF("[runtime.symdefs_deserialize] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx",
               STRTABLE(s.name_offset), s.base,
               s.size, s.need_gc, fetch_int_value(s.base, s.size));
    }
}

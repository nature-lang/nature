#include "fn.h"

#include "array.h"
#include "runtime/runtime.h"

#ifdef __AMD64

table_t *env_upvalue_table;
mutex_t env_upvalue_locker;

/**
 * 假设 addr = 0x40007fffb8
 * 汇编为
 * mov rax,0x40007fffb8
 * jmp rax
 * 机器码为
 *
 * { 0x48, 0xB8, 0xB8, 0xFF, 0x7F, 0x00, 0x40, 0x00, 0x00, 0x00, 0xFF, 0xE0 }
 * @return
 */
static uint8_t gen_jmp_addr_codes(uint8_t *codes, uint64_t addr) {
    codes[0] = 0x48;
    codes[1] = 0xB8;
    *((uint64_t *) (codes + 2)) = addr;
    codes[10] = 0xFF;
    codes[11] = 0xE0;
    return 12;
}

/**
 * fn_addr = 0x40007fffb8
 * stack_offset = 0x10
 * 则汇编指令为, amd64 不能直接将 imm64 移动到 rm64 中，所以需要固定寄存器 rax 做中专
 * mov  rax,0x40007fffb8
 * mov [rbp+10],rax
 *
 * 最终到机器码则是
 * { 0x48, 0xB8, 0xB8, 0xFF, 0x7F, 0x00, 0x40, 0x00, 0x00, 0x00, 0x48, 0x89, 0x45, 0x0A }
 *
 * 示例 mov
 * @param codes
 * @param stack_offset
 * @param fn_addr
 * @return
 */
static uint8_t gen_mov_stack_codes(uint8_t *codes, uint64_t stack_offset, uint64_t fn_runtime_ptr) {
    // 将 imm64 移动到寄存器 rax 中
    codes[0] = 0x48;
    codes[1] = 0xB8;
    memcpy(&codes[2], &fn_runtime_ptr, sizeof(uint64_t));// 2 ~ 8
    // 将寄存器 rax 中的值移动到 [rbp+stack_offset] 的地址中
    codes[10] = 0x48;
    codes[11] = 0x89;
    codes[12] = 0x45;
    codes[13] = (uint8_t) stack_offset;

    return 14;
}

static uint8_t gen_mov_reg_codes(uint8_t *codes, uint64_t reg_index, uint64_t fn_runtime_ptr) {
    // RDI = 7、RSI = 6、RDX = 2、RCX = 1、R8 = 8、R9 = 9
    uint8_t *imm = (uint8_t *) (&fn_runtime_ptr);
    switch (reg_index) {
        case 7: {// rdi
            codes[0] = 0x48;
            codes[1] = 0xBF;
            break;
        }
        case 6: {// rsi
            codes[0] = 0x48;
            codes[1] = 0xBE;
            break;
        }
        case 2: {// rdx
            codes[0] = 0x48;
            codes[1] = 0xBA;
            break;
        }
        case 1: {// rcx
            codes[0] = 0x48;
            codes[1] = 0xB9;
            break;
        }
        case 8: {// r8
            codes[0] = 0x49;
            codes[1] = 0xB8;
            break;
        }
        case 9: {// r9
            codes[0] = 0x49;
            codes[1] = 0xB9;
            break;
        }
        default:
            assert(false && "cannot use reg_index=%s in fn param" && reg_index);
            exit(1);
    }

    // 将地址按 little-endian 方式存储
    memcpy(&codes[2], &fn_runtime_ptr, sizeof(uint64_t));

    return 10;
}

static void gen_closure_jit_codes(fndef_t *fndef, runtime_fn_t *fn_runtime_ptr, addr_t fn_addr) {
    uint8_t codes[100] = {0};
    uint64_t size = 0;

    uint8_t temp_codes[50];
    uint8_t count;
    if (fndef->fn_runtime_reg > 0) {
        count = gen_mov_reg_codes(temp_codes, fndef->fn_runtime_reg, (uint64_t) fn_runtime_ptr);
    } else {
        count = gen_mov_stack_codes(temp_codes, fndef->fn_runtime_reg, (uint64_t) fn_runtime_ptr);
    }
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }
    count = gen_jmp_addr_codes(temp_codes, fn_addr);
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }

    memcpy(fn_runtime_ptr->closure_jit_codes, codes, size);
}

#else
static void gen_closure_jit_codes(fndef_t *fndef, runtime_fn_t *fn_runtime, addr_t fn_addr) {
    assert(false && "gen_closure_jit_codes cannot support arch");
}
#endif

void *fn_new(addr_t fn_addr, envs_t *envs) {
    //    PRE_RTCALL_HOOK(); // env_new 已经设置 pre_rt_call_hook，此处不需要重复设置
    processor_t *p = processor_get();
    assert(p->status == P_STATUS_RTCALL);

    DEBUGF("[runtime.fn_new] fn_addr=0x%lx, envs=%p", fn_addr, envs);
    assert(envs);
    rtype_t *fn_rtype = gc_rtype_array(TYPE_GC_FN, sizeof(runtime_fn_t) / POINTER_SIZE);
    // 手动设置 gc 区域, runtime_fn_t 一共是 12 个 pointer 区域，最后一个区域保存的是 envs 需要扫描
    // 其他区域都不需要扫面
    bitmap_set(fn_rtype->gc_bits, 11);
    fn_rtype->last_ptr = sizeof(runtime_fn_t);// 也就是最后一个内存位置包含了指针

    runtime_fn_t *fn_runtime = rt_clr_malloc(sizeof(runtime_fn_t), fn_rtype);
    fn_runtime->fn_addr = fn_addr;

    // fn_runtime->envs = envs;
    write_barrier(&fn_runtime->envs, &envs);

    // 基于 jit 返回一个可以直接被外部 call 的 fn_addr
    fndef_t *fndef = find_fn(fn_addr);
    assert(fndef && "cannot find fn by addr");

    gen_closure_jit_codes(fndef, fn_runtime, fn_addr);

    DEBUGF("[runtime.fn_new] fn find success, fn=%p, stack=%lu, reg=%lu, jit_code=%p, envs=%p, fn_addr=%p",
           fn_runtime, fndef->fn_runtime_stack, fndef->fn_runtime_reg, fn_runtime->closure_jit_codes, fn_runtime->envs, (void *) fn_addr);

    /**
     * closure_jit_codes 就是 fn_runtime 对首个值，所以 return fn_runtime 和 fn_runtime->closure_jit_codes 没有区别
     * 都是堆内存区域对首个地址, 所以将返回值当成数据参数或者时 call label 都是可以的.
     *
     */
    assert((void *) fn_runtime == (void *) fn_runtime->closure_jit_codes && "fn_new base must equal fn_runtime first property");
    return fn_runtime->closure_jit_codes;
}

envs_t *env_new(uint64_t length, bool imm_close) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.env_new] length=%lu, %p", length, env_upvalue_table);
    assert(env_upvalue_table);

    // upvalue
    rtype_t *element_rtype = gc_rtype(TYPE_GC_ENV_VALUE, 1, TYPE_GC_SCAN);

    rtype_t *envs_rtype = gc_rtype(TYPE_GC_ENV, 3, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    envs_t *envs = rt_clr_malloc(sizeof(envs_t), envs_rtype);
    envs->values = (void *) rt_array_new(element_rtype, length);
    envs->length = length;
    envs->imm_close = imm_close;

    DEBUGF("[runtime.env_new] success,env_base=%p, values_base=%p, length=%lu", envs, envs->values, envs->length);
    return envs;
}

void env_assign(envs_t *envs, uint64_t rtype_hash, uint64_t env_index, addr_t stack_addr) {
    //    PRE_RTCALL_HOOK(); env_new 已经设置，此处不需要重复设置
    processor_t *p = processor_get();
    assert(p->status == P_STATUS_RTCALL);

    rtype_t *rtype = rt_find_rtype(rtype_hash);

    DEBUGF("[runtime.env_assign] env_base=%p, rtype_kind=%s, rtype_size=%lu, env_index=%lu, stack_addr=%p, value=%p", envs,
           type_kind_str[rtype->kind], rtype->size, env_index, (void *) stack_addr, (void *) fetch_addr_value(stack_addr));

    if (envs->imm_close) {
        rtype_t *upvalue_rtype = gc_rtype(TYPE_GC_UPVALUE, 2, to_gc_kind(rtype->kind), TYPE_GC_SCAN);
        upvalue_t *upvalue = rt_clr_malloc(sizeof(upvalue_t), upvalue_rtype);

        if (rtype->size <= 8) {
            uint64_t value = fetch_addr_value(stack_addr);
            upvalue->value.uint_value = value;
            upvalue->ref = &upvalue->value;
        } else {
            // 如果 rtype->size > 8, 则需要从堆中申请空间存放 struct, 避免空间不足。
            void *new_value = rt_clr_malloc(rtype->size, rtype);
            DEBUGF("[runtime.env_closure] size gt 8byte, malloc new_value=%p", new_value);
            memcpy(new_value, (void *) stack_addr, rtype->size);
            upvalue->ref = new_value;
        }

        envs->values[env_index] = upvalue;
        return;
    }

    mutex_lock(&env_upvalue_locker);
    upvalue_t *upvalue = table_get(env_upvalue_table, utoa(stack_addr));
    if (!upvalue) {
        // TYPE_GC_NOSCAN=upvalue.value, TYPE_GC_SCAN=upvalue.ref, ref 在 closure 后会指向 value 部分
        // 所以总是将 ref 部分设置为 scan
        DEBUGF("[runtime.env_assign] not found upvalue by stack_addr=0x%lx, will create", stack_addr);
        rtype_t *upvalue_rtype = gc_rtype(TYPE_GC_UPVALUE, 2, to_gc_kind(rtype->kind), TYPE_GC_SCAN);
        upvalue = rt_clr_malloc(sizeof(upvalue_t), upvalue_rtype);
        DEBUGF("[runtime.env_assign] upvalue addr=%p, upvalue_rtype.hash=%ld", upvalue, upvalue_rtype->hash);

        table_set(env_upvalue_table, utoa(stack_addr), upvalue);
        DEBUGF("[runtime.env_assign] table set success");

        upvalue->ref = (void *) stack_addr;
        DEBUGF("[runtime.env_assign] upvalue %p created", upvalue);
    } else {
        DEBUGF("[runtime.env_assign] upvalue=%p already in table", upvalue);
    }

    mutex_unlock(&env_upvalue_locker);

    assert(envs->values);
    envs->values[env_index] = upvalue;
}

/*
 * stack 需要被回收，但是 stack addr 缺被引用了，此时需要将 stack addr 的值 copy 到 upvalue 中
 */
void env_closure(uint64_t stack_addr, uint64_t rtype_hash) {
    PRE_RTCALL_HOOK();

    mutex_lock(&env_upvalue_locker);

    upvalue_t *upvalue = table_get(env_upvalue_table, utoa(stack_addr));
    assert(upvalue && "not found stack addr upvalue, cannot close");
    DEBUGF("[runtime.env_closure] stack_addr=0x%lx, find_upvalue=%p, upvalue->ref=%p, rtype_hash=%lu", stack_addr, upvalue, upvalue->ref,
           rtype_hash);

    rtype_t *rtype = rt_find_rtype(rtype_hash);

    if (rtype->size <= 8) {
        uint64_t value = fetch_addr_value(stack_addr);
        upvalue->value.uint_value = value;
        upvalue->ref = &upvalue->value;
    } else {
        // 如果 rtype->size > 8, 则需要从堆中申请空间存放 struct, 避免空间不足。
        void *new_value = rt_clr_malloc(rtype->size, rtype);
        DEBUGF("[runtime.env_closure] size gt 8byte, malloc new_value=%p", new_value);
        memcpy(new_value, (void *) stack_addr, rtype->size);
        upvalue->ref = new_value;
    }

    DEBUGF("[runtime.env_closure] closure success, upvalue->ref=%p stack_addr=0x%lx, ", upvalue->ref, stack_addr);

    table_delete(env_upvalue_table, utoa(stack_addr));

    mutex_unlock(&env_upvalue_locker);
    post_rtcall_hook("env_closure");
}

void env_access_ref(runtime_fn_t *fn, uint64_t index, void *dst_ref, uint64_t size) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.env_access_ref] fn_base=%p", fn);

    assert(index < fn->envs->length);
    upvalue_t *upvalue = fn->envs->values[index];

    DEBUGF("[runtime.env_access_ref] fn_base=%p, fn->envs_base=%p, index=%lu, dst_ref=%p, size=%lu, env_int_value=0x%lx", fn, fn->envs,
           index, dst_ref, size, fetch_int_value((addr_t) upvalue->ref, size));

    memmove(dst_ref, upvalue->ref, size);
}

void env_assign_ref(runtime_fn_t *fn, uint64_t index, void *src_ref, uint64_t size) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.env_assign_ref] fn_base=%p, index=%lu, src_ref=%p, size=%lu", fn, index, src_ref, size);
    assert(index < fn->envs->length);
    assert(fn);

    upvalue_t *upvalue = fn->envs->values[index];

    memmove(upvalue->ref, src_ref, size);
}

void *env_element_addr(runtime_fn_t *fn, uint64_t index) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.env_element_addr] fn_base=%p, envs=%p, envs.length=%lu, index=%lu, fn_addr=%p", fn, fn->envs, fn->envs->length, index, (void *) fn->fn_addr);
    assert(fn);
    assertf(index < fn->envs->length, "index out of range, fn=%p, envs=%p", fn, fn->envs);

    upvalue_t *upvalue = fn->envs->values[index];

    // ref 保存了一个地址，指向了可以存储的数据的初始位置
    // 在 closure 之前，ref 指向栈地址
    // closure 之后，ref 指向自身的 value 部分的地址, value 部分则保存具体的值
    // value 只能保存 8byte 空间，如果需要的值超过 8byte，则需要从堆中从新申请空间
    DEBUGF("[runtime.env_element_addr] ref=%p, ref_value=%p", upvalue->ref, (void *) fetch_addr_value((addr_t) upvalue->ref));

    return upvalue->ref;
}

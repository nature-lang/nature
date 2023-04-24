#include "fn.h"
#include "runtime/allocator.h"
#include "runtime/collector.h"
#include "array.h"

/**
 * 绝对地址跳转
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
static uint8_t gen_mov_stack_codes(uint8_t *codes, uint64_t stack_offset, uint64_t fn_runtime) {
    // 将 imm64 移动到寄存器 rax 中
    codes[0] = 0x48;
    codes[1] = 0xB8;
    memcpy(&codes[2], &fn_runtime, sizeof(uint64_t)); // 2 ~ 8
    // 将寄存器 rax 中的值移动到 [rbp+stack_offset] 的地址中
    codes[10] = 0x48;
    codes[11] = 0x89;
    codes[12] = 0x45;
    codes[13] = (uint8_t) stack_offset;

    return 14;
}

static uint8_t gen_mov_reg_codes(uint8_t *codes, uint64_t reg_index, uint64_t fn_runtime) {
    // RDI = 7、RSI = 6、RDX = 2、RCX = 1、R8 = 8、R9 = 9
    uint8_t *imm = (uint8_t *) (&fn_runtime);
    switch (reg_index) {
        case 7: { // rdi
            codes[0] = 0x48;
            codes[1] = 0xBF;
            break;
        }
        case 6: { // rsi
            codes[0] = 0x48;
            codes[1] = 0xBE;
            break;
        }
        case 2: { // rdx
            codes[0] = 0x48;
            codes[1] = 0xBA;
            break;
        }
        case 1: { // rcx
            codes[0] = 0x48;
            codes[1] = 0xB9;
            break;
        }
        case 8: { // r8
            codes[0] = 0x49;
            codes[1] = 0xB8;
            break;
        }
        case 9: { // r9
            codes[0] = 0x49;
            codes[1] = 0xB9;
            break;
        }
        default:
            assertf(false, "cannot use reg_index=%s in fn param", reg_index);
            exit(1);
    }

    // 将地址按 little-endian 方式存储
    memcpy(&codes[2], &fn_runtime, sizeof(uint64_t));

    return 10;
}

static void gen_closure_jit_codes(fndef_t *fndef, runtime_fn_t *fn_runtime, addr_t fn_addr) {
    uint8_t codes[100] = {0};
    uint64_t size = 0;

    uint8_t temp_codes[50];
    uint8_t count;
    if (fndef->fn_runtime_reg > 0) {
        count = gen_mov_reg_codes(temp_codes, fndef->fn_runtime_reg, (uint64_t) fn_runtime);
    } else {
        count = gen_mov_stack_codes(temp_codes, fndef->fn_runtime_reg, (uint64_t) fn_runtime);
    }
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }
    count = gen_jmp_addr_codes(temp_codes, fn_addr);
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }

    uint8_t *jit_codes = runtime_malloc(size, NULL);
    memcpy(jit_codes, codes, size);
    fn_runtime->closure_jit_codes = jit_codes;
    fn_runtime->code_size = size;
}

void *fn_new(addr_t fn_addr, envs_t *envs) {
    assert(envs);
    rtype_t fn_rtype = rt_tuple_rtype(4, TYPE_POINTER, TYPE_POINTER, TYPE_INT64, TYPE_INT64);
    runtime_fn_t *fn_runtime = runtime_malloc(sizeof(runtime_fn_t), &fn_rtype);
    free(fn_rtype.gc_bits);
    fn_runtime->fn_addr = fn_addr;
    fn_runtime->envs = envs;


    // 基于 jit 返回一个可以直接被外部 call 的 fn_addr
    fndef_t *fndef = find_fn(fn_addr);
    assertf(fndef, "cannot find fn by addr=%lx", fn_addr);
    gen_closure_jit_codes(fndef, fn_runtime, fn_addr);

    DEBUGF("[runtime.fn_new] fn_runtime_base=%p, fn_addr=0x%lx, envs_base=%p, fndef.fn_runtime_stack=%lu, fndef.fn_runtime_reg=%lu, jit_code=%p",
           fn_runtime,
           fn_addr,
           envs,
           fndef->fn_runtime_stack,
           fndef->fn_runtime_reg,
           fn_runtime->closure_jit_codes)

    return fn_runtime->closure_jit_codes;
}

envs_t *env_new(int length) {
    if (env_table == NULL) {
        env_table = table_new();
    }

    rtype_t element_rtype = rt_reflect_type(type_basic_new(TYPE_POINTER));
    void *values = array_new(&element_rtype, length);

    rtype_t envs_rtype = rt_tuple_rtype(2, TYPE_POINTER, TYPE_INT64);
    envs_t *envs = runtime_malloc(sizeof(envs_t), &envs_rtype);
    free(envs_rtype.gc_bits);
    envs->length = length;
    envs->values = values;

    return envs;
}

void env_assign(envs_t *envs, uint64_t item_rtype_index, uint64_t index, addr_t stack_addr) {
    upvalue_t *upvalue = table_get(env_table, utoa(stack_addr));
    rtype_t *item_rtype = rt_find_rtype(item_rtype_index);
    if (!upvalue) {
        // create upvalue_t
        rtype_t upvalue_rtype = rt_tuple_rtype(2, item_rtype->kind, TYPE_INT64);
        upvalue = runtime_malloc(sizeof(upvalue_t), &upvalue_rtype);
        free(upvalue_rtype.gc_bits);
        table_set(env_table, utoa(stack_addr), upvalue);
        upvalue->ref = (void *) stack_addr;
    }

    envs->values[index] = upvalue;
}

void env_close(uint64_t stack_addr) {
    upvalue_t *upvalue = table_get(env_table, utoa(stack_addr));
    assertf(upvalue, "not found stack addr=%p upvalue, cannot close", stack_addr);
    // 无论值的大小，同一按 8byte copy 就是了， copy 多了也没事
    uint64_t value = fetch_addr_value(stack_addr);
    upvalue->value.uint_value = value;
    upvalue->ref = &upvalue->value;

}

void env_access_ref(runtime_fn_t *fn, uint64_t index, void *dst_ref, uint64_t size) {
    DEBUGF("[env_access_ref] fn_base=%p, fn->envs_base=%p, index=%lu, dst_ref=%p, size=%lu",
           fn, fn->envs, index, dst_ref, size);
    assert(index >= fn->envs->length);
    upvalue_t *upvalue = fn->envs->values[index];

    memmove(dst_ref, upvalue->ref, size);
}

void env_assign_ref(runtime_fn_t *fn, uint64_t index, void *src_ref, uint64_t size) {
    DEBUGF("[env_access_ref] fn_base=%p, fn->envs_base=%p, index=%lu, src_ref=%p, size=%lu",
           fn, fn->envs, index, src_ref, size);
    assert(index >= fn->envs->length);

    upvalue_t *upvalue = fn->envs->values[index];

    memmove(upvalue->ref, src_ref, size);
}
#include "fn.h"

#include "array.h"
#include "runtime/rtype.h"
#include "runtime/runtime.h"
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __DARWIN
#include <libkern/OSCacheControl.h> // 这是需要的头文件
#endif

table_t *env_upvalue_table = NULL;
mutex_t env_upvalue_locker = {0};


#ifdef __AMD64

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
    DEBUGF("[gen_mov_stack_codes] stack_offset %ld, runtime_ptr %p", stack_offset, (void *) fn_runtime_ptr)

    // 将 imm64 移动到寄存器 rax 中
    codes[0] = 0x48;
    codes[1] = 0xB8;
    memcpy(&codes[2], &fn_runtime_ptr, sizeof(uint64_t)); // 2 ~ 8
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
            assert(false && "cannot use reg_index=%s in fn param" && reg_index);
            exit(1);
    }

    // 将地址按 little-endian 方式存储
    memcpy(&codes[2], &fn_runtime_ptr, sizeof(uint64_t));

    return 10;
}


#elif defined(__RISCV64)

/**
 * RISCV64 加载 64 位立即数并跳转
 * 例如加载地址 0x40007fffb8:
 * lui t0, %hi(addr)     // 加载高20位
 * addi t0, t0, %lo(addr) // 加载低12位
 * jalr x0, t0, 0        // 跳转到 t0 寄存器地址
 */
static uint8_t gen_jmp_addr_codes(uint8_t *codes, uint64_t addr) {
    // 使用 t0 寄存器 (x5) 作为临时寄存器
    uint32_t *instr = (uint32_t *)codes;

    // LUI t0, %hi(addr) - 加载高20位到 t0
    uint32_t hi20 = (addr + 0x800) >> 12; // 加 0x800 处理符号扩展
    instr[0] = 0x00000037 | (5 << 7) | (hi20 << 12); // LUI x5, imm

    // ADDI t0, t0, %lo(addr) - 加载低12位
    uint32_t lo12 = addr & 0xfff;
    instr[1] = 0x00000013 | (5 << 7) | (5 << 15) | (lo12 << 20); // ADDI x5, x5, imm

    // JALR x0, t0, 0 - 跳转到 t0 地址
    instr[2] = 0x00000067 | (0 << 7) | (5 << 15); // JALR x0, x5, 0

    return 12;
}

/**
 * RISCV64 版本的栈存储实现
 * fn_addr = 0x40007fffb8
 * stack_offset = 0x10
 *
 * 汇编指令为:
 * lui t0, %hi(addr)     // 加载地址高位到 t0
 * addi t0, t0, %lo(addr) // 加载地址低位
 * sd t0, offset(fp)     // 将 t0 存储到 fp+offset
 *
 * @param codes 输出的机器码缓冲区
 * @param stack_offset 栈偏移量
 * @param fn_runtime_ptr 要存储的函数指针
 * @return 返回生成的机器码长度(12字节)
 */
static uint8_t gen_mov_stack_codes(uint8_t *codes, uint64_t stack_offset, uint64_t fn_runtime_ptr) {
    uint32_t *instr = (uint32_t *)codes;

    // LUI t0, %hi(fn_runtime_ptr)
    uint32_t hi20 = (fn_runtime_ptr + 0x800) >> 12;
    instr[0] = 0x00000037 | (5 << 7) | (hi20 << 12); // LUI x5, imm

    // ADDI t0, t0, %lo(fn_runtime_ptr)
    uint32_t lo12 = fn_runtime_ptr & 0xfff;
    instr[1] = 0x00000013 | (5 << 7) | (5 << 15) | (lo12 << 20); // ADDI x5, x5, imm

    // SD t0, offset(fp) - 存储到栈
    // fp 是 x8 寄存器，offset 需要编码到指令中
    uint32_t offset_hi = (stack_offset >> 5) & 0x7f;
    uint32_t offset_lo = stack_offset & 0x1f;
    instr[2] = 0x00003023 | (offset_lo << 7) | (8 << 15) | (5 << 20) | (offset_hi << 25); // SD x5, offset(x8)

    return 12;
}

/**
 * RISCV64 参数寄存器: a0-a7 (x10-x17)
 */
static uint8_t gen_mov_reg_codes(uint8_t *codes, uint64_t reg_index, uint64_t fn_runtime_ptr) {
    if (reg_index > 7) {
        assert(false && "RISCV64 only supports a0-a7 as parameter registers");
    }

    uint32_t *instr = (uint32_t *)codes;
    uint32_t target_reg = 10 + reg_index; // a0-a7 对应 x10-x17

    // LUI target_reg, %hi(fn_runtime_ptr)
    uint32_t hi20 = (fn_runtime_ptr + 0x800) >> 12;
    instr[0] = 0x00000037 | (target_reg << 7) | (hi20 << 12);

    // ADDI target_reg, target_reg, %lo(fn_runtime_ptr)
    uint32_t lo12 = fn_runtime_ptr & 0xfff;
    instr[1] = 0x00000013 | (target_reg << 7) | (target_reg << 15) | (lo12 << 20);

    return 8;
}


#else
/**
 * ARM64 加载 64 位立即数需要使用多条指令
 * 例如加载地址 0x40007fffb8:
 * movz x16, #0xffb8, lsl #0
 * movk x16, #0x7fff, lsl #16
 * movk x16, #0x4000, lsl #32
 * br x16
 */
static uint8_t gen_jmp_addr_codes(uint8_t *codes, uint64_t addr) {
    // LDR X16, #8 (0x58000050)
    uint32_t ldr = 0x58000050;
    memcpy(codes, &ldr, 4);

    // BR X16 (0xD61F0200)
    uint32_t br = 0xD61F0200;
    memcpy(codes + 4, &br, 4);

    // 64位目标地址
    memcpy(codes + 8, &addr, 8);

    return 16;
}

/**
 * ARM64 版本的栈存储实现
 * fn_addr = 0x40007fffb8
 * stack_offset = 0x10
 *
 * 汇编指令为:
 * LDR X16, #8          // 加载地址到 X16 寄存器
 * STR X16, [FP, #16]   // 将 X16 中的值存储到 FP+16 的位置
 * .quad fn_addr        // 64位地址值
 *
 * 机器码为:
 * 58000050            // LDR X16, #8
 * F90008F0            // STR X16, [FP, #16]  (16 = stack_offset)
 * [8字节地址值]        // 目标地址
 *
 * @param codes 输出的机器码缓冲区
 * @param stack_offset 栈偏移量
 * @param fn_runtime_ptr 要存储的函数指针
 * @return 返回生成的机器码长度(16字节)
 */
static uint8_t gen_mov_stack_codes(uint8_t *codes, uint64_t stack_offset, uint64_t fn_runtime_ptr) {
    // LDR X16, #8 (0x58000050)
    uint32_t ldr = 0x58000050;
    memcpy(codes, &ldr, 4);

    // STR X16, [FP, #offset]
    // 注意：这里假设 stack_offset 是小于 4096 的正数
    // 基础指令是 0xF90000F0，需要将偏移量编码进去
    uint32_t str = 0xF90000F0 | ((stack_offset & 0x7F8) << 7);
    memcpy(codes + 4, &str, 4);

    // 64位目标地址
    memcpy(codes + 8, &fn_runtime_ptr, 8);

    return 16;
}

/**
 * ARM64 参数寄存器: x0-x7
 */
static uint8_t gen_mov_reg_codes(uint8_t *codes, uint64_t reg_index, uint64_t fn_runtime_ptr) {
    if (reg_index > 7) {
        assert(false && "ARM64 only supports x0-x7 as parameter registers");
    }

    // movz xN, #imm16_0
    uint32_t movz = 0xD2800000 | (reg_index & 0x1F) | ((fn_runtime_ptr & 0xFFFF) << 5);
    memcpy(codes, &movz, 4);

    // movk xN, #imm16_1, lsl #16
    uint32_t movk1 = 0xF2A00000 | (reg_index & 0x1F) | (((fn_runtime_ptr >> 16) & 0xFFFF) << 5);
    memcpy(codes + 4, &movk1, 4);

    // movk xN, #imm16_2, lsl #32
    uint32_t movk2 = 0xF2C00000 | (reg_index & 0x1F) | (((fn_runtime_ptr >> 32) & 0xFFFF) << 5);
    memcpy(codes + 8, &movk2, 4);

    return 12;
}

#endif

static void gen_closure_jit_codes(fndef_t *fndef, runtime_fn_t *fn_runtime_ptr, addr_t fn_addr) {
    uint8_t codes[100] = {0};
    uint64_t size = 0;

    DEBUGF("fn_runtime_reg=%lu, fn_runtime_stack=%lu", fndef->fn_runtime_reg, fndef->fn_runtime_stack);
    uint8_t temp_codes[50];
    uint8_t count;
    if (fndef->fn_runtime_stack > 0) {
        count = gen_mov_stack_codes(temp_codes, fndef->fn_runtime_stack, (uint64_t) fn_runtime_ptr);
    } else {
        count = gen_mov_reg_codes(temp_codes, fndef->fn_runtime_reg, (uint64_t) fn_runtime_ptr);
    }
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }
    count = gen_jmp_addr_codes(temp_codes, fn_addr);
    for (int i = 0; i < count; ++i) {
        codes[size++] = temp_codes[i];
    }

    memcpy(fn_runtime_ptr->closure_jit_codes, codes, size);
#ifdef __ARM64
#ifdef __DARWIN
    sys_icache_invalidate(fn_runtime_ptr->closure_jit_codes, size);
#elif defined(__LINUX)
    __builtin___clear_cache(fn_runtime_ptr->closure_jit_codes, fn_runtime_ptr->closure_jit_codes + size);
#endif
    // arm64 清理缓存
#endif
}

void *fn_new(addr_t fn_addr, envs_t *envs) {
    // // env_new 已经设置 pre_rt_call_hook，此处不需要重复设置
    n_processor_t *p = processor_get();
    //    assert(p->status == P_STATUS_RTCALL);

    DEBUGF("[runtime.fn_new] fn_addr=0x%lx, envs=%p", fn_addr, envs);
    assert(envs);

    runtime_fn_t *fn_runtime = rti_gc_malloc(sizeof(runtime_fn_t), &fn_rtype);

    // 启用 pthread_jit_write_protect_np 才能写入数据
#if defined(__DARWIN) && defined(__ARM64)
    pthread_jit_write_protect_np(false);
#endif

    fn_runtime->fn_addr = fn_addr;

    // fn_runtime->envs = envs;
    rti_write_barrier_ptr(&fn_runtime->envs, envs, false);

    // 基于 jit 返回一个可以直接被外部 call 的 fn_addr
    fndef_t *fndef = find_fn(fn_addr, p);
    assert(fndef && "cannot find fn by addr");

    gen_closure_jit_codes(fndef, fn_runtime, fn_addr);

    DEBUGF(
            "[runtime.fn_new] fn find success, fn_runtime=%p, fn_name=%s, stack=%lu, reg=%lu, jit_code=%p, envs=%p, fn_addr=%p",
            fn_runtime, STRTABLE(fndef->name_offset), fndef->fn_runtime_stack, fndef->fn_runtime_reg, fn_runtime->closure_jit_codes,
            fn_runtime->envs,
            (void *) fn_addr);

    /**
     * closure_jit_codes 就是 fn_runtime 对首个值，所以 return fn_runtime 和 fn_runtime->closure_jit_codes 没有区别
     * 都是堆内存区域对首个地址, 所以将返回值当成数据参数或者时 call label 都是可以的.
     *
     */
    //    assert((void *) fn_runtime == (void *) fn_runtime->closure_jit_codes &&
    //           "fn_new base must equal fn_runtime first property");

#if defined(__DARWIN) && defined(__ARM64)
    pthread_jit_write_protect_np(true); // 开启内存区域可执行能力，开启后内存区域不可写入, 不可清空
#endif


    return fn_runtime->closure_jit_codes;
}

envs_t *env_new(uint64_t length) {

    DEBUGF("[runtime.env_new] length=%lu, %p", length, env_upvalue_table);
    assert(env_upvalue_table);

    envs_t *envs = rti_gc_malloc(sizeof(envs_t), &envs_rtype);
    envs->values = (void *) rti_array_new(&env_value_rtype, length);
    envs->length = length;

    DEBUGF("[runtime.env_new] success,env_base=%p, values_base=%p, length=%lu", envs, envs->values, envs->length);
    return envs;
}

/*
 * stack 需要被回收，但是 stack addr 缺被引用了，此时需要将 stack addr 的值 copy 到 upvalue 中
 */
void env_closure(uint64_t stack_addr, uint64_t rtype_hash) {


    mutex_lock(&env_upvalue_locker);

    upvalue_t *upvalue = table_get(env_upvalue_table, utoa(stack_addr));
    assert(upvalue && "not found stack addr upvalue, cannot close");
    DEBUGF("[runtime.env_closure] stack_addr=0x%lx, find_upvalue=%p, upvalue->ref=%p, rtype_hash=%lu", stack_addr,
           upvalue, upvalue->ref,
           rtype_hash);

    rtype_t *rtype = rt_find_rtype(rtype_hash);

    if (rtype->size <= 8) {
        uint64_t value = fetch_addr_value(stack_addr);
        upvalue->value.uint_value = value;
        upvalue->ref = &upvalue->value;
    } else {
        // 如果 rtype->size > 8, 则需要从堆中申请空间存放 struct, 避免空间不足。
        void *new_value = rti_gc_malloc(rtype->size, rtype);
        DEBUGF("[runtime.env_closure] size gt 8byte, malloc new_value=%p", new_value);
        memcpy(new_value, (void *) stack_addr, rtype->size);
        upvalue->ref = new_value;
    }

    DEBUGF("[runtime.env_closure] closure success, upvalue->ref=%p stack_addr=0x%lx, ", upvalue->ref, stack_addr);

    table_delete(env_upvalue_table, utoa(stack_addr));

    mutex_unlock(&env_upvalue_locker);
}

void env_assign_ref(runtime_fn_t *fn, uint64_t index, void *src_ref, uint64_t size) {
    uint64_t start = uv_hrtime();


    DEBUGF("[runtime.env_assign_ref] fn_base=%p, index=%lu, src_ref=%p, size=%lu", fn, index, src_ref, size);
    assert(index < fn->envs->length);
    assert(fn);

    void *heap_addr = fn->envs->values[index];

    DEBUGF(
            "[runtime.env_assign_ref] pre fn_base=%p, fn->envs_base=%p, index=%lu, src_ref=%p, size=%lu, env_int_value=0x%lx, heap_addr=%p",
            fn, fn->envs,
            index, src_ref, size, fetch_int_value((addr_t) heap_addr, size), heap_addr);

    // heap_addr 是比较小的空间
    assert(size <= 8);
    memmove(heap_addr, src_ref, size);

    DEBUGF(
            "[runtime.env_assign_ref] post fn_base=%p, fn->envs_base=%p, index=%lu, src_ref=%p, size=%lu, env_int_value=0x%lx, heap_addr=%p",
            fn, fn->envs,
            index, src_ref, size, fetch_int_value((addr_t) heap_addr, size), heap_addr);
}

void *env_element_value(runtime_fn_t *fn, uint64_t index) {

    DEBUGF("[runtime.env_element_value] fn_base=%p, envs=%p, envs.length=%lu, index=%lu, fn_addr=%p", fn, fn->envs,
           fn->envs->length, index, (void *) fn->fn_addr);
    assert(fn);
    assertf(index < fn->envs->length, "index out of range, fn=%p, envs=%p", fn, fn->envs);

    DEBUGF("[runtime.env_element_.value] fn_base=%p, envs=%p, value=%p",
           fn, fn->envs, fn->envs->values[index]);

    return fn->envs->values[index];
}

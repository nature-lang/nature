#include "lower.h"
#include "amd64.h"
#include <assert.h>

/**
 * TODO 直接搞一个 cross 注册中心来选择 fn 每次遇到多架构的时候，直接调用 cross 注册中心的函数
 * asm_operations asm_operations 目前属于一个更加抽象的层次，不利于寄存器分配，所以进行更加本土化的处理
 * 1. 部分指令需要 fixed register, 比如 return,div,shl,shr 等
 * @param c
 */
void lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        if (BUILD_ARCH == ARCH_AMD64) {
            amd64_lower_block(c, block);
        } else {
            assert(false && "not support arch");
        }

        lir_set_quick_op(block);
    }
}

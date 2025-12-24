#ifndef NATURE_ARM64_FMOV_IMM_TABLE_H
#define NATURE_ARM64_FMOV_IMM_TABLE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * ARM64 FMOV 立即数编码辅助函数
 * 
 * FMOV 指令使用 8 位立即数 (imm8) 来表示特定的浮点常量
 */
static inline uint8_t arm64_fmov_double_to_imm8(double value) {
    // ARM64 FMOV imm8 格式基于 VFPExpandImm 伪代码:
    // VFPExpandImm: exp = NOT(imm8<6>) : Replicate(imm8<6>, 8) : imm8<5:4>
    //              frac = imm8<3:0> : Zeros(48)
    //
    // 反向编码：从 IEEE 754 双精度提取 imm8

    union {
        double d;
        uint64_t u;
    } conv = {.d = value};

    uint64_t bits = conv.u;

    // 提取 IEEE 754 双精度浮点数的各个部分
    uint64_t sign = (bits >> 63) & 1;
    uint64_t exp = (bits >> 52) & 0x7FF; // 11位指数
    uint64_t frac = bits & 0xFFFFFFFFFFFFFULL; // 52位尾数

    // 特殊情况：0, NaN, Inf
    if (exp == 0 || exp == 0x7FF) {
        return 0xFF;
    }

    // 检查尾数低48位是否为0（只保留高4位）
    if ((frac & 0xFFFFFFFFFFFFULL) != 0) {
        return 0xFF;
    }

    // 根据 VFPExpandImm，指数格式为：
    // exp[10] = NOT(imm8<6>)
    // exp[9:2] = Replicate(imm8<6>, 8)
    // exp[1:0] = imm8<5:4>

    uint8_t exp_bit10 = (exp >> 10) & 1;
    uint8_t exp_bits9_2 = (exp >> 2) & 0xFF;
    uint8_t exp_bits1_0 = exp & 0x3;

    // 检查 exp[9:2] 是否全为相同的值
    if (exp_bits9_2 != 0x00 && exp_bits9_2 != 0xFF) {
        return 0xFF; // 不符合 Replicate 模式
    }

    // imm8<6> 应该是被复制的位
    uint8_t imm8_bit6 = (exp_bits9_2 == 0xFF) ? 1 : 0;

    // 验证 exp[10] = NOT(imm8<6>)
    if (exp_bit10 != (imm8_bit6 ^ 1)) {
        return 0xFF;
    }

    // 提取尾数的高4位
    uint8_t imm8_bits3_0 = (frac >> 48) & 0xF;

    // 组合 imm8
    uint8_t imm8 = ((uint8_t) sign << 7) | (imm8_bit6 << 6) |
                   (exp_bits1_0 << 4) | imm8_bits3_0;

    return imm8;
}

#endif // NATURE_ARM64_FMOV_IMM_TABLE_H

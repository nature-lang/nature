#include "src/binary/encoding/arm64/asm.h"
#include "src/binary/encoding/arm64/opcode.h"
#include "src/build/config.h"
#include "src/register/arch/arm64.h"
#include "src/register/register.h"
#include <stdio.h>

int setup() {
    BUILD_ARCH = ARCH_ARM64;
    reg_init();
}

/**
 * 输出格式 A0 B0 C0 D0, 比如 SUB x8, x10, #0x10 dump 48 41 00 D1
 */
static void test_dump(uint32_t encoding) {
    // 将32位编码分解为4个字节
    uint8_t byte3 = (encoding >> 24) & 0xFF;
    uint8_t byte2 = (encoding >> 16) & 0xFF;
    uint8_t byte1 = (encoding >> 8) & 0xFF;
    uint8_t byte0 = encoding & 0xFF;

    // 按照指定格式输出
    printf("%02X %02X %02X %02X\n", byte0, byte1, byte2, byte3);
}

#define TEST_EQ(encoding, ...) ({ \
    uint8_t actual[4] = { \
        (encoding) & 0xFF, \
        ((encoding) >> 8) & 0xFF, \
        ((encoding) >> 16) & 0xFF, \
        ((encoding) >> 24) & 0xFF \
    }; \
    uint8_t expected[] = { __VA_ARGS__ }; \
    for (int i = 0; i < 4; i++) { \
        if (actual[i] != expected[i]) { \
            assert(false);                      \
        } \
    } \
})

static void test_basic() {
    uint8_t count = 0;
    arm64_asm_inst_t *inst = ARM64_ASM(R_SUB, ARM64_REG(x8), ARM64_REG(x10), ARM64_IMM(0x10));
    uint32_t binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x48, 0x41, 0x00, 0xD1);

    // SUB sp, sp, #32
    inst = ARM64_ASM(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(32));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0xFF, 0x83, 0x00, 0xD1);


    // ADRP x8, :got:foo
    // 注意：ADRP的编码依赖于具体的地址，这里使用占位符
    inst = ARM64_ASM(R_ADRP, ARM64_REG(x8), ARM64_SYM("hello", true, 0, 0)); // 使用0作为占位符
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x08, 0x00, 0x00, 0x90);

    // LDR x8, [x8, :got_lo12:foo]
    inst = ARM64_ASM(R_LDR, ARM64_REG(x8), ARM64_INDIRECT(x8, 0, 0)); // 假设0表示无偏移
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x08, 0x01, 0x40, 0xF9);

    //  bl test 类似于 amd64 中的 call test
    inst = ARM64_ASM(R_BL, ARM64_SYM("test", true, 0, 0));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x00, 0x00, 0x00, 0x94);

    // bl offset
    inst = ARM64_ASM(R_BL, ARM64_SYM("test", true, 0x20, 0));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x08, 0x00, 0x00, 0x94);

    inst = ARM64_ASM(R_BL, ARM64_IMM(0x10));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x04, 0x00, 0x00, 0x94);

    // beq 测试
    inst = ARM64_ASM(R_BEQ, ARM64_SYM("symbol", true, 0, 0));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x00, 0x00, 0x00, 0x54);

    inst = ARM64_ASM(R_BEQ, ARM64_IMM(0x10));
    binary = arm64_asm_inst_encoding(inst, &count);
    TEST_EQ(binary, 0x80, 0x00, 0x00, 0x54);
}


int main(void) {
    setup();
    test_basic();
}
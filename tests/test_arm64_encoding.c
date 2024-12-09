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

#define TEST_EQ(inst, ...) ({ \
    uint8_t count = 0; \
    uint32_t encoding = arm64_asm_inst_encoding(inst, &count); \
    uint8_t actual[4] = { \
        encoding & 0xFF, \
        (encoding >> 8) & 0xFF, \
        (encoding >> 16) & 0xFF, \
        (encoding >> 24) & 0xFF \
    }; \
    uint8_t expected[] = { __VA_ARGS__ }; \
    for (int i = 0; i < 4; i++) { \
        assertf(actual[i] == expected[i],"Encoding mismatch\nExpected: %02X %02X %02X %02X\nActual:   %02X %02X %02X %02X\n", \
            expected[0], expected[1], expected[2], expected[3], \
            actual[0], actual[1], actual[2], actual[3]); \
    } \
})

static void test_basic() {
    arm64_asm_inst_t *inst = ARM64_ASM(R_SUB, ARM64_REG(x8), ARM64_REG(x10), ARM64_IMM(0x10));
    TEST_EQ(inst, 0x48, 0x41, 0x00, 0xD1);

    // SUB sp, sp, #32
    inst = ARM64_ASM(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(32));
    TEST_EQ(inst, 0xFF, 0x83, 0x00, 0xD1);

    // ADRP x8, :got:foo
    // 注意：ADRP的编码依赖于具体的地址，这里使用占位符
    inst = ARM64_ASM(R_ADRP, ARM64_REG(x8), ARM64_SYM("hello", true, 0, 0)); // 使用0作为占位符
    TEST_EQ(inst, 0x08, 0x00, 0x00, 0x90);

    // LDR x8, [x8, :got_lo12:foo]
    inst = ARM64_ASM(R_LDR, ARM64_REG(x8), ARM64_INDIRECT(x8, 0, 0, 8)); // 假设0表示无偏移
    TEST_EQ(inst, 0x08, 0x01, 0x40, 0xF9);

    //  bl test 类似于 amd64 中的 call test
    inst = ARM64_ASM(R_BL, ARM64_SYM("test", true, 0, 0));
    TEST_EQ(inst, 0x00, 0x00, 0x00, 0x94);

    // bl offset
    inst = ARM64_ASM(R_BL, ARM64_SYM("test", true, 0x20, 0));
    TEST_EQ(inst, 0x08, 0x00, 0x00, 0x94);

    inst = ARM64_ASM(R_BL, ARM64_IMM(0x10));
    TEST_EQ(inst, 0x04, 0x00, 0x00, 0x94);

    // beq 测试
    inst = ARM64_ASM(R_BEQ, ARM64_SYM("symbol", true, 0, 0));
    TEST_EQ(inst, 0x00, 0x00, 0x00, 0x54);

    inst = ARM64_ASM(R_BEQ, ARM64_IMM(0x10));
    TEST_EQ(inst, 0x80, 0x00, 0x00, 0x54);

    // strb 和 str 比较测试
    inst = ARM64_ASM(R_STR, ARM64_REG(w7), ARM64_INDIRECT(x29, -1, 0, 8)); // 假设0表示无偏移
    TEST_EQ(inst, 0xA7, 0xF3, 0x1F, 0xB8);

    inst = ARM64_ASM(R_STRB, ARM64_REG(w7), ARM64_INDIRECT(x29, -1, 0, 1)); // 假设0表示无偏移
    TEST_EQ(inst, 0xA7, 0xF3, 0x1F, 0x38);

    // strb w2, [x1, #8]
    inst = ARM64_ASM(R_STRB, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x20, 0x00, 0x39);

    // strh w2, [x1, #8]
    inst = ARM64_ASM(R_STRH, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x10, 0x00, 0x79);

    // str w2, [x1, #8]
    inst = ARM64_ASM(R_STR, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x08, 0x00, 0xB9);

    // str x2, [x1, #8]
    inst = ARM64_ASM(R_STR, ARM64_REG(x2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x04, 0x00, 0xf9);

    // ldrb w2, [x1, #8]
    inst = ARM64_ASM(R_LDRB, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x20, 0x40, 0x39);

    // ldrh w2, [x1, #8]
    inst = ARM64_ASM(R_LDRH, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x10, 0x40, 0x79);

    // ldr w2, [x1, #8]
    inst = ARM64_ASM(R_LDR, ARM64_REG(w2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x08, 0x40, 0xB9);

    // ldr x2, [x1, #8]
    inst = ARM64_ASM(R_LDR, ARM64_REG(x2), ARM64_INDIRECT(x1, 8, 0, 1));
    TEST_EQ(inst, 0x22, 0x04, 0x40, 0xf9);

    // str d0, [x0]  - 存储64位浮点数，无偏移
    inst = ARM64_ASM(R_STR, ARM64_REG(d0), ARM64_INDIRECT(x0, 0, 0, 8));
    TEST_EQ(inst, 0x00, 0x00, 0x00, 0xFD);

    // str s1, [x0, #8]  - 存储32位浮点数，偏移8字节
    inst = ARM64_ASM(R_STR, ARM64_REG(s1), ARM64_INDIRECT(x0, 8, 0, 4));
    TEST_EQ(inst, 0x01, 0x08, 0x00, 0xBD);

    // str d0, [x0, #16]  - 存储64位浮点数，偏移16字节
    inst = ARM64_ASM(R_STR, ARM64_REG(d0), ARM64_INDIRECT(x0, 16, 0, 8));
    TEST_EQ(inst, 0x00, 0x08, 0x00, 0xFD);

    // 测试数字 mov
    inst = ARM64_ASM(R_MOV, ARM64_REG(w0), ARM64_IMM(-1238));
    TEST_EQ(inst, 0xA0, 0x9A, 0x80, 0x12);

    inst = ARM64_ASM(R_MOV, ARM64_REG(w0), ARM64_IMM(-1238));
    TEST_EQ(inst, 0xA0, 0x9A, 0x80, 0x12);

    // 测试 eor
    inst = ARM64_ASM(R_EOR, ARM64_REG(w0), ARM64_REG(w1), ARM64_REG(w2));
    TEST_EQ(inst, 0x20, 0x00, 0x02, 0x4A);

    // 测试 mvn
    inst = ARM64_ASM(R_MVN, ARM64_REG(x0), ARM64_REG(x1)); // E00321AA
    TEST_EQ(inst, 0xE0, 0x03, 0x21, 0xAA);

    // 测试 fmov d1, xzr
    inst = ARM64_ASM(R_FMOV, ARM64_REG(d1), ARM64_REG(xzr)); // E103679E
    TEST_EQ(inst, 0xE1, 0x03, 0x67, 0x9E);

    // 测试 fmov s1, wzr
    inst = ARM64_ASM(R_FMOV, ARM64_REG(s1), ARM64_REG(wzr)); // 0110251E
    TEST_EQ(inst, 0xE1, 0x03, 0x27, 0x1E);
}


int main(void) {
    setup();
    test_basic();
}

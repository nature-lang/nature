#include "src/binary/encoding/amd64/asm.h"
#include "src/binary/encoding/amd64/opcode.h"
#include "src/build/config.h"
#include "src/register/arch/amd64.h"
#include "src/register/register.h"
#include <stdio.h>

int setup() {
    BUILD_ARCH = ARCH_AMD64;
    reg_init();
    amd64_opcode_init();
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

#define TEST_EQ(inst, ...) ({                                               \
    uint8_t count = 0;                                                      \
    uint8_t data[128] = {0};                                                \
    amd64_asm_inst_encoding(inst, data, &count, NULL);                      \
    uint8_t expected[] = {__VA_ARGS__};                                     \
    size_t expected_size = sizeof(expected);                                \
                                                                            \
    int mismatch = (count != expected_size);                                \
    for (int i = 0; i < count && i < expected_size && !mismatch; i++) {     \
        if (data[i] != expected[i]) {                                       \
            mismatch = 1;                                                   \
        }                                                                   \
    }                                                                       \
                                                                            \
    if (mismatch) {                                                         \
        printf("Encoding mismatch\nExpected (%zu bytes): ", expected_size); \
        for (int i = 0; i < expected_size; i++) {                           \
            printf("%02X ", expected[i]);                                   \
        }                                                                   \
        printf("\nActual (%u bytes):   ", count);                           \
        for (int i = 0; i < count; i++) {                                   \
            printf("%02X ", data[i]);                                       \
        }                                                                   \
        printf("\n");                                                       \
        assert(0);                                                          \
    }                                                                       \
})

// ... existing code ...

static void test_basic() {
    lir_op_t *op = NEW(lir_op_t);

    // mov rax, QWORD PTR fs:tls_safepoint@tpoff
    amd64_asm_inst_t *inst = AMD64_INST("mov", AMD64_REG(rax), AMD64_SEG_OFFSET("fs", 0));
    TEST_EQ(*inst, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00);

    inst = AMD64_INST("mov", AMD64_REG(rax), AMD64_SEG_OFFSET("gs", 0));
    TEST_EQ(*inst, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00);

    inst = AMD64_INST("mov", AMD64_REG(rax), AMD64_SEG_OFFSET("gs", 0x25));
    TEST_EQ(*inst, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x25, 0x00, 0x00, 0x00);

    inst = AMD64_INST("mov", INDIRECT_REG(r13, DWORD), AMD64_UINT32(0));
    TEST_EQ(*inst, 0x41, 0xC7, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00);

    inst = AMD64_INST("mov", AMD64_REG(rcx), DISP_REG(rax, 128, QWORD));
    TEST_EQ(*inst, 0x48, 0x8B, 0x88, 0x80, 0x00, 0x00, 0x00);

    inst = AMD64_INST("mov", AMD64_REG(r11), DISP_REG(r10, 128, QWORD));
    TEST_EQ(*inst, 0x4D, 0x8B, 0x9A, 0x80, 0x00, 0x00, 0x00);

    inst = AMD64_INST("div", AMD64_REG(xmm1s32), AMD64_REG(xmm2s32)); // div ss
    TEST_EQ(*inst, 0xF3, 0x0F, 0x5E, 0xCA);

    inst = AMD64_INST("sar", AMD64_REG(rdi), AMD64_REG(cl));
    TEST_EQ(*inst, 0x48, 0xd3, 0xFF);

    inst = AMD64_INST("shr", AMD64_REG(rdi), AMD64_REG(cl));
    TEST_EQ(*inst, 0x48, 0xd3, 0xEF);

    inst = AMD64_INST("sal", AMD64_REG(rdi), AMD64_REG(cl));
    TEST_EQ(*inst, 0x48, 0xd3, 0xE7);
}


int main(void) {
    setup();
    test_basic();
}

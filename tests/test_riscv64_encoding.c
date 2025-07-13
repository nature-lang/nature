#include "src/binary/encoding/riscv64//asm.h"
#include "src/binary/encoding/riscv64//opcode.h"
#include "src/build/config.h"
#include "src/register/arch/riscv64.h"
#include "src/register/register.h"
#include <stdio.h>

int setup() {
    BUILD_ARCH = ARCH_RISCV64;
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

#define TEST_EQ(inst, ...) ({                                                        \
    uint8_t *data = riscv64_asm_inst_encoding(inst, NULL);                           \
    uint8_t expected[] = {__VA_ARGS__};                                              \
    size_t expected_size = sizeof(expected);                                         \
                                                                                     \
    int mismatch = (inst->opcode_count != expected_size);                            \
    for (int i = 0; i < inst->opcode_count && i < expected_size && !mismatch; i++) { \
        if (data[i] != expected[i]) {                                                \
            mismatch = 1;                                                            \
        }                                                                            \
    }                                                                                \
                                                                                     \
    if (mismatch) {                                                                  \
        fprintf(stderr, "Encoding mismatch\nExpected (%zu bytes): ", expected_size); \
        for (int i = 0; i < expected_size; i++) {                                    \
            fprintf(stderr, "%02X ", expected[i]);                                   \
        }                                                                            \
        fprintf(stderr, "\nActual (%u bytes):   ", inst->opcode_count);              \
        for (int i = 0; i < inst->opcode_count; i++) {                               \
            fprintf(stderr, "%02X ", data[i]);                                       \
        }                                                                            \
        fprintf(stderr, "\n");                                                       \
        assert(0);                                                                   \
    }                                                                                \
})

// ... existing code ...

static void test_basic() {
    lir_op_t *op = NEW(lir_op_t);
    riscv64_asm_inst_t *inst = {0};

    // rs, rs1, imm
    inst = RISCV64_INST(RV_ADDI, RO_REG(T0), RO_REG(T0), RO_IMM(1));
    TEST_EQ(inst, 0x85, 0x02);

    inst = RISCV64_INST(RV_ADDI, RO_REG(T2), RO_REG(T0), RO_IMM(1));
    TEST_EQ(inst, 0x93, 0x83, 0x12, 0x00);

    inst = RISCV64_INST(RV_ADD, RO_REG(T0), RO_REG(T1), RO_REG(T2));
    TEST_EQ(inst, 0xB3, 0x02, 0x73, 0x00);

    inst = RISCV64_INST(RV_SUB, RO_REG(T0), RO_REG(T1), RO_REG(T2));
    TEST_EQ(inst, 0xB3, 0x02, 0x73, 0x40);

    inst = RISCV64_INST(RV_ADDIW, RO_REG(T0), RO_REG(T1), RO_IMM(10));
    TEST_EQ(inst, 0x9B, 0x02, 0xA3, 0x00);

    inst = RISCV64_INST(RV_LD, RO_REG(T1), RO_INDIRECT(R_SP, 12, QWORD));
    TEST_EQ(inst, 0x03, 0x33, 0xC1, 0x00);

    inst = RISCV64_INST(RV_SD, RO_REG(T2), RO_INDIRECT(T0, 304, QWORD));
    TEST_EQ(inst, 0x23, 0xB8, 0x72, 0x12);

    inst = RISCV64_INST(RV_BEQ, RO_REG(A0), RO_REG(A1), RO_IMM(0xc));
    TEST_EQ(inst, 0x63, 0x06, 0xB5, 0x00);

    inst = RISCV64_INST(RV_BEQ, RO_REG(T0), RO_REG(T1), RO_SYM("end", true, 0, 0));
    TEST_EQ(inst, 0x63, 0x80, 0x62, 0x00);

    inst = RISCV64_INST(RV_BNE, RO_REG(A0), RO_REG(A1), RO_IMM(0xa));
    TEST_EQ(inst, 0x63, 0x15, 0xB5, 0x00);

    inst = RISCV64_INST(RV_CALL, RO_SYM("foo", false, 0, 0));
    TEST_EQ(inst, 0x97, 0x00, 0x00, 0x00, 0xE7, 0x80, 0x00, 0x00);

    inst = RISCV64_INST(RV_LI, RO_REG(T0), RO_IMM(12));
    TEST_EQ(inst, 0xB1, 0x42);

    inst = RISCV64_INST(RV_MV, RO_REG(T1), RO_REG(T2));
    TEST_EQ(inst, 0x1E, 0x83);

    inst = RISCV64_INST(RV_FLD, RO_REG(FT0), RO_INDIRECT(T0, 0, QWORD));
    TEST_EQ(inst, 0x07, 0xB0, 0x02, 0x00);

    inst = RISCV64_INST(RV_FADD_S, RO_REG(FT0), RO_REG(FT1), RO_REG(FT2));
    TEST_EQ(inst, 0x53, 0xf0, 0x20, 0x00);

    inst = RISCV64_INST(RV_FSUB_S, RO_REG(FT0), RO_REG(FT1), RO_REG(FT2));
    TEST_EQ(inst, 0x53, 0xf0, 0x20, 0x08);

    inst = RISCV64_INST(RV_FMUL_S, RO_REG(FT0), RO_REG(FT1), RO_REG(FT2));
    TEST_EQ(inst, 0x53, 0xf0, 0x20, 0x10);

    inst = RISCV64_INST(RV_FDIV_S, RO_REG(FT0), RO_REG(FT1), RO_REG(FT2));
    TEST_EQ(inst, 0x53, 0xf0, 0x20, 0x18);

    inst = RISCV64_INST(RV_FCVT_S_W, RO_REG(FT0), RO_REG(T0));
    TEST_EQ(inst, 0x53, 0xf0, 0x02, 0xD0);

    inst = RISCV64_INST(RV_FCVT_W_S, RO_REG(T0), RO_REG(FT0)); // Assuming T0 is dest, FT0 is src
    TEST_EQ(inst, 0xd3, 0x72, 0x00, 0xC0); // Expected: C0080253 (rd=t0, rs1=ft0, rm=000)

    inst = RISCV64_INST(RV_FLW, RO_REG(FT0), RO_INDIRECT(T0, 0, DWORD));
    TEST_EQ(inst, 0x07, 0xA0, 0x02, 0x00);

    inst = RISCV64_INST(RV_FSW, RO_REG(FT0), RO_INDIRECT(T0, 0, DWORD));
    TEST_EQ(inst, 0x27, 0xA0, 0x02, 0x00);

    inst = RISCV64_INST(RV_J, RO_IMM(6));
    TEST_EQ(inst, 0x6f, 0x00, 0x60, 0x00);

    inst = RISCV64_INST(RV_J, RO_SYM("foo", false, 0, ASM_RISCV64_RELOC_JAL));
    TEST_EQ(inst, 0x6f, 0x00, 0x00, 0x00);

    inst = RISCV64_INST(RV_J, RO_IMM(-4096));
    TEST_EQ(inst, 0x6f, 0xf0, 0x0f, 0x80);
}

int main(void) {
    setup();
    test_basic();
}

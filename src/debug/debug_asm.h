#ifndef NATURE_DEBUG_ASM_H
#define NATURE_DEBUG_ASM_H

#include "src/native/amd64.h"
#include "src/native/arm64.h"
#include "src/native/riscv64.h"

void asm_op_to_string(int i, void *op);

static inline char *code_to_string(uint8_t *data, uint8_t count) {
    // 打印生成的二进制指令
    printf("{ ");
    for (int i = 0; i < count; i++) {
        printf("0x%02X", data[i]);
        if (i < count - 1) {
            printf(", ");
        }
    }
    printf(" }\n");
    fflush(stdout);
}

#endif //NATURE_ASM_H

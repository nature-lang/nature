#include "asm.h"

amd64_asm_operand_t *amd64_asm_symbol_operand(amd64_asm_inst_t asm_inst) {
    for (int i = 0; i < asm_inst.count; ++i) {
        amd64_asm_operand_t *operand = asm_inst.operands[i];
        if (operand->type == ASM_OPERAND_TYPE_SYMBOL) {
            return operand;
        }
    }
    return NULL;
}

amd64_asm_operand_t *asm_match_int_operand(int64_t n) {
    // 正负数处理
    if (n >= INT8_MIN && n <= INT8_MAX) {
        return UINT8(n);
    }

    if (n >= INT16_MIN && n <= INT16_MAX) {
        return UINT16(n);
    }

    if (n >= INT32_MIN && n <= INT32_MAX) {
        return UINT32(n);
    }

    if (n >= INT64_MIN && n <= INT64_MAX) {
        return UINT64(n);
    }

    return NULL;
}

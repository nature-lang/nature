#include "asm.h"
#include "utils/helper.h"
#include <assert.h>

static string asm_operand_to_string(asm_operand_t *operand) {
    void *value = operand->value;
    switch (operand->type) {
        case ASM_OPERAND_TYPE_UINT8: {
            return utoa(((asm_uint8_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_UINT16: {
            return utoa(((asm_uint16_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_UINT:
        case ASM_OPERAND_TYPE_UINT32: {
            return utoa(((asm_uint32_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_UINT64: {
            return utoa(((asm_uint64_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_INT8: {
            return itoa(((asm_int8_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_INT32: {
            return itoa(((asm_int32_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_REG: {
            return ((reg_t *) value)->name;
        }
        case ASM_OPERAND_TYPE_DISP_REG: {
            asm_disp_reg_t *disp = value;
            return dsprintf("[%s%d|%d]", disp->reg->name, disp->disp, operand->size);
        }
        case ASM_OPERAND_TYPE_RIP_RELATIVE: {
            return dsprintf("[rip%d|%d]", ((asm_rip_relative_t *) value)->disp, operand->size);
        }
        case ASM_OPERAND_TYPE_INDIRECT_REG: {
            return dsprintf("[%s|%d]", ((asm_indirect_reg_t *) value)->reg->name, operand->size);
        }
        case ASM_OPERAND_TYPE_SIB_REG: {
            // [base+index*scale+offset]
            asm_sib_reg_t *sib = (asm_sib_reg_t *) value;
            return dsprintf("[%s+%s*%d]", sib->base->name, sib->base->name, sib->scale);
        }
        case ASM_OPERAND_TYPE_SYMBOL: {
            return dsprintf("%s", ((asm_symbol_t *) value)->name);
        }

        case ASM_OPERAND_TYPE_FLOAT32: {
            return dsprintf("%f", ((asm_float32_t *) value)->value);
        }
        case ASM_OPERAND_TYPE_FLOAT64: {
            return dsprintf("%f", ((asm_float64_t *) value)->value);
        }
        default: {
            assert(false && "unknown operand type");
        }
    }
}

// mov a -> b
void asm_op_to_string(int i, asm_operation_t *op) {
    printf("%d \t", i);
    if (str_equal(op->name, "label")) {
        printf("%s  ", op->name);
    } else {
        printf("\t\t%s  ", op->name);
    }

    if (op->count == 1) {
        printf("%s", asm_operand_to_string(op->operands[0]));
    } else if (op->count == 2) {
        printf("%s -> %s",
               asm_operand_to_string(op->operands[1]),
               asm_operand_to_string(op->operands[0])
        );
    } else if (op->count == 0) {
    } else {
        printf("not_support");
    }

    printf("\n");
}

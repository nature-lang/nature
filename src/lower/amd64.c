#include "amd64.h"
#include "src/register/amd64.h"

static list *amd64_formal_params_lower(closure_t *c, slice_t *formal_params) {
    list *operations = list_new();
    uint8_t used[2] = {0};
    // 16byte 起点, 因为 call 和 push rbp 占用了16 byte空间, 当参数寄存器用完的时候，就会使用 stack offset 了
    int16_t stack_param_slot = 16;
    SLICE_FOR(formal_params) {
        lir_var_t *var = SLICE_VALUE(formal_params);
        lir_operand_t *source = NULL;
        reg_t *reg = amd64_fn_param_next_reg(used, var->type_base);
        if (reg) {
            source = LIR_NEW_OPERAND(LIR_OPERAND_REG, reg);
        } else {
            lir_stack_t *stack = NEW(lir_stack_t);
            stack->size = QWORD; // 使用了 push 指令进栈，所以固定 QWORD(float 也是 8字节，只是不直接使用 push)
            stack->slot = stack_param_slot;
            stack_param_slot += QWORD; // 8byte 会造成 stack_slot algin exception
            source = LIR_NEW_OPERAND(LIR_OPERAND_STACK, stack);
        }

        lir_op_t *op = lir_op_move(LIR_NEW_OPERAND(LIR_OPERAND_VAR, var), source);
        list_push(operations, op);
    }
    // 是否需要 stack slot 16byte 对齐 ? 不用，caller 会处理好

    return operations;
}

static void amd64_lower_block(closure_t *c, basic_block_t *block) {
// handle opcode
    for (list_node *node = block->operations->front; node != block->operations->rear; node = node->succ) {
        lir_op_t *op = node->value;

        // 处理 imm string operands
        slice_t *imm_operands = lir_input_operands(op, FLAG(LIR_OPERAND_IMM));
        for (int i = 0; i < imm_operands->count; ++i) {
            lir_operand_t *imm_operand = imm_operands->take[i];
            lir_imm_t *imm = imm_operand->value;
            if (imm->type == TYPE_STRING_RAW) {
                lir_operand_t *var_operand = lir_new_temp_var_operand(c, type_new_base(TYPE_STRING_RAW));
                slice_push(c->globals, var_operand->value);
                lir_op_t *temp = lir_op_new(LIR_OPCODE_LEA, imm_operand, NULL, var_operand);
                imm_operand->type = var_operand->type;
                imm_operand->value = var_operand->value;
                list_insert_before(block->operations, node, temp);
            }
        }

        // if op is fn begin, will set formal param
        // 也就是为了能够方便的计算生命周期，把 native 中做的事情提前到这里而已
        if (op->code == LIR_OPCODE_FN_PARAM) {
            // fn begin
            // mov rsi -> formal param 1
            // mov rdi -> formal param 2
            // ....
            list *temps = amd64_formal_params_lower(c, op->output->value);
            list_node *current = temps->front;
            while (current->value != NULL) {
                list_insert_before(block->operations, LIST_NODE(), current->value);
                current = current->succ;
            }

            list_remove(block->operations, node);
            continue;
        }

        /**
         * return var
         * ↓
         * mov var -> rax/xmm0
         * return rax/xmm0
         */
        if (op->code == LIR_OPCODE_RETURN && op->output != NULL) {
            // 1.1 return 指令需要将返回值放到 rax 中
            lir_operand_t *reg_operand;
            if (lir_operand_type_base(op->output) == TYPE_FLOAT) {
                reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, xmm0);
            } else {
                reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, rax);
            }

            lir_op_t *temp = lir_op_move(reg_operand, op->output);
            op->output = reg_operand;
            list_insert_before(block->operations, node, temp);
            continue;
        }

        /**
         * call test -> var
         * ↓
         * call test -> rax/xmm0
         * mov rax/xmm0 -> var
         */
        if (lir_op_is_call(op) && op->output != NULL) {
            lir_operand_t *reg_operand;
            if (lir_operand_type_base(op->output) == TYPE_FLOAT) {
                reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, xmm0);
            } else {
                reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, rax);
            }
            lir_op_t *temp = lir_op_move(op->output, reg_operand);
            op->output = reg_operand;
            list_insert_after(block->operations, node, temp);
            continue;
        }

        // div 被输数，除数 = 商
        if (op->code == LIR_OPCODE_DIV) {
            lir_operand_t *reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, rax);
            lir_op_t *before = lir_op_move(reg_operand, op->first);
            lir_op_t *after = lir_op_move(op->output, reg_operand);
            op->first = reg_operand;
            op->output = reg_operand;
            list_insert_before(block->operations, LIST_VALUE(), before);
            list_insert_after(block->operations, LIST_VALUE(), after);
            continue;
        }
    }
}

/**
 * operations operations 目前属于一个更加抽象的层次，不利于寄存器分配，所以进行更加本土化的处理
 * 1. 部分指令需要 fixed register, 比如 return,div,shl,shr 等
 * @param c
 */
void amd64_lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        amd64_lower_block(c, block);
    }
}

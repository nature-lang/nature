#include "amd64.h"
#include "src/register/amd64.h"

#define ASSIGN_VAR(_operand) \
   type_kind base = lir_operand_type_base(_operand); \
   lir_operand_t *temp = temp_var_operand(c, type_base_new(base)); \
   slice_push(c->globals, temp->value);                \
   linked_insert_before(block->operations, node, lir_op_move(temp, op->first)); \
   op->first = lir_reset_operand(temp, VR_FLAG_FIRST);

static void amd64_lower_imm_operand(closure_t *c, basic_block_t *block, linked_node *node) {
    lir_op_t *op = node->value;
    slice_t *imm_operands = lir_op_operands(op, FLAG(LIR_OPERAND_IMM), 0, false);
    for (int i = 0; i < imm_operands->count; ++i) {
        lir_operand_t *imm_operand = imm_operands->take[i];
        lir_imm_t *imm = imm_operand->value;
        if (imm->kind == TYPE_RAW_STRING) {
            lir_operand_t *var_operand = temp_var_operand(c, type_base_new(TYPE_RAW_STRING));
            slice_push(c->globals, var_operand->value);

            lir_op_t *temp = lir_op_new(LIR_OPCODE_LEA, imm_operand, NULL, var_operand);
            linked_insert_before(block->operations, node, temp);

            lir_operand_t *temp_operand = lir_reset_operand(var_operand, imm_operand->pos);
            imm_operand->assert_type = temp_operand->assert_type;
            imm_operand->value = temp_operand->value;
        }
    }
}

// mov var -> eax
static linked_t *amd64_actual_params_lower(closure_t *c, slice_t *actual_params) {
    linked_t *operations = linked_new();
    linked_t *push_operations = linked_new();
    int push_length = 0;
    uint8_t used[2] = {0};
    for (int i = 0; i < actual_params->count; ++i) {
        lir_operand_t *param_operand = actual_params->take[i];
        type_kind type_base = lir_operand_type_base(param_operand);
        reg_t *reg = amd64_fn_param_next_reg(used, type_base);
        lir_operand_t *target = NULL;
        if (reg) {
            // empty reg
            if (reg->size < QWORD) {
                linked_push(operations, lir_op_new(LIR_OPCODE_CLR, NULL, NULL,
                                                   operand_new(LIR_OPERAND_REG, covert_alloc_reg(reg))));
            }

            lir_op_t *op = lir_op_move(operand_new(LIR_OPERAND_REG, reg), param_operand);

            linked_push(operations, op);
        } else {
            // 不需要 move, 直接走 push 指令即可, 这里虽然操作了 rsp，但是 rbp 是没有变化的
            // 不过 call 之前需要保证 rsp 16 byte 对齐
            lir_op_t *push_op = lir_op_new(LIR_OPCODE_PUSH, param_operand, NULL, NULL);
            linked_push(push_operations, push_op);
            push_length += QWORD;
        }
    }
    // 由于使用了 push 指令操作了堆栈，可能导致堆栈不对齐，所以需要操作一下堆栈对齐
    uint64_t diff_length = align(push_length, 16) - push_length;

    // sub rsp - 1 = rsp
    // 先 sub 再 push, 保证 rsp 16 byte 对齐
    if (diff_length > 0) {
        lir_op_t *binary_op = lir_op_new(LIR_OPCODE_SUB,
                                         operand_new(LIR_OPERAND_REG, rsp),
                                         int_operand(diff_length),
                                         operand_new(LIR_OPERAND_REG, rsp));
        linked_push(push_operations, binary_op);
    }

    // 倒序 push operations 追加到 operations 中
    linked_node *current = linked_last(push_operations);
    while (current && current->value) {
        linked_push(operations, current->value);
        current = current->prev;
    }

    return operations;
}

/**
 * 寄存器选择进行了 fit 匹配
 * @param c
 * @param formal_params
 * @return
 */
static linked_t *amd64_formal_params_lower(closure_t *c, slice_t *formal_params) {
    linked_t *operations = linked_new();
    uint8_t used[2] = {0};
    // 16byte 起点, 因为 call 和 push rbp 占用了16 byte空间, 当参数寄存器用完的时候，就会使用 stack offset 了
    int16_t stack_param_slot = 16; // ret addr + push rsp
    SLICE_FOR(formal_params) {
        lir_var_t *var = SLICE_VALUE(formal_params);
        lir_operand_t *source = NULL;
        reg_t *reg = amd64_fn_param_next_reg(used, var->type.kind);
        if (reg) {
            source = operand_new(LIR_OPERAND_REG, reg);
        } else {
            lir_stack_t *stack = NEW(lir_stack_t);
            // caller 虽然使用了 pushq 指令进栈，但是实际上并不需要使用这么大的空间,
            stack->size = type_kind_sizeof(var->type.kind);
            stack->slot = stack_param_slot; // caller push 入栈的参数的具体位置

            // 如果是 c 的话会有 16byte,但 nature 最大也就 8byte 了
            // 固定 QWORD(caller float 也是 8 byte，只是不直接使用 push)
            stack_param_slot += QWORD; // 固定 8 bit， 不过 8byte 会造成 stack_slot align exception

            source = operand_new(LIR_OPERAND_STACK, stack);
        }

        lir_op_t *op = lir_op_move(operand_new(LIR_OPERAND_VAR, var), source);
        linked_push(operations, op);
    }

    return operations;
}

void amd64_lower_block(closure_t *c, basic_block_t *block) {
    // handle opcode
    for (linked_node *node = block->operations->front; node != block->operations->rear; node = node->succ) {
        lir_op_t *op = node->value;

        // 处理 imm string operands
        amd64_lower_imm_operand(c, block, node);

        // TODO lower indirect addr
        // TODO lower lea op

        if (lir_op_is_call(op) && op->second->value != NULL) {
            // lower call actual params
            linked_t *temps = amd64_actual_params_lower(c, op->second->value);
            linked_node *current = temps->front;
            while (current->value != NULL) {
                linked_insert_before(block->operations, LINKED_NODE(), current->value);
                current = current->succ;
            }
            op->second->value = slice_new();

            /**
             * call test -> var
             * ↓
             * call test -> rax/xmm0
             * mov rax/xmm0 -> var
             */
            if (op->output != NULL) {
                lir_operand_t *reg_operand;
                if (lir_operand_type_base(op->output) == TYPE_FLOAT) {
                    reg_operand = operand_new(LIR_OPERAND_REG, xmm0);
                } else {
                    reg_operand = operand_new(LIR_OPERAND_REG, rax);
                }
                linked_insert_after(block->operations, node, lir_op_move(op->output, reg_operand));
                op->output = lir_reset_operand(reg_operand, VR_FLAG_OUTPUT);
            }

            continue;
        }

        // if op is fn begin, will set formal param
        // 也就是为了能够方便的计算生命周期，把 native 中做的事情提前到这里而已
        if (op->code == LIR_OPCODE_FN_BEGIN) {
            // fn begin
            // mov rsi -> formal param 1
            // mov rdi -> formal param 2
            // ....
            linked_t *temps = amd64_formal_params_lower(c, op->output->value);
            linked_node *current = temps->front;
            while (current->value != NULL) {
                linked_insert_after(block->operations, LINKED_NODE(), current->value);
                current = current->succ;
            }
            op->output->value = slice_new();

            continue;
        }

        /**
         * return var
         * ↓
         * mov var -> rax/xmm0
         * return rax/xmm0
         */
        if (op->code == LIR_OPCODE_RETURN && op->first != NULL) {
            // 1.1 return 指令需要将返回值放到 rax 中
            lir_operand_t *reg_operand;
            if (lir_operand_type_base(op->first) == TYPE_FLOAT) {
                reg_operand = operand_new(LIR_OPERAND_REG, xmm0);
            } else {
                reg_operand = operand_new(LIR_OPERAND_REG, rax);
            }

            linked_insert_before(block->operations, node, lir_op_move(reg_operand, op->first));
            op->first = lir_reset_operand(reg_operand, VR_FLAG_FIRST);
            continue;
        }

        // div 被输数，除数 = 商
        if (op->code == LIR_OPCODE_DIV) {
            lir_operand_t *reg_operand = operand_new(LIR_OPERAND_REG, rax);
            lir_op_t *before = lir_op_move(reg_operand, op->first);
            lir_op_t *after = lir_op_move(op->output, reg_operand);

            linked_insert_before(block->operations, node, before);
            linked_insert_after(block->operations, node, after);

            op->first = lir_reset_operand(reg_operand, VR_FLAG_FIRST);
            op->output = lir_reset_operand(reg_operand, VR_FLAG_OUTPUT);
            continue;
        }

        // amd64 的 add imm -> rax 相当于 add imm,rax -> rax
        // cmp imm -> rax 同理。所以 output 部分不能是 imm 操作数, 且必须要有一个寄存器参与计算
        // 对于 lir 指令，例如 bea first,second => label 进行比较时，将 first 放到 asm 的 result 部分
        // second 放到 asm 的 input 部分。 所以 result 不能为 imm, 也就是 beq 指令的 first 部分不能为 imm
        // tips: 不能随便调换 first 和 second 的顺序，会导致 asm cmp 指令对比异常
        if (lir_op_contain_cmp(op)) {
            // first is native target, cannot imm, so in case swap first and second
            if (op->first->assert_type != LIR_OPERAND_VAR) {
                ASSIGN_VAR(op->first);
                continue;
            }
        }

        if (lir_op_is_arithmetic(op)) {
            // first must var for assign reg
            if (op->first->assert_type != LIR_OPERAND_VAR) {
                ASSIGN_VAR(op->first);
                continue;
            }
        }

        if (op->code == LIR_OPCODE_MOVE) {
            if (op->output->assert_type != LIR_OPERAND_VAR) {
                ASSIGN_VAR(op->output);
                continue;
            }
        }

    }

    // realloc first op
}



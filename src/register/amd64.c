#include "amd64.h"
#include "src/type.h"
#include "utils/slice.h"

list *amd64_formal_params_lower(closure_t *c) {
    list *operations = list_new();
    uint8_t used[2] = {0};
    // 16byte 起点, 因为 call 和 push rbp 占用了16 byte空间
    int16_t stack_param_offset = 16;
    LIST_FOR(c->formal_params) {
        lir_var_decl *var_decl = LIST_VALUE();
        lir_operand *source = NULL;
        lir_operand_var *var = lir_new_var_operand(c, var_decl->ident);
        reg_t *reg = amd64_fn_param_next_reg(used, var->type_base);
        if (reg) {
            source = LIR_NEW_OPERAND(LIR_OPERAND_REG, reg);
        } else {
            lir_operand_stack *stack = NEW(lir_operand_stack);
            stack->size = QWORD; // 使用了 push 指令进栈，所以固定 QWORD(float 也是 8字节，只是不直接使用 push)
            stack->slot = stack_param_offset;
            stack_param_offset += QWORD;
            source = LIR_NEW_OPERAND(LIR_OPERAND_STACK, stack);
        }

        lir_op_t *op = lir_op_move(LIR_NEW_OPERAND(LIR_OPERAND_VAR, var), source);
        list_push(operations, op);
    }
    return operations;
}


void amd64_reg_init() {
    rax = reg_new("rax", 0, REG_TYPE_INT, QWORD, 0);
    rcx = reg_new("rcx", 1, REG_TYPE_INT, QWORD, 1);
    rdx = reg_new("rdx", 2, REG_TYPE_INT, QWORD, 2);
    rbx = reg_new("rbx", 3, REG_TYPE_INT, QWORD, 3);
    rsp = reg_new("rsp", 4, REG_TYPE_INT, QWORD, -1);
    rbp = reg_new("rbp", 5, REG_TYPE_INT, QWORD, -1);
    rsi = reg_new("rsi", 6, REG_TYPE_INT, QWORD, 6);
    rdi = reg_new("rdi", 7, REG_TYPE_INT, QWORD, 7);
    r8 = reg_new("r8", 8, REG_TYPE_INT, QWORD, 8);
    r9 = reg_new("r9", 9, REG_TYPE_INT, QWORD, 9);
    r10 = reg_new("r10", 10, REG_TYPE_INT, QWORD, 10);
    r11 = reg_new("r11", 11, REG_TYPE_INT, QWORD, 11);
    r12 = reg_new("r12", 12, REG_TYPE_INT, QWORD, 12);
    r13 = reg_new("r13", 13, REG_TYPE_INT, QWORD, 13);
    r14 = reg_new("r14", 14, REG_TYPE_INT, QWORD, 14);
    r15 = reg_new("r15", 15, REG_TYPE_INT, QWORD, 15);

    eax = reg_new("eax", 0, REG_TYPE_INT, DWORD, -1);
    ecx = reg_new("ecx", 1, REG_TYPE_INT, DWORD, -1);
    edx = reg_new("edx", 2, REG_TYPE_INT, DWORD, -1);
    ebx = reg_new("ebx", 3, REG_TYPE_INT, DWORD, -1);
    esp = reg_new("esp", 4, REG_TYPE_INT, DWORD, -1);
    ebp = reg_new("ebp", 5, REG_TYPE_INT, DWORD, -1);
    esi = reg_new("esi", 6, REG_TYPE_INT, DWORD, -1);
    edi = reg_new("edi", 7, REG_TYPE_INT, DWORD, -1);
    r8d = reg_new("r8d", 8, REG_TYPE_INT, DWORD, -1);
    r9d = reg_new("r9d", 9, REG_TYPE_INT, DWORD, -1);
    r10d = reg_new("r10d", 10, REG_TYPE_INT, DWORD, -1);
    r11d = reg_new("r11d", 11, REG_TYPE_INT, DWORD, -1);
    r12d = reg_new("r12d", 12, REG_TYPE_INT, DWORD, -1);
    r13d = reg_new("r13d", 13, REG_TYPE_INT, DWORD, -1);
    r14d = reg_new("r14d", 14, REG_TYPE_INT, DWORD, -1);
    r15d = reg_new("r15d", 15, REG_TYPE_INT, DWORD, -1);

    ax = reg_new("ax", 0, REG_TYPE_INT, WORD, -1);
    cx = reg_new("cx", 1, REG_TYPE_INT, WORD, -1);
    dx = reg_new("dx", 2, REG_TYPE_INT, WORD, -1);
    bx = reg_new("bx", 3, REG_TYPE_INT, WORD, -1);
    sp = reg_new("sp", 4, REG_TYPE_INT, WORD, -1);
    bp = reg_new("bp", 5, REG_TYPE_INT, WORD, -1);
    si = reg_new("si", 6, REG_TYPE_INT, WORD, -1);
    di = reg_new("di", 7, REG_TYPE_INT, WORD, -1);
    r8w = reg_new("r8w", 8, REG_TYPE_INT, WORD, -1);
    r9w = reg_new("r9w", 9, REG_TYPE_INT, WORD, -1);
    r10w = reg_new("r10w", 10, REG_TYPE_INT, WORD, -1);
    r11w = reg_new("r11w", 11, REG_TYPE_INT, WORD, -1);
    r12w = reg_new("r12w", 12, REG_TYPE_INT, WORD, -1);
    r13w = reg_new("r13w", 13, REG_TYPE_INT, WORD, -1);
    r14w = reg_new("r14w", 14, REG_TYPE_INT, WORD, -1);
    r15w = reg_new("r15w", 15, REG_TYPE_INT, WORD, -1);

    // 正常来说也应该是 0, 1, 2, 3, 但是 intel 手册规定了使用 4，5，6, 7
    ah = reg_new("ah", 4, REG_TYPE_INT, BYTE, -1);
    ch = reg_new("ch", 5, REG_TYPE_INT, BYTE, -1);
    dh = reg_new("dh", 6, REG_TYPE_INT, BYTE, -1);
    bh = reg_new("bh", 7, REG_TYPE_INT, BYTE, -1);

    al = reg_new("al", 0, REG_TYPE_INT, BYTE, -1);
    cl = reg_new("cl", 1, REG_TYPE_INT, BYTE, -1);
    dl = reg_new("dl", 2, REG_TYPE_INT, BYTE, -1);
    bl = reg_new("bl", 3, REG_TYPE_INT, BYTE, -1);
    spl = reg_new("spl", 4, REG_TYPE_INT, BYTE, -1);
    bpl = reg_new("bpl", 5, REG_TYPE_INT, BYTE, -1);
    sil = reg_new("sil", 6, REG_TYPE_INT, BYTE, -1);
    dil = reg_new("dil", 7, REG_TYPE_INT, BYTE, -1);
    r8b = reg_new("r8b", 8, REG_TYPE_INT, BYTE, -1);
    r9b = reg_new("r9b", 9, REG_TYPE_INT, BYTE, -1);
    r10b = reg_new("r10b", 10, REG_TYPE_INT, BYTE, -1);
    r11b = reg_new("r11b", 11, REG_TYPE_INT, BYTE, -1);
    r12b = reg_new("r12b", 12, REG_TYPE_INT, BYTE, -1);
    r13b = reg_new("r13b", 13, REG_TYPE_INT, BYTE, -1);
    r14b = reg_new("r14b", 14, REG_TYPE_INT, BYTE, -1);
    r15b = reg_new("r15b", 15, REG_TYPE_INT, BYTE, -1);

    xmm0 = reg_new("xmm0", 0, REG_TYPE_FLOAT, OWORD, 16);
    xmm1 = reg_new("xmm1", 1, REG_TYPE_FLOAT, OWORD, 17);
    xmm2 = reg_new("xmm2", 2, REG_TYPE_FLOAT, OWORD, 18);
    xmm3 = reg_new("xmm3", 3, REG_TYPE_FLOAT, OWORD, 19);
    xmm4 = reg_new("xmm4", 4, REG_TYPE_FLOAT, OWORD, 20);
    xmm5 = reg_new("xmm5", 5, REG_TYPE_FLOAT, OWORD, 21);
    xmm6 = reg_new("xmm6", 6, REG_TYPE_FLOAT, OWORD, 22);
    xmm7 = reg_new("xmm7", 7, REG_TYPE_FLOAT, OWORD, 23);
    xmm8 = reg_new("xmm8", 8, REG_TYPE_FLOAT, OWORD, 24);
    xmm9 = reg_new("xmm9", 9, REG_TYPE_FLOAT, OWORD, 25);
    xmm10 = reg_new("xmm10", 10, REG_TYPE_FLOAT, OWORD, 26);
    xmm11 = reg_new("xmm11", 11, REG_TYPE_FLOAT, OWORD, 27);
    xmm12 = reg_new("xmm12", 12, REG_TYPE_FLOAT, OWORD, 28);
    xmm13 = reg_new("xmm13", 13, REG_TYPE_FLOAT, OWORD, 29);
    xmm14 = reg_new("xmm14", 14, REG_TYPE_FLOAT, OWORD, 30);
    xmm15 = reg_new("xmm15", 15, REG_TYPE_FLOAT, OWORD, 31);

    ymm0 = reg_new("ymm0", 0, REG_TYPE_FLOAT, YWORD, -1);
    ymm1 = reg_new("ymm1", 1, REG_TYPE_FLOAT, YWORD, -1);
    ymm2 = reg_new("ymm2", 2, REG_TYPE_FLOAT, YWORD, -1);
    ymm3 = reg_new("ymm3", 3, REG_TYPE_FLOAT, YWORD, -1);
    ymm4 = reg_new("ymm4", 4, REG_TYPE_FLOAT, YWORD, -1);
    ymm5 = reg_new("ymm5", 5, REG_TYPE_FLOAT, YWORD, -1);
    ymm6 = reg_new("ymm6", 6, REG_TYPE_FLOAT, YWORD, -1);
    ymm7 = reg_new("ymm7", 7, REG_TYPE_FLOAT, YWORD, -1);
    ymm8 = reg_new("ymm8", 8, REG_TYPE_FLOAT, YWORD, -1);
    ymm9 = reg_new("ymm9", 9, REG_TYPE_FLOAT, YWORD, -1);
    ymm10 = reg_new("ymm10", 10, REG_TYPE_FLOAT, YWORD, -1);
    ymm11 = reg_new("ymm11", 11, REG_TYPE_FLOAT, YWORD, -1);
    ymm12 = reg_new("ymm12", 12, REG_TYPE_FLOAT, YWORD, -1);
    ymm13 = reg_new("ymm13", 13, REG_TYPE_FLOAT, YWORD, -1);
    ymm14 = reg_new("ymm14", 14, REG_TYPE_FLOAT, YWORD, -1);
    ymm15 = reg_new("ymm15", 15, REG_TYPE_FLOAT, YWORD, -1);

    zmm0 = reg_new("zmm0", 0, REG_TYPE_FLOAT, ZWORD, -1);
    zmm1 = reg_new("zmm1", 1, REG_TYPE_FLOAT, ZWORD, -1);
    zmm2 = reg_new("zmm2", 2, REG_TYPE_FLOAT, ZWORD, -1);
    zmm3 = reg_new("zmm3", 3, REG_TYPE_FLOAT, ZWORD, -1);
    zmm4 = reg_new("zmm4", 4, REG_TYPE_FLOAT, ZWORD, -1);
    zmm5 = reg_new("zmm5", 5, REG_TYPE_FLOAT, ZWORD, -1);
    zmm6 = reg_new("zmm6", 6, REG_TYPE_FLOAT, ZWORD, -1);
    zmm7 = reg_new("zmm7", 7, REG_TYPE_FLOAT, ZWORD, -1);
    zmm8 = reg_new("zmm8", 8, REG_TYPE_FLOAT, ZWORD, -1);
    zmm9 = reg_new("zmm9", 9, REG_TYPE_FLOAT, ZWORD, -1);
    zmm10 = reg_new("zmm10", 10, REG_TYPE_FLOAT, ZWORD, -1);
    zmm11 = reg_new("zmm11", 11, REG_TYPE_FLOAT, ZWORD, -1);
    zmm12 = reg_new("zmm12", 12, REG_TYPE_FLOAT, ZWORD, -1);
    zmm13 = reg_new("zmm13", 13, REG_TYPE_FLOAT, ZWORD, -1);
    zmm14 = reg_new("zmm14", 14, REG_TYPE_FLOAT, ZWORD, -1);
    zmm15 = reg_new("zmm15", 15, REG_TYPE_FLOAT, ZWORD, -1);
}

/**
 * operations operations 目前属于一个更加抽象的层次，不利于寄存器分配，所以进行更加本土化的处理
 * 1. 部分指令需要 fixed register, 比如 return,div,shl,shr 等
 * @param c
 */
void amd64_operations_lower(closure_t *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        LIST_FOR(block->operations) {
            lir_op_t *op = LIST_VALUE();

            // if op is fn begin, will set formal param
            // 也就是为了能够方便的计算生命周期，把 native 中做的事情提前到这里而已
            if (op->code == LIR_OPCODE_FN_BEGIN) {
                // fn begin
                // mov rsi -> formal param 1
                // mov rdi -> formal param 2
                // ....
                list *temps = amd64_formal_params_lower(c);
                list_node *current = temps->front;
                while (current->value != NULL) {
                    list_insert_after(block->operations, LIST_NODE(), current->value);
                    current = current->succ;
                }
            }

            if (op->code == LIR_OPCODE_RETURN && op->output != NULL) {
                // 1.1 return 指令需要将返回值放到 rax 中
                lir_operand *reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, rax);
                lir_op_t *before = lir_op_move(reg_operand, op->output);
                op->output = reg_operand;
                list_insert_before(block->operations, LIST_VALUE(), before);
            }

            // div 被输数，除数 = 商
            if (op->code == LIR_OPCODE_DIV) {
                lir_operand *reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_REG, rax);
                lir_op_t *before = lir_op_move(reg_operand, op->first);
                lir_op_t *after = lir_op_move(op->output, reg_operand);
                op->first = reg_operand;
                op->output = reg_operand;
                list_insert_before(block->operations, LIST_VALUE(), before);
                list_insert_after(block->operations, LIST_VALUE(), after);
            }
        }
    }
}

/**
 * amd64 下统一使用 8byte 寄存器或者 16byte xmm 寄存器
 * 返回下一个可用的寄存器或者内存地址
 * 但是这样就需要占用 rbp 偏移，怎么做？
 * 每个函数定义的最开始已经使用 sub n => rsp, 已经申请了栈空间 [0 ~ n]
 * 后续函数中调用其他函数所使用的栈空间都是在 [n ~ 无限] 中, 直接通过 push 操作即可
 * 但是需要注意 push 的顺序，最后的参数先 push (也就是指令反向 merge)
 * 如果返回了 NULL 就说明没有可用的寄存器啦，加一条 push 就行了
 * @param count
 * @param size
 * @return
 */
reg_t *amd64_fn_param_next_reg(uint8_t *used, type_base_t base) {
    uint8_t size = type_base_sizeof(base);
    // TODO 更多 float 类型, float 虽然栈大小为 8byte, 但是使用的寄存器确是 16byte 的
    if (base == TYPE_FLOAT) {
        size = OWORD;
    }

    uint8_t used_index = 0; // 8bit ~ 64bit
    if (size > QWORD) {
        used_index = 1;
    }
    uint8_t count = used[used_index]++;
    uint8_t int_param_indexes[] = {7, 6, 2, 1, 8, 9};
    // 通用寄存器 (0~5 = 6 个) rdi, rsi, rdx, rcx, r8, r9
    if (size <= QWORD && count <= 5) {
        uint8_t reg_index = int_param_indexes[count];
        return (reg_t *) reg_find(reg_index, size);
    }

    // 浮点寄存器(0~7 = 8 个) xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7
    if (size > QWORD && count <= 7) {
        return (reg_t *) reg_find(count, size);
    }

    return NULL;
}

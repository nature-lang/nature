#include "amd64.h"
#include "src/type.h"
#include "utils/slice.h"

void amd64_register_init() {
    rax = reg_new("rax", 0, QWORD);
    rcx = reg_new("rcx", 1, QWORD);
    rdx = reg_new("rdx", 2, QWORD);
    rbx = reg_new("rbx", 3, QWORD);
    rsp = reg_new("rsp", 4, QWORD);
    rbp = reg_new("rbp", 5, QWORD);
    rsi = reg_new("rsi", 6, QWORD);
    rdi = reg_new("rdi", 7, QWORD);
    r8 = reg_new("r8", 8, QWORD);
    r9 = reg_new("r9", 9, QWORD);
    r10 = reg_new("r10", 10, QWORD);
    r11 = reg_new("r11", 11, QWORD);
    r12 = reg_new("r12", 12, QWORD);
    r13 = reg_new("r13", 13, QWORD);
    r14 = reg_new("r14", 14, QWORD);
    r15 = reg_new("r15", 15, QWORD);

    eax = reg_new("eax", 0, DWORD);
    ecx = reg_new("ecx", 1, DWORD);
    edx = reg_new("edx", 2, DWORD);
    ebx = reg_new("ebx", 3, DWORD);
    esp = reg_new("esp", 4, DWORD);
    ebp = reg_new("ebp", 5, DWORD);
    esi = reg_new("esi", 6, DWORD);
    edi = reg_new("edi", 7, DWORD);
    r8d = reg_new("r8d", 8, DWORD);
    r9d = reg_new("r9d", 9, DWORD);
    r10d = reg_new("r10d", 10, DWORD);
    r11d = reg_new("r11d", 11, DWORD);
    r12d = reg_new("r12d", 12, DWORD);
    r13d = reg_new("r13d", 13, DWORD);
    r14d = reg_new("r14d", 14, DWORD);
    r15d = reg_new("r15d", 15, DWORD);

    ax = reg_new("ax", 0, WORD);
    cx = reg_new("cx", 1, WORD);
    dx = reg_new("dx", 2, WORD);
    bx = reg_new("bx", 3, WORD);
    sp = reg_new("sp", 4, WORD);
    bp = reg_new("bp", 5, WORD);
    si = reg_new("si", 6, WORD);
    di = reg_new("di", 7, WORD);
    r8w = reg_new("r8w", 8, WORD);
    r9w = reg_new("r9w", 9, WORD);
    r10w = reg_new("r10w", 10, WORD);
    r11w = reg_new("r11w", 11, WORD);
    r12w = reg_new("r12w", 12, WORD);
    r13w = reg_new("r13w", 13, WORD);
    r14w = reg_new("r14w", 14, WORD);
    r15w = reg_new("r15w", 15, WORD);

    // 正常来说也应该是 0, 1, 2, 3, 但是 intel 手册规定了使用 4，5，6, 7
    ah = reg_new("ah", 4, BYTE);
    ch = reg_new("ch", 5, BYTE);
    dh = reg_new("dh", 6, BYTE);
    bh = reg_new("bh", 7, BYTE);

    al = reg_new("al", 0, BYTE);
    cl = reg_new("cl", 1, BYTE);
    dl = reg_new("dl", 2, BYTE);
    bl = reg_new("bl", 3, BYTE);
    spl = reg_new("spl", 4, BYTE);
    bpl = reg_new("bpl", 5, BYTE);
    sil = reg_new("sil", 6, BYTE);
    dil = reg_new("dil", 7, BYTE);
    r8b = reg_new("r8b", 8, BYTE);
    r9b = reg_new("r9b", 9, BYTE);
    r10b = reg_new("r10b", 10, BYTE);
    r11b = reg_new("r11b", 11, BYTE);
    r12b = reg_new("r12b", 12, BYTE);
    r13b = reg_new("r13b", 13, BYTE);
    r14b = reg_new("r14b", 14, BYTE);
    r15b = reg_new("r15b", 15, BYTE);

    xmm0 = reg_new("xmm0", 0, OWORD);
    xmm1 = reg_new("xmm1", 1, OWORD);
    xmm2 = reg_new("xmm2", 2, OWORD);
    xmm3 = reg_new("xmm3", 3, OWORD);
    xmm4 = reg_new("xmm4", 4, OWORD);
    xmm5 = reg_new("xmm5", 5, OWORD);
    xmm6 = reg_new("xmm6", 6, OWORD);
    xmm7 = reg_new("xmm7", 7, OWORD);
    xmm8 = reg_new("xmm8", 8, OWORD);
    xmm9 = reg_new("xmm9", 9, OWORD);
    xmm10 = reg_new("xmm10", 10, OWORD);
    xmm11 = reg_new("xmm11", 11, OWORD);
    xmm12 = reg_new("xmm12", 12, OWORD);
    xmm13 = reg_new("xmm13", 13, OWORD);
    xmm14 = reg_new("xmm14", 14, OWORD);
    xmm15 = reg_new("xmm15", 15, OWORD);

    ymm0 = reg_new("ymm0", 0, YWORD);
    ymm1 = reg_new("ymm1", 1, YWORD);
    ymm2 = reg_new("ymm2", 2, YWORD);
    ymm3 = reg_new("ymm3", 3, YWORD);
    ymm4 = reg_new("ymm4", 4, YWORD);
    ymm5 = reg_new("ymm5", 5, YWORD);
    ymm6 = reg_new("ymm6", 6, YWORD);
    ymm7 = reg_new("ymm7", 7, YWORD);
    ymm8 = reg_new("ymm8", 8, YWORD);
    ymm9 = reg_new("ymm9", 9, YWORD);
    ymm10 = reg_new("ymm10", 10, YWORD);
    ymm11 = reg_new("ymm11", 11, YWORD);
    ymm12 = reg_new("ymm12", 12, YWORD);
    ymm13 = reg_new("ymm13", 13, YWORD);
    ymm14 = reg_new("ymm14", 14, YWORD);
    ymm15 = reg_new("ymm15", 15, YWORD);

    zmm0 = reg_new("zmm0", 0, ZWORD);
    zmm1 = reg_new("zmm1", 1, ZWORD);
    zmm2 = reg_new("zmm2", 2, ZWORD);
    zmm3 = reg_new("zmm3", 3, ZWORD);
    zmm4 = reg_new("zmm4", 4, ZWORD);
    zmm5 = reg_new("zmm5", 5, ZWORD);
    zmm6 = reg_new("zmm6", 6, ZWORD);
    zmm7 = reg_new("zmm7", 7, ZWORD);
    zmm8 = reg_new("zmm8", 8, ZWORD);
    zmm9 = reg_new("zmm9", 9, ZWORD);
    zmm10 = reg_new("zmm10", 10, ZWORD);
    zmm11 = reg_new("zmm11", 11, ZWORD);
    zmm12 = reg_new("zmm12", 12, ZWORD);
    zmm13 = reg_new("zmm13", 13, ZWORD);
    zmm14 = reg_new("zmm14", 14, ZWORD);
    zmm15 = reg_new("zmm15", 15, ZWORD);

}

/**
 * operations operations 目前属于一个更加抽象的层次，不利于寄存器分配，所以对齐进行更加本土化的处理
 * 1. 部分指令需要 fixed register, 比如 return,div,shl,shr 等
 * @param c
 */
void amd64_operations_lower(closure *c) {
    // 按基本块遍历所有指令
    SLICE_FOR(c->blocks) {
        lir_basic_block *block = SLICE_VALUE(c->blocks);
        LIST_FOR(block->operations) {
            lir_op *op = LIST_VALUE();
            if (op->code == LIR_OPCODE_RETURN && op->result != NULL) {
                // 1.1 return 指令需要将返回值放到 rax 中
                lir_operand *reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_REG, rax);
                lir_op *before = lir_op_move(reg_operand, op->result);
                op->result = reg_operand;
                list_insert_before(block->operations, LIST_VALUE(), before);
            }

            // div 被输数，除数 = 商
            if (op->code == LIR_OPCODE_DIV) {
                lir_operand *reg_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_REG, rax);
                lir_op *before = lir_op_move(reg_operand, op->first);
                lir_op *after = lir_op_move(op->result, reg_operand);
                op->first = reg_operand;
                op->result = reg_operand;
                list_insert_before(block->operations, LIST_VALUE(), before);
                list_insert_after(block->operations, LIST_VALUE(), after);
            }
        }
    }
}

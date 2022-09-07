#include "register.h"
#include "utils/error.h"
#include "utils/helper.h"
#include "src/register/register.h"


void x86_64_register_init() {
    x86_64_regs_table = table_new();

    rax = x86_64_register_operand_new("rax", 0, QWORD);
    rcx = x86_64_register_operand_new("rcx", 1, QWORD);
    rdx = x86_64_register_operand_new("rdx", 2, QWORD);
    rbx = x86_64_register_operand_new("rbx", 3, QWORD);
    rsp = x86_64_register_operand_new("rsp", 4, QWORD);
    rbp = x86_64_register_operand_new("rbp", 5, QWORD);
    rsi = x86_64_register_operand_new("rsi", 6, QWORD);
    rdi = x86_64_register_operand_new("rdi", 7, QWORD);
    r8 = x86_64_register_operand_new("r8", 8, QWORD);
    r9 = x86_64_register_operand_new("r9", 9, QWORD);
    r10 = x86_64_register_operand_new("r10", 10, QWORD);
    r11 = x86_64_register_operand_new("r11", 11, QWORD);
    r12 = x86_64_register_operand_new("r12", 12, QWORD);
    r13 = x86_64_register_operand_new("r13", 13, QWORD);
    r14 = x86_64_register_operand_new("r14", 14, QWORD);
    r15 = x86_64_register_operand_new("r15", 15, QWORD);

    eax = x86_64_register_operand_new("eax", 0, DWORD);
    ecx = x86_64_register_operand_new("ecx", 1, DWORD);
    edx = x86_64_register_operand_new("edx", 2, DWORD);
    ebx = x86_64_register_operand_new("ebx", 3, DWORD);
    esp = x86_64_register_operand_new("esp", 4, DWORD);
    ebp = x86_64_register_operand_new("ebp", 5, DWORD);
    esi = x86_64_register_operand_new("esi", 6, DWORD);
    edi = x86_64_register_operand_new("edi", 7, DWORD);
    r8d = x86_64_register_operand_new("r8d", 8, DWORD);
    r9d = x86_64_register_operand_new("r9d", 9, DWORD);
    r10d = x86_64_register_operand_new("r10d", 10, DWORD);
    r11d = x86_64_register_operand_new("r11d", 11, DWORD);
    r12d = x86_64_register_operand_new("r12d", 12, DWORD);
    r13d = x86_64_register_operand_new("r13d", 13, DWORD);
    r14d = x86_64_register_operand_new("r14d", 14, DWORD);
    r15d = x86_64_register_operand_new("r15d", 15, DWORD);

    ax = x86_64_register_operand_new("ax", 0, WORD);
    cx = x86_64_register_operand_new("cx", 1, WORD);
    dx = x86_64_register_operand_new("dx", 2, WORD);
    bx = x86_64_register_operand_new("bx", 3, WORD);
    sp = x86_64_register_operand_new("sp", 4, WORD);
    bp = x86_64_register_operand_new("bp", 5, WORD);
    si = x86_64_register_operand_new("si", 6, WORD);
    di = x86_64_register_operand_new("di", 7, WORD);
    r8w = x86_64_register_operand_new("r8w", 8, WORD);
    r9w = x86_64_register_operand_new("r9w", 9, WORD);
    r10w = x86_64_register_operand_new("r10w", 10, WORD);
    r11w = x86_64_register_operand_new("r11w", 11, WORD);
    r12w = x86_64_register_operand_new("r12w", 12, WORD);
    r13w = x86_64_register_operand_new("r13w", 13, WORD);
    r14w = x86_64_register_operand_new("r14w", 14, WORD);
    r15w = x86_64_register_operand_new("r15w", 15, WORD);

    // 正常来说也应该是 0, 1, 2, 3, 但是 intel 手册规定了使用 4，5，6, 7
    ah = x86_64_register_operand_new("ah", 4, BYTE);
    ch = x86_64_register_operand_new("ch", 5, BYTE);
    dh = x86_64_register_operand_new("dh", 6, BYTE);
    bh = x86_64_register_operand_new("bh", 7, BYTE);

    al = x86_64_register_operand_new("al", 0, BYTE);
    cl = x86_64_register_operand_new("cl", 1, BYTE);
    dl = x86_64_register_operand_new("dl", 2, BYTE);
    bl = x86_64_register_operand_new("bl", 3, BYTE);
    spl = x86_64_register_operand_new("spl", 4, BYTE);
    bpl = x86_64_register_operand_new("bpl", 5, BYTE);
    sil = x86_64_register_operand_new("sil", 6, BYTE);
    dil = x86_64_register_operand_new("dil", 7, BYTE);
    r8b = x86_64_register_operand_new("r8b", 8, BYTE);
    r9b = x86_64_register_operand_new("r9b", 9, BYTE);
    r10b = x86_64_register_operand_new("r10b", 10, BYTE);
    r11b = x86_64_register_operand_new("r11b", 11, BYTE);
    r12b = x86_64_register_operand_new("r12b", 12, BYTE);
    r13b = x86_64_register_operand_new("r13b", 13, BYTE);
    r14b = x86_64_register_operand_new("r14b", 14, BYTE);
    r15b = x86_64_register_operand_new("r15b", 15, BYTE);

    xmm0 = x86_64_register_operand_new("xmm0", 0, OWORD);
    xmm1 = x86_64_register_operand_new("xmm1", 1, OWORD);
    xmm2 = x86_64_register_operand_new("xmm2", 2, OWORD);
    xmm3 = x86_64_register_operand_new("xmm3", 3, OWORD);
    xmm4 = x86_64_register_operand_new("xmm4", 4, OWORD);
    xmm5 = x86_64_register_operand_new("xmm5", 5, OWORD);
    xmm6 = x86_64_register_operand_new("xmm6", 6, OWORD);
    xmm7 = x86_64_register_operand_new("xmm7", 7, OWORD);
    xmm8 = x86_64_register_operand_new("xmm8", 8, OWORD);
    xmm9 = x86_64_register_operand_new("xmm9", 9, OWORD);
    xmm10 = x86_64_register_operand_new("xmm10", 10, OWORD);
    xmm11 = x86_64_register_operand_new("xmm11", 11, OWORD);
    xmm12 = x86_64_register_operand_new("xmm12", 12, OWORD);
    xmm13 = x86_64_register_operand_new("xmm13", 13, OWORD);
    xmm14 = x86_64_register_operand_new("xmm14", 14, OWORD);
    xmm15 = x86_64_register_operand_new("xmm15", 15, OWORD);

    ymm0 = x86_64_register_operand_new("ymm0", 0, YWORD);
    ymm1 = x86_64_register_operand_new("ymm1", 1, YWORD);
    ymm2 = x86_64_register_operand_new("ymm2", 2, YWORD);
    ymm3 = x86_64_register_operand_new("ymm3", 3, YWORD);
    ymm4 = x86_64_register_operand_new("ymm4", 4, YWORD);
    ymm5 = x86_64_register_operand_new("ymm5", 5, YWORD);
    ymm6 = x86_64_register_operand_new("ymm6", 6, YWORD);
    ymm7 = x86_64_register_operand_new("ymm7", 7, YWORD);
    ymm8 = x86_64_register_operand_new("ymm8", 8, YWORD);
    ymm9 = x86_64_register_operand_new("ymm9", 9, YWORD);
    ymm10 = x86_64_register_operand_new("ymm10", 10, YWORD);
    ymm11 = x86_64_register_operand_new("ymm11", 11, YWORD);
    ymm12 = x86_64_register_operand_new("ymm12", 12, YWORD);
    ymm13 = x86_64_register_operand_new("ymm13", 13, YWORD);
    ymm14 = x86_64_register_operand_new("ymm14", 14, YWORD);
    ymm15 = x86_64_register_operand_new("ymm15", 15, YWORD);

    zmm0 = x86_64_register_operand_new("zmm0", 0, ZWORD);
    zmm1 = x86_64_register_operand_new("zmm1", 1, ZWORD);
    zmm2 = x86_64_register_operand_new("zmm2", 2, ZWORD);
    zmm3 = x86_64_register_operand_new("zmm3", 3, ZWORD);
    zmm4 = x86_64_register_operand_new("zmm4", 4, ZWORD);
    zmm5 = x86_64_register_operand_new("zmm5", 5, ZWORD);
    zmm6 = x86_64_register_operand_new("zmm6", 6, ZWORD);
    zmm7 = x86_64_register_operand_new("zmm7", 7, ZWORD);
    zmm8 = x86_64_register_operand_new("zmm8", 8, ZWORD);
    zmm9 = x86_64_register_operand_new("zmm9", 9, ZWORD);
    zmm10 = x86_64_register_operand_new("zmm10", 10, ZWORD);
    zmm11 = x86_64_register_operand_new("zmm11", 11, ZWORD);
    zmm12 = x86_64_register_operand_new("zmm12", 12, ZWORD);
    zmm13 = x86_64_register_operand_new("zmm13", 13, ZWORD);
    zmm14 = x86_64_register_operand_new("zmm14", 14, ZWORD);
    zmm15 = x86_64_register_operand_new("zmm15", 15, ZWORD);

}

static string x86_64_register_table_key(uint8_t index, uint8_t size) {
    uint16_t int_key = ((uint16_t) index << 8) | size;
    return itoa(int_key);
}

asm_operand_register_t *x86_64_register_find(uint8_t index, uint8_t size) {
    return table_get(x86_64_regs_table, x86_64_register_table_key(index, size));
}

asm_operand_register_t *x86_64_register_operand_new(char *name, uint8_t index, uint8_t size) {
    asm_operand_register_t *reg = NEW(asm_operand_register_t);
    reg->name = name;
    reg->index = index;
    reg->size = size;

    table_set(x86_64_regs_table, x86_64_register_table_key(index, size), reg);
    return reg;
}

regs_t *register_find(uint8_t index, uint8_t size) {
    if (true) {
        return (regs_t *) x86_64_register_find(index, size);
    }
    return NULL;
}

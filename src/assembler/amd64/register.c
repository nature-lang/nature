#include "register.h"
#include "src/lib/error.h"
#include "src/lib/helper.h"
#include "src/register/register.h"


void amd64_register_init() {
    amd64_regs_table = table_new();

    rax = amd64_register_operand_new("rax", 0, QWORD);
    rcx = amd64_register_operand_new("rcx", 1, QWORD);
    rdx = amd64_register_operand_new("rdx", 2, QWORD);
    rbx = amd64_register_operand_new("rbx", 3, QWORD);
    rsp = amd64_register_operand_new("rsp", 4, QWORD);
    rbp = amd64_register_operand_new("rbp", 5, QWORD);
    rsi = amd64_register_operand_new("rsi", 6, QWORD);
    rdi = amd64_register_operand_new("rdi", 7, QWORD);
    r8 = amd64_register_operand_new("r8", 8, QWORD);
    r9 = amd64_register_operand_new("r9", 9, QWORD);
    r10 = amd64_register_operand_new("r10", 10, QWORD);
    r11 = amd64_register_operand_new("r11", 11, QWORD);
    r12 = amd64_register_operand_new("r12", 12, QWORD);
    r13 = amd64_register_operand_new("r13", 13, QWORD);
    r14 = amd64_register_operand_new("r14", 14, QWORD);
    r15 = amd64_register_operand_new("r15", 15, QWORD);

    eax = amd64_register_operand_new("eax", 0, DWORD);
    ecx = amd64_register_operand_new("ecx", 1, DWORD);
    edx = amd64_register_operand_new("edx", 2, DWORD);
    ebx = amd64_register_operand_new("ebx", 3, DWORD);
    esp = amd64_register_operand_new("esp", 4, DWORD);
    ebp = amd64_register_operand_new("ebp", 5, DWORD);
    esi = amd64_register_operand_new("esi", 6, DWORD);
    edi = amd64_register_operand_new("edi", 7, DWORD);
    r8d = amd64_register_operand_new("r8d", 8, DWORD);
    r9d = amd64_register_operand_new("r9d", 9, DWORD);
    r10d = amd64_register_operand_new("r10d", 10, DWORD);
    r11d = amd64_register_operand_new("r11d", 11, DWORD);
    r12d = amd64_register_operand_new("r12d", 12, DWORD);
    r13d = amd64_register_operand_new("r13d", 13, DWORD);
    r14d = amd64_register_operand_new("r14d", 14, DWORD);
    r15d = amd64_register_operand_new("r15d", 15, DWORD);

    ax = amd64_register_operand_new("ax", 0, WORD);
    cx = amd64_register_operand_new("cx", 1, WORD);
    dx = amd64_register_operand_new("dx", 2, WORD);
    bx = amd64_register_operand_new("bx", 3, WORD);
    sp = amd64_register_operand_new("sp", 4, WORD);
    bp = amd64_register_operand_new("bp", 5, WORD);
    si = amd64_register_operand_new("si", 6, WORD);
    di = amd64_register_operand_new("di", 7, WORD);
    r8w = amd64_register_operand_new("r8w", 8, WORD);
    r9w = amd64_register_operand_new("r9w", 9, WORD);
    r10w = amd64_register_operand_new("r10w", 10, WORD);
    r11w = amd64_register_operand_new("r11w", 11, WORD);
    r12w = amd64_register_operand_new("r12w", 12, WORD);
    r13w = amd64_register_operand_new("r13w", 13, WORD);
    r14w = amd64_register_operand_new("r14w", 14, WORD);
    r15w = amd64_register_operand_new("r15w", 15, WORD);

    al = amd64_register_operand_new("al", 0, BYTE);
    cl = amd64_register_operand_new("cl", 1, BYTE);
    dl = amd64_register_operand_new("dl", 2, BYTE);
    bl = amd64_register_operand_new("bl", 3, BYTE);
    spl = amd64_register_operand_new("spl", 4, BYTE);
    bpl = amd64_register_operand_new("bpl", 5, BYTE);
    sil = amd64_register_operand_new("sil", 6, BYTE);
    dil = amd64_register_operand_new("dil", 7, BYTE);
    r8b = amd64_register_operand_new("r8b", 8, BYTE);
    r9b = amd64_register_operand_new("r9b", 9, BYTE);
    r10b = amd64_register_operand_new("r10b", 10, BYTE);
    r11b = amd64_register_operand_new("r11b", 11, BYTE);
    r12b = amd64_register_operand_new("r12b", 12, BYTE);
    r13b = amd64_register_operand_new("r13b", 13, BYTE);
    r14b = amd64_register_operand_new("r14b", 14, BYTE);
    r15b = amd64_register_operand_new("r15b", 15, BYTE);

    ah = amd64_register_operand_new("ah", 4, BYTE);
    ch = amd64_register_operand_new("ch", 5, BYTE);
    dh = amd64_register_operand_new("dh", 6, BYTE);
    bh = amd64_register_operand_new("bh", 7, BYTE);

    xmm0 = amd64_register_operand_new("xmm0", 0, OWORD);
    xmm1 = amd64_register_operand_new("xmm1", 1, OWORD);
    xmm2 = amd64_register_operand_new("xmm2", 2, OWORD);
    xmm3 = amd64_register_operand_new("xmm3", 3, OWORD);
    xmm4 = amd64_register_operand_new("xmm4", 4, OWORD);
    xmm5 = amd64_register_operand_new("xmm5", 5, OWORD);
    xmm6 = amd64_register_operand_new("xmm6", 6, OWORD);
    xmm7 = amd64_register_operand_new("xmm7", 7, OWORD);
    xmm8 = amd64_register_operand_new("xmm8", 8, OWORD);
    xmm9 = amd64_register_operand_new("xmm9", 9, OWORD);
    xmm10 = amd64_register_operand_new("xmm10", 10, OWORD);
    xmm11 = amd64_register_operand_new("xmm11", 11, OWORD);
    xmm12 = amd64_register_operand_new("xmm12", 12, OWORD);
    xmm13 = amd64_register_operand_new("xmm13", 13, OWORD);
    xmm14 = amd64_register_operand_new("xmm14", 14, OWORD);
    xmm15 = amd64_register_operand_new("xmm15", 15, OWORD);

    ymm0 = amd64_register_operand_new("ymm0", 0, YWORD);
    ymm1 = amd64_register_operand_new("ymm1", 1, YWORD);
    ymm2 = amd64_register_operand_new("ymm2", 2, YWORD);
    ymm3 = amd64_register_operand_new("ymm3", 3, YWORD);
    ymm4 = amd64_register_operand_new("ymm4", 4, YWORD);
    ymm5 = amd64_register_operand_new("ymm5", 5, YWORD);
    ymm6 = amd64_register_operand_new("ymm6", 6, YWORD);
    ymm7 = amd64_register_operand_new("ymm7", 7, YWORD);
    ymm8 = amd64_register_operand_new("ymm8", 8, YWORD);
    ymm9 = amd64_register_operand_new("ymm9", 9, YWORD);
    ymm10 = amd64_register_operand_new("ymm10", 10, YWORD);
    ymm11 = amd64_register_operand_new("ymm11", 11, YWORD);
    ymm12 = amd64_register_operand_new("ymm12", 12, YWORD);
    ymm13 = amd64_register_operand_new("ymm13", 13, YWORD);
    ymm14 = amd64_register_operand_new("ymm14", 14, YWORD);
    ymm15 = amd64_register_operand_new("ymm15", 15, YWORD);

    zmm0 = amd64_register_operand_new("zmm0", 0, ZWORD);
    zmm1 = amd64_register_operand_new("zmm1", 1, ZWORD);
    zmm2 = amd64_register_operand_new("zmm2", 2, ZWORD);
    zmm3 = amd64_register_operand_new("zmm3", 3, ZWORD);
    zmm4 = amd64_register_operand_new("zmm4", 4, ZWORD);
    zmm5 = amd64_register_operand_new("zmm5", 5, ZWORD);
    zmm6 = amd64_register_operand_new("zmm6", 6, ZWORD);
    zmm7 = amd64_register_operand_new("zmm7", 7, ZWORD);
    zmm8 = amd64_register_operand_new("zmm8", 8, ZWORD);
    zmm9 = amd64_register_operand_new("zmm9", 9, ZWORD);
    zmm10 = amd64_register_operand_new("zmm10", 10, ZWORD);
    zmm11 = amd64_register_operand_new("zmm11", 11, ZWORD);
    zmm12 = amd64_register_operand_new("zmm12", 12, ZWORD);
    zmm13 = amd64_register_operand_new("zmm13", 13, ZWORD);
    zmm14 = amd64_register_operand_new("zmm14", 14, ZWORD);
    zmm15 = amd64_register_operand_new("zmm15", 15, ZWORD);

}

static string amd64_register_table_key(uint8_t index, uint8_t size) {
    uint16_t int_key = ((uint16_t) index << 8) | size;
    return itoa(int_key);
}

asm_operand_register_t *amd64_register_find(uint8_t index, uint8_t size) {
    return table_get(amd64_regs_table, amd64_register_table_key(index, size));
}

asm_operand_register_t *amd64_register_operand_new(char *name, uint8_t index, uint8_t size) {
    asm_operand_register_t *reg = NEW(asm_operand_register_t);
    reg->name = name;
    reg->index = index;
    reg->size = size;

    table_set(amd64_regs_table, amd64_register_table_key(index, size), reg);
    return reg;
}

regs_t *register_find(uint8_t index, uint8_t size) {
    if (true) {
        return (regs_t *) amd64_register_find(index, size);
    }
    return NULL;
}

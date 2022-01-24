#ifndef NATURE_SRC_ASSEMBLER_AMD64_REGISTER_H_
#define NATURE_SRC_ASSEMBLER_AMD64_REGISTER_H_

#include "asm.h"

asm_operand_register rax = {"rax", 0, QWORD};
asm_operand_register rcx = {"rcx", 1, QWORD};
asm_operand_register rdx = {"rcx", 2, QWORD};
asm_operand_register rbx = {"rbx", 3, QWORD};
asm_operand_register rsp = {"rsp", 4, QWORD};
asm_operand_register rbp = {"rbp", 5, QWORD};
asm_operand_register rsi = {"rsi", 6, QWORD};
asm_operand_register rdi = {"rdi", 7, QWORD};
asm_operand_register r8 = {"r8", 8, QWORD};
asm_operand_register r9 = {"r9", 9, QWORD};
asm_operand_register r10 = {"r10", 10, QWORD};
asm_operand_register r11 = {"r11", 11, QWORD};
asm_operand_register r12 = {"r12", 12, QWORD};
asm_operand_register r13 = {"r13", 13, QWORD};
asm_operand_register r14 = {"r14", 14, QWORD};
asm_operand_register r15 = {"r15", 15, QWORD};

asm_operand_register eax = {"eax", 0, DWORD}
asm_operand_register ecx = {"ecx", 1, DWORD}
asm_operand_register edx = {"edx", 2, DWORD}
asm_operand_register ebx = {"ebx", 3, DWORD}
asm_operand_register esp = {"esp", 4, DWORD}
asm_operand_register ebp = {"ebp", 5, DWORD}
asm_operand_register esi = {"esi", 6, DWORD}
asm_operand_register edi = {"edi", 7, DWORD}
asm_operand_register r8d = {"r8d", 8, DWORD}
asm_operand_register r9d = {"r9d", 9, DWORD}
asm_operand_register r10d = {"r10d", 10, DWORD}
asm_operand_register r11d = {"r11d", 11, DWORD}
asm_operand_register r12d = {"r12d", 12, DWORD}
asm_operand_register r13d = {"r13d", 13, DWORD}
asm_operand_register r14d = {"r14d", 14, DWORD}
asm_operand_register r15d = {"r15d", 15, DWORD}

asm_operand_register ax = {"ax", 0, WORD}
asm_operand_register cx = {"cx", 1, WORD}
asm_operand_register dx = {"dx", 2, WORD}
asm_operand_register bx = {"bx", 3, WORD}
asm_operand_register sp = {"sp", 4, WORD}
asm_operand_register bp = {"bp", 5, WORD}
asm_operand_register si = {"si", 6, WORD}
asm_operand_register di = {"di", 7, WORD}
asm_operand_register r8w = {"r8w", 8, WORD}
asm_operand_register r9w = {"r9w", 9, WORD}
asm_operand_register r10w = {"r10w", 10, WORD}
asm_operand_register r11w = {"r11w", 11, WORD}
asm_operand_register r12w = {"r12w", 12, WORD}
asm_operand_register r13w = {"r13w", 13, WORD}
asm_operand_register r14w = {"r14w", 14, WORD}
asm_operand_register r15w = {"r15w", 15, WORD}

asm_operand_register al = {"al", 0, BYTE}
asm_operand_register cl = {"cl", 1, BYTE}
asm_operand_register dl = {"dl", 2, BYTE}
asm_operand_register bl = {"bl", 3, BYTE}
asm_operand_register spl = {"spl", 4, BYTE}
asm_operand_register bpl = {"bpl", 5, BYTE}
asm_operand_register sil = {"sil", 6, BYTE}
asm_operand_register dil = {"dil", 7, BYTE}
asm_operand_register r8b = {"r8b", 8, BYTE}
asm_operand_register r9b = {"r9b", 9, BYTE}
asm_operand_register r10b = {"r10b", 10, BYTE}
asm_operand_register r11b = {"r11b", 11, BYTE}
asm_operand_register r12b = {"r12b", 12, BYTE}
asm_operand_register r13b = {"r13b", 13, BYTE}
asm_operand_register r14b = {"r14b", 14, BYTE}
asm_operand_register r15b = {"r15b", 15, BYTE}

asm_operand_register ah = {"ah", 4, BYTE}
asm_operand_register ch = {"ch", 5, BYTE}
asm_operand_register dh = {"dh", 6, BYTE}
asm_operand_register bh = {"bh", 7, BYTE}

asm_operand_register xmm0 = {"xmm0", 0, OWORD}
asm_operand_register xmm1 = {"xmm1", 1, OWORD}
asm_operand_register xmm2 = {"xmm2", 2, OWORD}
asm_operand_register xmm3 = {"xmm3", 3, OWORD}
asm_operand_register xmm4 = {"xmm4", 4, OWORD}
asm_operand_register xmm5 = {"xmm5", 5, OWORD}
asm_operand_register xmm6 = {"xmm6", 6, OWORD}
asm_operand_register xmm7 = {"xmm7", 7, OWORD}
asm_operand_register xmm8 = {"xmm8", 8, OWORD}
asm_operand_register xmm9 = {"xmm9", 9, OWORD}
asm_operand_register xmm10 = {"xmm10", 10, OWORD}
asm_operand_register xmm11 = {"xmm11", 11, OWORD}
asm_operand_register xmm12 = {"xmm12", 12, OWORD}
asm_operand_register xmm13 = {"xmm13", 13, OWORD}
asm_operand_register xmm14 = {"xmm14", 14, OWORD}
asm_operand_register xmm15 = {"xmm15", 15, OWORD}

asm_operand_register ymm0 = {"ymm0", 0, YWORD}
asm_operand_register ymm1 = {"ymm1", 1, YWORD}
asm_operand_register ymm2 = {"ymm2", 2, YWORD}
asm_operand_register ymm3 = {"ymm3", 3, YWORD}
asm_operand_register ymm4 = {"ymm4", 4, YWORD}
asm_operand_register ymm5 = {"ymm5", 5, YWORD}
asm_operand_register ymm6 = {"ymm6", 6, YWORD}
asm_operand_register ymm7 = {"ymm7", 7, YWORD}
asm_operand_register ymm8 = {"ymm8", 8, YWORD}
asm_operand_register ymm9 = {"ymm9", 9, YWORD}
asm_operand_register ymm10 = {"ymm10", 10, YWORD}
asm_operand_register ymm11 = {"ymm11", 11, YWORD}
asm_operand_register ymm12 = {"ymm12", 12, YWORD}
asm_operand_register ymm13 = {"ymm13", 13, YWORD}
asm_operand_register ymm14 = {"ymm14", 14, YWORD}
asm_operand_register ymm15 = {"ymm15", 15, YWORD}

asm_operand_register zmm0 = {"zmm0", 0, ZWORD}
asm_operand_register zmm1 = {"zmm1", 1, ZWORD}
asm_operand_register zmm2 = {"zmm2", 2, ZWORD}
asm_operand_register zmm3 = {"zmm3", 3, ZWORD}
asm_operand_register zmm4 = {"zmm4", 4, ZWORD}
asm_operand_register zmm5 = {"zmm5", 5, ZWORD}
asm_operand_register zmm6 = {"zmm6", 6, ZWORD}
asm_operand_register zmm7 = {"zmm7", 7, ZWORD}
asm_operand_register zmm8 = {"zmm8", 8, ZWORD}
asm_operand_register zmm9 = {"zmm9", 9, ZWORD}
asm_operand_register zmm10 = {"zmm10", 10, ZWORD}
asm_operand_register zmm11 = {"zmm11", 11, ZWORD}
asm_operand_register zmm12 = {"zmm12", 12, ZWORD}
asm_operand_register zmm13 = {"zmm13", 13, ZWORD}
asm_operand_register zmm14 = {"zmm14", 14, ZWORD}
asm_operand_register zmm15 = {"zmm15", 15, ZWORD}

#endif //NATURE_SRC_ASSEMBLER_AMD64_REGISTER_H_

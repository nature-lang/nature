#include "fndef.h"
#include <stdint.h>

typedef struct {
    uint64_t v_start; // 虚拟内存偏移
    uint64_t v_end; // 虚拟内存结束
    uint16_t opcode_size; // 当前 label 下的指令的总大小 (v_start+opcode_size = v_end)
    int stack_size; // 经过寄存器分配后可以计算出当前 fn 的栈总大小
} fndef_t;
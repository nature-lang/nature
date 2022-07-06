#include "builtin.h"

list *builtin_print() {
    asm_operand_t *string_entity_var = DISP_REG(rbp, -8);
    asm_operand_t *string_addr_var = DISP_REG(rbp, -16);
    asm_operand_t *string_len_var = DISP_REG(rbp, -24);

    list *inst_list = list_new();
    list_push(inst_list, ASM_INST("label", { SYMBOL("builtin_print", true, false) }));
    list_push(inst_list, ASM_INST("push", { REG(rbp) }));
    list_push(inst_list, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    list_push(inst_list, ASM_INST("sub", { REG(rsp), UINT32(24) })); // 防止其他函数调用占用这一段栈空间

    list_push(inst_list, ASM_INST("mov", { string_entity_var, REG(rdi) }));

    // runtime_call string_point => addr
    list_push(inst_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(inst_list, ASM_INST("call", { LABEL(RUNTIME_CALL_STRING_ADDR) }));
    list_push(inst_list, ASM_INST("mov", { string_addr_var, REG(rax) }));


    // runtime_call string_len => len
    list_push(inst_list, ASM_INST("mov", { REG(rdi), string_entity_var }));
    list_push(inst_list, ASM_INST("call", { SYMBOL(RUNTIME_CALL_STRING_LENGTH, true, false) }));
    list_push(inst_list, ASM_INST("mov", { string_len_var, REG(rax) }));


    // syscall
    list_push(inst_list, ASM_INST("mov", { REG(rax), UINT32(1) })); // 声明系统调用编号 write
    list_push(inst_list, ASM_INST("mov", { REG(rdi), UINT32(1) })); // 参数 1： 声明系统输出为标准输出 STDOUT_FILENO
    list_push(inst_list, ASM_INST("mov", { REG(rsi), string_addr_var })); // 参数 2: 字符串地址
    list_push(inst_list, ASM_INST("mov", { REG(rdx), string_len_var })); // 参数 3：字符串长度
    list_push(inst_list, ASM_INST("syscall", {})); // syscall

    // linux abi 要求 callee 在结束函数块前恢复 rsp 和 rpb
    // pop rbp 将恢复 caller 的栈帧赋, 且rsp - 8,
    list_push(inst_list, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    list_push(inst_list, ASM_INST("pop", { REG(rbp) }));
    list_push(inst_list, ASM_INST("ret", {}));

    return inst_list;
}
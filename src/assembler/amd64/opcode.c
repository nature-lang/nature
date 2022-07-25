#include "opcode.h"
#include "string.h"
#include "src/lib/error.h"
#include "src/lib/helper.h"
#include <math.h>

inst_t movsq = {
        "movsq", 0, {0xA5}, {OPCODE_EXT_REX_W},
        {}
};

inst_t call_rm64 = {"call", 0, {0xFF}, {OPCODE_EXT_SLASH2,},
                    {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};

inst_t call_rel32 = {"call", 0, {0xE8}, {OPCODE_EXT_IMM_DWORD},
                     {
                             OPERAND_TYPE_REL32, ENCODING_TYPE_IMM
                     }
};

inst_t jmp_rel8 = {
        "jmp", 0, {0xEB}, {OPCODE_EXT_IMM_BYTE}, {
                OPERAND_TYPE_REL8, ENCODING_TYPE_IMM
        }
};

inst_t jmp_rel32 = {
        "jmp", 0, {0xE9}, {OPCODE_EXT_IMM_DWORD}, {
                OPERAND_TYPE_REL32, ENCODING_TYPE_IMM
        }
};

inst_t je_rel8 = {
        "je", 0, {0x74}, {OPCODE_EXT_IMM_BYTE}, {
                OPERAND_TYPE_REL8, ENCODING_TYPE_IMM
        }
};

inst_t je_rel32 = {
        "je", 0, {0x0F, 0x84}, {OPCODE_EXT_IMM_DWORD}, {
                OPERAND_TYPE_REL32, ENCODING_TYPE_IMM
        }
};


inst_t sub_imm32_rm64 = {"sub", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};


inst_t add_imm32_rm64 = {"add", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};

// intel 指令顺序
inst_t add_r64_rm64 = {"add", 0, {0x03}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};

inst_t add_rm64_r64 = {"add", 0, {0x01}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};

inst_t sub_imm8_rm64 = {"sub", 0, {0x83}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_BYTE},
                        {
                                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                        }
};

inst_t add_imm8_rm64 = {"add", 0, {0x83}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_BYTE},
                        {
                                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                        }
};


inst_t mov_rm8_r8 = {"mov", 0, {0x88}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                     }
};

inst_t mov_r8_rm8 = {"mov", 0, {0x8A}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                     }
};

// 注册指令列表 asm operand
inst_t mov_r16_rm16 = {"mov", 0x66, {0xB8}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                       }
};

inst_t mov_imm32_r32 = {"mov", 0, {0xB8}, {OPCODE_EXT_IMM_DWORD},
                        {
                                {OPERAND_TYPE_R32, ENCODING_TYPE_OPCODE_PLUS},
                                {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                        }
};

inst_t mov_rm64_imm32 = {"mov", 0, {0xC7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};

inst_t mov_r64_imm64 = {"mov", 0, {0xB8}, {OPCODE_EXT_REX_W, OPCODE_EXT_IMM_QWORD},
                        {
                                {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS},
                                {OPERAND_TYPE_IMM64, ENCODING_TYPE_IMM}
                        }
};

// intel 指令顺序
inst_t mov_r64_rm64 = {"mov", 0, {0x8B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};

inst_t mov_r32_rm32 = {"mov", 0, {0x8B}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
                       }
};

inst_t movsd_xmm1_xmm2 = {"mov", 0, {0xF2, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                          {
                                  {OPERAND_TYPE_XMM1, ENCODING_TYPE_MODRM_REG},
                                  {OPERAND_TYPE_XMM2, ENCODING_TYPE_MODRM_RM}
                          }
};


inst_t movsd_xmm1_m64 = {"mov", 0, {0xF2, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_XMM1, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                         }
};

inst_t movsd_xmm1m64_xmm2 = {"mov", 0, {0x0F2, 0x0F, 0x11}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1M64, ENCODING_TYPE_MODRM_RM},
                                     {OPERAND_TYPE_XMM2, ENCODING_TYPE_MODRM_REG}
                             }
};


inst_t mov_rm64_r64 = {"mov", 0, {0x89}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};

inst_t mov_rm32_r32 = {"mov", 0, {0x89}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}
                       }
};


inst_t lea_r64_m = {"lea", 0, {0x8D}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                    {
                            {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                            {OPERAND_TYPE_M, ENCODING_TYPE_MODRM_RM},
                    }
};

inst_t syscall = {"syscall", 0, {0x0F, 0x05}, {}, {}};

// TODO 什么时候使用 near 什么时候使用 far?
inst_t ret = {"ret", 0, {0xC3}, {}, {}};

inst_t push_r64 = {
        "push", 0, {0x50}, {},
        {
                {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS}
        }
};

inst_t push_rm64 = {
        "push", 0, {0xFF}, {OPCODE_EXT_SLASH6},
        {
                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
        }
};

inst_t pop_r64 = {
        "pop", 0, {0x58}, {},
        {
                {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS}
        }
};

inst_t pop_rm64 = {
        "pop", 0, {0x8F}, {OPCODE_EXT_SLASH0},
        {
                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
        }
};

// intel 指令顺序
inst_t cmp_r64_rm64 = {"cmp", 0, {0x3B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};

inst_t cmp_rm64_r64 = {"cmp", 0, {0x39}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};

inst_t cmp_rm64_imm8 = {"cmp", 0, {0x83}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_BYTE},
                        {
                                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                        }
};

inst_t cmp_rm8_imm8 = {"cmp", 0, {0x80}, {OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }
};

inst_t cmp_rm64_imm32 = {"cmp", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};

inst_t cmp_rax_imm32 = {"cmp", 0, {0x3D}, {OPCODE_EXT_REX_W, OPCODE_EXT_IMM_DWORD},
                        {
                                {OPERAND_TYPE_RAX, ENCODING_TYPE_MODRM_RAX},
                                {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                        }
};

inst_t cmp_al_imm32 = {"cmp", 0, {0x3C}, {OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }
};


inst_t setg_rm8 = {
        "setg", 0, {0x0F, 0x9F}, {}, {
                OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM
        }
};
inst_t setge_rm8 = {
        "setge", 0, {0x0F, 0x9D}, {}, {
                OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM
        }
};


static opcode_tree_node_t *opcode_node_new() {
    opcode_tree_node_t *node = NEW(opcode_tree_node_t);
    node->key = "";
    node->insts = (insts_t) {
            .count = 0,
            .list = malloc(sizeof(inst_t) * 10),
    };
    node->succs = table_new();
    return node;
}

/**
 * 将底层指令整理成树形结构, 如果没有特别声明则
 * operands 表示底层指令， 即 r16/r8/r64/rm
 * asm_operands 表示高层指令， 即 uint8/uint64/register
 *
 * 注册所有的底层指令集，并生成树，树的枝节点在构造树的过程中将底层指令参数转换为高层指令参数(这样可以方便查找)
 * 一个底层指令参数参数对应多个高层指令参数
 * 树的叶子节点是一个数组，数组的内容就是原始的底层指令集
 *
 * @param inst
 * @return
 */
void opcode_init() {
    opcode_tree_root = opcode_node_new();
    opcode_tree_root->key = "root";
    // 收集所有指令，进行注册
    opcode_tree_build(&call_rm64);
    opcode_tree_build(&call_rel32);
    opcode_tree_build(&jmp_rel8);
    opcode_tree_build(&jmp_rel32);
    opcode_tree_build(&je_rel8);
    opcode_tree_build(&je_rel32);
    opcode_tree_build(&ret);
    opcode_tree_build(&push_rm64);
    opcode_tree_build(&push_r64);
    opcode_tree_build(&pop_r64);
    opcode_tree_build(&pop_rm64);
    opcode_tree_build(&sub_imm32_rm64);
    opcode_tree_build(&sub_imm8_rm64);
    opcode_tree_build(&add_imm32_rm64);
    opcode_tree_build(&add_imm8_rm64);
    opcode_tree_build(&add_r64_rm64);
    opcode_tree_build(&add_rm64_r64);
    opcode_tree_build(&mov_rm8_r8);
    opcode_tree_build(&mov_r8_rm8);
    opcode_tree_build(&mov_r16_rm16);
    opcode_tree_build(&mov_imm32_r32);
    opcode_tree_build(&mov_rm64_imm32);
    opcode_tree_build(&mov_r64_imm64);
    opcode_tree_build(&mov_r64_rm64);
    opcode_tree_build(&mov_r32_rm32);
    opcode_tree_build(&mov_rm64_r64);
    opcode_tree_build(&mov_rm32_r32);
    opcode_tree_build(&movsd_xmm1_m64); // 内存到 xmm
    opcode_tree_build(&movsd_xmm1_xmm2); // 内存到 xmm
    opcode_tree_build(&movsd_xmm1m64_xmm2); // xmm 到内存或者xmm
    opcode_tree_build(&cmp_al_imm32);
    opcode_tree_build(&cmp_rax_imm32);
    opcode_tree_build(&cmp_r64_rm64);
    opcode_tree_build(&cmp_rm64_imm32);
    opcode_tree_build(&cmp_rm64_imm8);
    opcode_tree_build(&cmp_rm8_imm8);
    opcode_tree_build(&cmp_rm64_r64);
    opcode_tree_build(&setg_rm8);
    opcode_tree_build(&setge_rm8);
    opcode_tree_build(&lea_r64_m);
    opcode_tree_build(&syscall);
}

/**
 * @param type
 * @param byte
 * @return
 */
uint16_t asm_operand_to_key(uint8_t type, uint8_t byte) {
    uint16_t flag = ((uint16_t) type << 8) | byte;
    return flag;
}

asm_keys_t operand_low_to_high(operand_type t) {
    asm_keys_t res = {
            .count = 0,
    };

    if (t == OPERAND_TYPE_REL8) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT8, 1);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_REL16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT16, 2);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_REL32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT32, 4);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM8) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 1);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 1);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 1);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM16) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 2);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 2);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 2);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM32) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 4);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 4);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 4);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM64) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 8);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 8);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 8);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M) {
        res.count = 2;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 8);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 2);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 4);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M64) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_IMM8) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT8, BYTE);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_IMM16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT16, WORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_IMM32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT32, DWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_IMM64) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT64, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R8) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 1);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 2);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 4);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R64) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RAX) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_XMM1 || t == OPERAND_TYPE_XMM2) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, OWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_XMM1M64 || t == OPERAND_TYPE_XMM2M64) {
        res.count = 6;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 8);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 16);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 8);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 8);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
        highs[5] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 8);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_YMM1 || t == OPERAND_TYPE_YMM2) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 32);
        res.list = highs;
        return res;
    }

    error_exit("cannot identify operand_type index: %d", t);
    return res;
}

void opcode_tree_build(inst_t *inst) {
    // 第一层结构 指令名称
    opcode_tree_node_t *node = opcode_find_name(inst->name);

    // 其余层级结构,指令参数
    opcode_find_succs(node, inst, 0);
}

/**
 * @param name
 */
opcode_tree_node_t *opcode_find_name(string name) {
    bool exist = table_exist(opcode_tree_root->succs, name);
    if (exist) {
        return table_get(opcode_tree_root->succs, name);
    }

    opcode_tree_node_t *node = opcode_node_new();
    node->key = name;

    table_set(opcode_tree_root->succs, name, node);

    return node;
}

/**
 * @param node 树节点
 * @return
 */
void opcode_find_succs(opcode_tree_node_t *node, inst_t *inst, int operands_index) {
    // 读取 node
    opcode_operand_t operand = inst->operands[operands_index];
    // 表示已经找到头了，甚至有可能溢出
    if (operand.type == 0) {
        node->insts.list[node->insts.count++] = inst;
        return;
    }

    // 将一个底层指令转换成高层指令
    asm_keys_t asm_keys = operand_low_to_high(operand.type);
    for (int i = 0; i < asm_keys.count; ++i) {
        uint16_t key_int = asm_keys.list[i];
        char *key = itoa(key_int);
        opcode_tree_node_t *succ = table_get(node->succs, key);
        if (succ == NULL) {
            succ = opcode_node_new();
            succ->key = key;
            table_set(node->succs, key, succ);
        }
        // 继续寻找下一级node
        opcode_find_succs(succ, inst, operands_index + 1);
    }
}

inst_t *opcode_select(asm_inst_t asm_inst) {
    opcode_tree_node_t *current = table_get(opcode_tree_root->succs, asm_inst.name);
    if (current == NULL) {
        error_exit("cannot identify asm opcode %s ", asm_inst.name);
        return NULL;
    }

    for (int i = 0; i < asm_inst.count; ++i) {
        asm_operand_t *operand = asm_inst.operands[i];
        // 生成 key
        string key = itoa(asm_operand_to_key(operand->type, operand->size));

        // current 匹配
        bool exists = table_exist(current->succs, key);
        if (!exists) {
            error_exit("cannot identify asm opcode %s with operand index: %d", asm_inst.name, i);
            return NULL;
        }
        current = table_get(current->succs, key);
    }

    insts_t insts = current->insts;
    opcode_sort_insts(&insts);

    return insts.list[0];
}

void opcode_sort_insts(insts_t *insts) {
    if (insts->count == 0) {
        return;
    }

    // [2, 3, 4, 5, 6] // 升序排列
    for (int i = 0; i < insts->count - 1; ++i) {
        bool change = false;
        for (int j = 0; j < insts->count - 1 - i; ++j) {
            if (insts->list[j]->operands[0].type > insts->list[j + 1]->operands[0].type) {
                inst_t *temp = insts->list[j];
                insts->list[j] = insts->list[j + 1];
                insts->list[j + 1] = temp;
                change = true;
            }
        }

        if (!change) {
            break;
        }
    }
}

static rex_prefix_t *new_rex_prefix() {
    rex_prefix_t *r = NEW(rex_prefix_t);
    r->b = false;
    r->r = false;
    r->w = false;
    r->x = false;
    return r;
}

static vex_prefix_t *new_vex_prefix() {
    vex_prefix_t *v = NEW(vex_prefix_t);
    v->source = 0;
    v->vex_legacy_byte = 0;
    v->vex_opcode_extension = 0;
    v->l = false;
    v->r = true;
    v->w = true;
    v->x = true;
    v->b = true;
    return v;
}

static modrm_t *new_modrm() {
    modrm_t *m = NEW(modrm_t);
    m->mod = 0;
    m->reg = 0;
    m->rm = 0;
    return m;
}

static void parser_ext(inst_format_t *format, opcode_ext ext) {
    if (ext == OPCODE_EXT_SLASH0) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 0;
    } else if (ext == OPCODE_EXT_SLASH1) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 1;
    } else if (ext == OPCODE_EXT_SLASH2) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 2;
    } else if (ext == OPCODE_EXT_SLASH3) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 3;
    } else if (ext == OPCODE_EXT_SLASH4) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 4;
    } else if (ext == OPCODE_EXT_SLASH5) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 5;
    } else if (ext == OPCODE_EXT_SLASH6) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 6;
    } else if (ext == OPCODE_EXT_SLASH7) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
        format->modrm->reg = 7;
    } else if (ext == OPCODE_EXT_SLASHR) {
        if (format->modrm == NULL) {
            format->modrm = new_modrm();
        }
    } else if (ext == OPCODE_EXT_REX_W) {
        if (format->rex_prefix == NULL) {
            format->rex_prefix = new_rex_prefix();
        }
        format->rex_prefix->w = true;
    } else if (ext == OPCODE_EXT_REX) {
        if (format->rex_prefix == NULL) {
            format->rex_prefix = new_rex_prefix();
        }
    } else if (ext == OPCODE_EXT_VEX_128) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
    } else if (ext == OPCODE_EXT_VEX_256) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->l = true;
    } else if (ext == OPCODE_EXT_VEX_66) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_66;
    } else if (ext == OPCODE_EXT_VEX_F2) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_F2;
    } else if (ext == OPCODE_EXT_VEX_F3) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_F3;
    } else if (ext == OPCODE_EXT_VEX_0F) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F;
    } else if (ext == OPCODE_EXT_VEX_0F_38) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F_38;
    } else if (ext == OPCODE_EXT_VEX_0F_3A) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F_3A;
    } else if (ext == OPCODE_EXT_VEX_W0) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->w = false;
    } else if (ext == OPCODE_EXT_VEX_W1) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->w = true;
    } else if (ext == OPCODE_EXT_VEX_WIG) {
        if (format->vex_prefix == NULL) {
            format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->w = false;
    }
}

static void set_disp(inst_format_t *format, string reg, uint8_t *disps, uint8_t count) {
    // 特殊 register 处理
    int j = 0;
    if (strcmp(reg, "rsp") == 0) {
        format->disps[j++] = 0x24;
    }

    for (int i = 0; i < count; i++) {
        format->disps[j++] = disps[i];
    };
    format->disp_count = count;
}

static void set_imm(inst_format_t *format, uint8_t *imms, uint8_t count) {
    for (int i = 0; i < count; ++i) {
        format->imms[i] = imms[i];
    }
    format->imm_count = count;
}

/**
 * 小端处理
 * @param src
 * @param dst
 */
static void int32_to_uint8(int32_t src, uint8_t dst[4]) {
//  memcpy(dst, &src, sizeof(src));
    dst[0] = src;
    dst[1] = src >> 8;
    dst[2] = src >> 16;
    dst[3] = src >> 24;
}

static sib_t *new_sib(uint8_t scale, uint8_t index, uint8_t base) {
    sib_t *s = NEW(sib_t);
    s->scale = scale;
    s->index = index;
    s->base = base;
    return s;
}

static inst_format_t *inst_format_new(uint8_t *opcode) {
    inst_format_t *format = NEW(inst_format_t);
    for (int i = 0; i < 3; ++i) {
        format->opcode[i] = opcode[i];
    }

    format->prefix = 0;
    format->vex_prefix = NULL;
    format->rex_prefix = NULL;
    format->modrm = NULL;
    format->sib = NULL;

    for (int i = 0; i < 8; ++i) {
        format->disps[0] = 0;
        format->imms[0] = 0;
    }
    format->disp_count = 0;
    format->imm_count = 0;
    return format;
}

/**
 *
 * @param asm_inst
 * @param inst
 * @return
 */
inst_format_t *opcode_fill(inst_t *inst, asm_inst_t asm_inst) {
    inst_format_t *format = inst_format_new(inst->opcode);

    if (asm_inst.prefix > 0) {
        inst->prefix = asm_inst.prefix;
    }

    // format 填充 prefixes
    if (inst->prefix > 0) {
        format->prefix = inst->prefix;
    }

    // format 填充 extensions
    int i = 0;
    bool ext_exists[OPCODE_EXT_EOF] = {false};
    while (inst->extensions[i] > 0) {
        opcode_ext ext = inst->extensions[i++];

        parser_ext(format, ext);
        ext_exists[ext] = true;
    }

    i = 0;
    while (inst->operands[i].type > 0) {
        opcode_operand_t operand = inst->operands[i];
        asm_operand_t *asm_operand = asm_inst.operands[i];
        // asm 参数填充
        if (asm_operand->type == ASM_OPERAND_TYPE_REGISTER) {
            asm_operand_register_t *r = asm_operand->value;
            if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                }

                format->modrm->mod = MODRM_MOD_DIRECT_REGISTER;
                format->modrm->rm = r->index;
                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->b = r->index > 7;
                } else if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
                    format->vex_prefix->b = r->index <= 7;
                }
            } else if (operand.encoding == ENCODING_TYPE_MODRM_RAX) {
                if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
                    format->vex_prefix->r = true;
                }
            } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                    format->modrm->mod = MODRM_MOD_DIRECT_REGISTER;
                }

                format->modrm->reg = r->index;
                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->r = r->index > 7; // 添加 rex_prefix-b 前缀
                } else if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
                    format->vex_prefix->r = r->index <= 7;
                }
            } else if (operand.encoding == ENCODING_TYPE_OPCODE_PLUS) { // opcode = opcode + reg
                format->opcode[0] += r->index & 7;

                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->b = r->index > 7;
                }
            } else if (operand.encoding == ENCODING_TYPE_VEX_VVVV) {
                if (format->vex_prefix == NULL) {
                    format->vex_prefix = new_vex_prefix();
                }
                format->vex_prefix->source = 15 - r->index; // two's complement
                format->vex_prefix->r = r->index <= 7;
            } else {
                error_exit("unsupported encoding %v", operand.encoding);
                return NULL;
            }

        } else if (asm_operand->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
            asm_operand_disp_register_t *r = asm_operand->value;
            if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                }

                format->modrm->rm = r->reg->index;

                format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
                uint8_t count = 1;

                // 设置 displacement 部分(disp 最多 8个字节，通过 8 字节拆分的形式传参)
                uint8_t temp[4];
                int32_to_uint8(r->disp, temp);
                if (abs(r->disp) > 128) {
                    count = 4;
                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_DWORD_DISP;
                }

                set_disp(format, r->reg->name, temp, count);

                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->b = r->reg->index > 7;
                }
            } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
                }

                format->modrm->reg = r->reg->index;
                uint8_t temp[] = {r->disp};
                set_disp(format, r->reg->name, temp, 1);

                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->r = r->reg->index > 7;
                }
            } else {
                error_exit("unsupported encoding %v", operand.encoding);
                return NULL;
            }

        } else if (asm_operand->type == ASM_OPERAND_TYPE_INDIRECT_REGISTER) {
            asm_operand_indirect_register_t *r = asm_operand->value;
            if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                }

                format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
                format->modrm->rm = r->reg->index;
                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->b = r->reg->index > 7;
                }
            } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
                }

                format->modrm->reg = r->reg->index;
                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                    format->rex_prefix->r = r->reg->index > 7;
                }
            } else {
                error_exit("unsupported encoding %v", operand.encoding);
                return NULL;
            }
        } else if (asm_operand->type == ASM_OPERAND_TYPE_RIP_RELATIVE) { // 还会影响 modrm?
            asm_operand_rip_relative_t *r = asm_operand->value;
            if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                }

                format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
                format->modrm->rm = 5; // rm 为啥还有个 5 ?, 是考虑的 disp 比较长？

                // 32 to uint8 []
                uint8_t temp[4];
                int32_to_uint8(r->disp, temp);
                set_disp(format, "", temp, 4);
            } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                }

                format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
                format->modrm->reg = 5;

                // 小端处理
                uint8_t temp[4];
                int32_to_uint8(r->disp, temp);
                set_disp(format, "", temp, 4);
            } else if (asm_operand->type == ASM_OPERAND_TYPE_SIB_REGISTER) {
                asm_operand_sib_register_t *r = asm_operand->value;
                if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                    if (format->modrm == NULL) {
                        format->modrm = new_modrm();
                    }

                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
                    format->modrm->rm = MODRM_MOD_SIB_FOLLOWS_RM;

                    format->sib = new_sib(r->scale, r->index->index, r->base->index);
                    if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                        format->rex_prefix->x = r->index->index > 7;
                        format->rex_prefix->b = r->base->index > 7;
                    }

                    if (r->base->index == 13) {
                        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
                        uint8_t temp[1] = {0};
                        set_disp(format, r->base->name, temp, 0);
                    }
                }
            }
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT64) {
            asm_operand_uint64_t *i = asm_operand->value;
            uint8_t temp[8];
            memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
            set_imm(format, temp, 8);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT64) {
            asm_operand_float64_t *f = asm_operand->value;
            uint8_t temp[8];
            memcpy(temp, &f->value, sizeof(f->value));
            set_imm(format, temp, 8);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT32) {
            asm_operand_uint32_t *i = asm_operand->value;
            uint8_t temp[4];
            memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
            set_imm(format, temp, 4);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT16) {
            asm_operand_uint16_t *i = asm_operand->value;
            uint8_t temp[2];
            memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
            set_imm(format, temp, 2);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT8) {
            asm_operand_uint8_t *i = asm_operand->value;
            uint8_t temp[1] = {i->value};
            set_imm(format, temp, 1);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT32) {
            asm_operand_float32_t *f = asm_operand->value;
            uint8_t temp[4];
            memcpy(temp, &f->value, sizeof(f->value));
            set_imm(format, temp, 8);
        } /*else if (asm_operand->type == ASM_OPERAND_TYPE_INT32) {
      asm_operand_int32 *i = asm_operand->value;
      uint8_t temp[4];
      memcpy(temp, &i->value, sizeof(i->value));
    } else if (asm_operand->type == ASM_OPERAND_TYPE_INT8) {
      asm_operand_int32 *i = asm_operand->value;
      uint8_t temp[4];
      memcpy(temp, &i->value, sizeof(i->value));
    }*/ else {
            error_exit("unsupported asm operand type %v", asm_operand->type);
            return NULL;
        }

        i++;
    }
    return format;
}

static void opcode_vex_encoding(inst_format_t *format, uint8_t *data, uint8_t *count) {
    vex_prefix_t *v = format->vex_prefix;
    if ((v->vex_legacy_byte == 0 || v->vex_legacy_byte == VEX_LEGACY_BYTE_0F) && v->x && v->b) {
        uint8_t byte0 = 0xc5;
        uint8_t byte1 = 0;
        if (v->r) {
            byte1 = 1 << 7;
        }

        byte1 += (v->source << 3);
        if (v->l) {
            byte1 += (1 << 2);
        }

        byte1 += v->vex_opcode_extension;
        data[(*count)++] = byte0;
        data[(*count)++] = byte1;
        return;
    }

    // three byte form
    uint8_t byte0 = 0xc4;
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    if (v->r) {
        byte1 = 1 << 7;
    }

    if (v->x) {
        byte1 += (1 << 6);
    }

    if (v->b) {
        byte1 += (1 << 5);
    }

    byte1 += v->vex_legacy_byte;

    if (v->w) {
        byte2 = (1 << 7);
    }

    byte2 += (v->source << 3);
    if (v->l) {
        byte2 += (1 << 2);
    }

    byte2 += v->vex_opcode_extension;
    data[(*count)++] = byte0; // count = 1
    data[(*count)++] = byte1; // count = 2
    data[(*count)++] = byte2; // count = 3
}

static void opcode_rex_encoding(inst_format_t *format, uint8_t *result) {
    *result = 0;
    if (format->rex_prefix->b) {
        *result = 1;
    }

    if (format->rex_prefix->x) {
        *result += 1 << 1;
    }

    if (format->rex_prefix->r) {
        *result += 1 << 2;
    }
    if (format->rex_prefix->w) {
        *result += 1 << 3;
    }

    *result += (1 << 6);
}

static void opcode_modrm_encoding(inst_format_t *format, uint8_t *result) {
    modrm_t *modrm = format->modrm;
    // &7 = &00000111 让其余未归零处理
    *result = modrm->rm & 7;
    *result |= (modrm->reg & 7) << 3;

    int mod = (uint8_t) modrm->mod;
    *result |= (mod << 6);
}

static void opcode_sib_encoding(inst_format_t *format, uint8_t *result) {
    sib_t *sib = format->sib;
    *result = sib->base & 7;
    *result |= (sib->index & 7) << 3;
    *result |= (sib->scale << 6);
}

void opcode_format_encoding(inst_format_t *format, uint8_t *data, uint8_t *count) {
    *count = 0;
    if (format->prefix > 0) {
        data[(*count)++] = format->prefix;
    }

    if (format->vex_prefix != NULL) {
        opcode_vex_encoding(format, data, count);
    }

    if (format->rex_prefix != NULL) {
        opcode_rex_encoding(format, &data[(*count)++]);
    }

    uint8_t j = 0;
    while (format->opcode[j] > 0 && j < 3) {
        data[(*count)++] = format->opcode[j++];
    }

    if (format->modrm != NULL) {
        opcode_modrm_encoding(format, &data[(*count)++]);
    }

    if (format->sib != NULL) {
        opcode_sib_encoding(format, &data[(*count)++]);
    }

    for (int i = 0; i < format->disp_count; ++i) {
        data[(*count)++] = format->disps[i];
    }

    for (int i = 0; i < format->imm_count; ++i) {
        data[(*count)++] = format->imms[i];
    }
}

uint8_t *opcode_encoding(asm_inst_t asm_inst, uint8_t *count) {
    *count = 0;
    uint8_t *data = malloc(sizeof(uint8_t) * 30);

    inst_t *inst = opcode_select(asm_inst);
    inst_format_t *format = opcode_fill(inst, asm_inst);
    opcode_format_encoding(format, data, count);
    void *_ = realloc(data, *count);
    return data;
}



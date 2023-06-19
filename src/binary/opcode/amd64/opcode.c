#include "opcode.h"
#include "string.h"
#include "utils/error.h"
#include "utils/helper.h"
#include "src/register/amd64.h"
#include <assert.h>

inst_t movsq = {"movsq", "movsq", 0, {0xA5}, {OPCODE_EXT_REX_W},
                {}
};

inst_t call_rm64 = {"call", "call", 0, {0xFF}, {OPCODE_EXT_SLASH2,},
                    {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};

inst_t call_rel32 = {"call", "call", 0, {0xE8}, {OPCODE_EXT_IMM_DWORD},
                     {
                             OPERAND_TYPE_REL32, ENCODING_TYPE_IMM
                     }
};

inst_t jmp_rel8 = {"jmp", "jmp", 0, {0xEB}, {OPCODE_EXT_IMM_BYTE}, {
        OPERAND_TYPE_REL8, ENCODING_TYPE_IMM
}
};

inst_t jmp_rel32 = {"jmp", "jmp", 0, {0xE9}, {OPCODE_EXT_IMM_DWORD}, {
        OPERAND_TYPE_REL32, ENCODING_TYPE_IMM
}
};

inst_t je_rel8 = {"je", "je", 0, {0x74}, {OPCODE_EXT_IMM_BYTE}, {
        OPERAND_TYPE_REL8, ENCODING_TYPE_IMM
}
};

inst_t je_rel32 = {"je", "je", 0, {0x0F, 0x84}, {OPCODE_EXT_IMM_DWORD},
                   {OPERAND_TYPE_REL32, ENCODING_TYPE_IMM}
};

inst_t idiv_rm8 = {"idiv", "idiv", 0, {0xF6}, {OPCODE_EXT_SLASH7},
                   {
                           {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                   }
};
inst_t idiv_rex_rm8 = {"idiv", "idiv", 0, {0xF6}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH7},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                       }
};
inst_t idiv_rm16 = {"idiv", "idiv", 0x66, {0xF7}, {OPCODE_EXT_SLASH7},
                    {
                            {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                    }
};
inst_t idiv_rm32 = {"idiv", "idiv", 0, {0xF7}, {OPCODE_EXT_SLASH7},
                    {
                            {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                    }
};
inst_t idiv_rm64 = {"idiv", "idiv", 0, {0xF7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH7},
                    {
                            {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                    }
};


inst_t imul_rm8 = {"imul", "imul", 0, {0xF6}, {OPCODE_EXT_SLASH5},
                   {
                           {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                   }
};
inst_t imul_rm16 = {"imul", "imul", 0x66, {0xF7}, {OPCODE_EXT_SLASH5},
                    {
                            {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                    }
};
inst_t imul_rm32 = {"imul", "imul", 0, {0xF7}, {OPCODE_EXT_SLASH5},
                    {
                            {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                    }
};
inst_t imul_rm64 = {"imul", "imul", 0, {0xF7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH5},
                    {
                            {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                    }
};

// add------------------------------------------------------------------------------------------------------
inst_t add_rm8_imm8 = {"add", "add", 0, {0x80}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_BYTE},
                       {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}}
};
inst_t add_rex_rm8_imm8 = {"add", "add", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_BYTE},
                           {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}}
};
inst_t add_rm16_imm16 = {"add", "add", 0x66, {0x81}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_WORD},
                         {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}}
};
inst_t add_rm32_imm32 = {"add", "add", 0, {0x81}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t add_rm64_imm32 = {"add", "add", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t add_rm8_r8 = {"add", "add", 0, {0x00}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t add_rex_rm8_r8 = {"add", "add", 0, {0x00}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t add_rm16_r16 = {"add", "add", 0x66, {0x01}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}}
};
inst_t add_rm32_r32 = {"add", "add", 0, {0x01}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}}
};
inst_t add_rm64_r64 = {"add", "add", 0, {0x01}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}}
};
inst_t add_r8_rm8 = {"add", "add", 0, {0x02}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t add_rex_r8_rm8 = {"add", "add", 0, {0x02}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t add_r16_rm16 = {"add", "add", 0x66, {0x03}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}}
};
inst_t add_r32_rm32 = {"add", "add", 0, {0x03}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}}
};
inst_t add_r64_rm64 = {"add", "add", 0, {0x03}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};

// sub ------------------------------------------------------------------------------------------------------
inst_t sub_rm8_imm8 = {"sub", "sub", 0, {0x80}, {OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_BYTE},
                       {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}}
};
inst_t sub_rex_rm8_imm8 = {"sub", "sub", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_BYTE},
                           {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}}
};
inst_t sub_rm16_imm16 = {"sub", "sub", 0x66, {0x81}, {OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_WORD},
                         {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}}
};
inst_t sub_rm32_imm32 = {"sub", "sub", 0, {0x81}, {OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t sub_rm64_imm32 = {"sub", "sub", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH5, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t sub_rm8_r8 = {"sub", "sub", 0, {0x28}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t sub_rex_rm8_r8 = {"sub", "sub", 0, {0x28}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t sub_rm16_r16 = {"sub", "sub", 0x66, {0x29}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}}
};
inst_t sub_rm32_r32 = {"sub", "sub", 0, {0x29}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}}
};
inst_t sub_rm64_r64 = {"sub", "sub", 0, {0x29}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}}
};
inst_t sub_r8_rm8 = {"sub", "sub", 0, {0x2A}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t sub_rex_r8_rm8 = {"sub", "sub", 0, {0x2A}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t sub_r16_rm16 = {"sub", "sub", 0x66, {0x2B}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}}
};
inst_t sub_r32_rm32 = {"sub", "sub", 0, {0x2B}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}}
};
inst_t sub_r64_rm64 = {"sub", "sub", 0, {0x2B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};


// mov reg -> rm ------------------------------------------------------------------------------------------------------
inst_t mov_rm8_r8 = {"mov", "mov", 0, {0x88}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                     }
};
inst_t mov_rex_rm8_r8 = {"mov", "mov", 0, {0x88}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                         }
};
inst_t mov_rm16_r16 = {"mov", "mov", 0x66, {0x89}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t mov_rm32_r32 = {"mov", "mov", 0, {0x89}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t mov_rm64_r64 = {"mov", "mov", 0, {0x89}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};

// mov rm -> reg ------------------------------------------------------------------------------------------------------
inst_t mov_r8_rm8 = {"mov", "mov", 0, {0x8A}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                     }
};
inst_t mov_rex_r8_rm8 = {"mov", "mov", 0, {0x8A}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                         }
};
inst_t mov_r16_rm16 = {"mov", "mov", 0x66, {0x8B}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t mov_r32_rm32 = {"mov", "mov", 0, {0x8B}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t mov_r64_rm64 = {"mov", "mov", 0, {0x8B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};

// mov imm -> reg ------------------------------------------------------------------------------------------------------
inst_t mov_r8_imm8 = {"mov", "mov", 0, {0xB0}, {OPCODE_EXT_IMM_BYTE},
                      {
                              {OPERAND_TYPE_R8, ENCODING_TYPE_OPCODE_PLUS},
                              {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                      }
};
inst_t mov_rex_r8_imm8 = {"mov", "mov", 0, {0xB0}, {OPCODE_EXT_REX, OPCODE_EXT_IMM_BYTE},
                          {
                                  {OPERAND_TYPE_R8, ENCODING_TYPE_OPCODE_PLUS},
                                  {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                          }
};
inst_t mov_r16_imm16 = {"mov", "mov", 0x66, {0xB8}, {OPCODE_EXT_IMM_WORD},
                        {
                                {OPERAND_TYPE_R16, ENCODING_TYPE_OPCODE_PLUS},
                                {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}
                        }
};
inst_t mov_r32_imm32 = {"mov", "mov", 0, {0xB8}, {OPCODE_EXT_IMM_DWORD},
                        {
                                {OPERAND_TYPE_R32, ENCODING_TYPE_OPCODE_PLUS},
                                {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                        }
};

inst_t mov_r64_imm64 = {"mov", "mov", 0, {0xB8}, {OPCODE_EXT_REX_W, OPCODE_EXT_IMM_QWORD},
                        {
                                {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS},
                                {OPERAND_TYPE_IMM64, ENCODING_TYPE_IMM}
                        }
};


// mov imm -> rm ------------------------------------------------------------------------------------------------------
inst_t mov_rm8_imm8 = {"mov", "mov", 0, {0xC6}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }
};
inst_t mov_rex_rm8_imm8 = {"mov", "mov", 0, {0xC6}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_BYTE},
                           {
                                   {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                   {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                           }
};

inst_t mov_rm16_imm16 = {"mov", "mov", 0x66, {0xC7}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_WORD},
                         {
                                 {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}
                         }
};
inst_t mov_rm32_imm32 = {"mov", "mov", 0, {0xC7}, {OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};
inst_t mov_rm64_imm32 = {"mov", "mov", 0, {0xC7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH0, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};

inst_t lea_r64_m = {"lea", "lea", 0, {0x8D}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                    {
                            {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                            {OPERAND_TYPE_M, ENCODING_TYPE_MODRM_RM},
                    }
};

inst_t syscall_inst = {"syscall_inst", "syscall_inst", 0, {0x0F, 0x05}, {}, {}};

// TODO 什么时候使用 near 什么时候使用 far?
inst_t ret = {"ret", "ret", 0, {0xC3}, {}, {}};

inst_t push_r64 = {"push", "push", 0, {0x50}, {},
                   {
                           {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS}
                   }
};

inst_t push_rm64 = {"push", "push", 0, {0xFF}, {OPCODE_EXT_SLASH6},
                    {
                            {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                    }
};

inst_t pop_r64 = {"pop", "pop", 0, {0x58}, {},
                  {
                          {OPERAND_TYPE_R64, ENCODING_TYPE_OPCODE_PLUS}
                  }
};

inst_t pop_rm64 = {"pop", "pop", 0, {0x8F}, {OPCODE_EXT_SLASH0},
                   {
                           {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                   }
};


// cmp ------------------------------------------------------------------------------------------------------
inst_t cmp_rm8_imm8 = {"cmp", "cmp", 0, {0x80}, {OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }
};
inst_t cmp_rex_rm8_imm8 = {"cmp", "cmp", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_BYTE},
                           {
                                   {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                   {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                           }
};
inst_t cmp_rm16_imm16 = {"cmp", "cmp", 0x66, {0x81}, {OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_WORD},
                         {
                                 {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}
                         }
};
inst_t cmp_rm32_imm32 = {"cmp", "cmp", 0, {0x81}, {OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};
inst_t cmp_rm64_imm32 = {"cmp", "cmp", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH7, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};
inst_t cmp_rm8_r8 = {"cmp", "cmp", 0, {0x38}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                     }
};
inst_t cmp_rex_rm8_r8 = {"cmp", "cmp", 0, {0x38}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                         }
};
inst_t cmp_rm16_r16 = {"cmp", "cmp", 0x66, {0x39}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t cmp_rm32_r32 = {"cmp", "cmp", 0, {0x39}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t cmp_rm64_r64 = {"cmp", "cmp", 0, {0x39}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};

inst_t cmp_r8_rm8 = {"cmp", "cmp", 0, {0x3A}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                     }
};
inst_t cmp_rex_r8_rm8 = {"cmp", "cmp", 0, {0x3A}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                         }
};
inst_t cmp_r16_rm16 = {"cmp", "cmp", 0x66, {0x3B}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t cmp_r32_rm32 = {"cmp", "cmp", 0, {0x3B}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t cmp_r64_rm64 = {"cmp", "cmp", 0, {0x3B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};


// setcc ------------------------------------------------------------------------------------------------------
inst_t seta_rm8 = {"seta", "seta", 0, {0x0F, 0x97}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setae_rm8 = {"setae", "setae", 0, {0x0F, 0x93}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setb_rm8 = {"setb", "setb", 0, {0x0F, 0x92}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setbe_rm8 = {"setbe", "setbe", 0, {0x0F, 0x96}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setg_rm8 = {"setg", "setg", 0, {0x0F, 0x9F}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setge_rm8 = {"setge", "setge", 0, {0x0F, 0x9D}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setl_rm8 = {"setl", "setl", 0, {0x0F, 0x9C}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setle_rm8 = {"setle", "setle", 0, {0x0F, 0x9E}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t sete_rm8 = {"sete", "sete", 0, {0x0F, 0x94}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setne_rm8 = {"setne", "setne", 0, {0x0F, 0x95}, {}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
// set rex ------------------------------------------------------------------------------------------------------
inst_t seta_rex_rm8 = {"seta", "seta", 0, {0x0F, 0x97}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setae_rex_rm8 = {"setae", "setae", 0, {0x0F, 0x93}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setb_rex_rm8 = {"setb", "setb", 0, {0x0F, 0x92}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setbe_rex_rm8 = {"setbe", "setbe", 0, {0x0F, 0x96}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setg_rex_rm8 = {"setg", "setg", 0, {0x0F, 0x9F}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setge_rex_rm8 = {"setge", "setge", 0, {0x0F, 0x9D}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setl_rex_rm8 = {"setl", "setl", 0, {0x0F, 0x9C}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setle_rex_rm8 = {"setle", "setle", 0, {0x0F, 0x9E}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t sete_rex_rm8 = {"sete", "sete", 0, {0x0F, 0x94}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t setne_rex_rm8 = {"setne", "setne", 0, {0x0F, 0x95}, {OPCODE_EXT_REX}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};


// neg ------------------------------------------------------------------------------------------------------
inst_t neg_rm8 = {"neg", "neg", 0, {0xF6}, {OPCODE_EXT_SLASH3}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t neg_rex_rm8 = {"neg", "neg", 0, {0xF6}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH3}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t neg_rm16 = {"neg", "neg", 0x66, {0xF7}, {OPCODE_EXT_SLASH3}, {
        OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
};
inst_t neg_rm32 = {"neg", "neg", 0, {0xF7}, {OPCODE_EXT_SLASH3}, {
        OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
};
inst_t neg_rm64 = {"neg", "neg", 0, {0xF7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH3}, {
        OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
};

// not ------------------------------------------------------------------------------------------------------
inst_t not_rm8 = {"not", "not", 0, {0xF6}, {OPCODE_EXT_SLASH2}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t not_rex_rm8 = {"not", "not", 0, {0xF6}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH2}, {
        OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
};
inst_t not_rm16 = {"not", "not", 0x66, {0xF7}, {OPCODE_EXT_SLASH2}, {
        OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
};
inst_t not_rm32 = {"not", "not", 0, {0xF7}, {OPCODE_EXT_SLASH2}, {
        OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
};
inst_t not_rm64 = {"not", "not", 0, {0xF7}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH2},
                   {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};

// xor ------------------------------------------------------------------------------------------------------
inst_t xor_rm8_imm8 = {"xor", "xor", 0, {0x80}, {OPCODE_EXT_SLASH6, OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }};
inst_t xor_rex_rm8_imm8 = {"xor", "xor", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH6, OPCODE_EXT_IMM_BYTE},
                           {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}}
};
inst_t xor_rm16_imm16 = {"xor", "xor", 0x66, {0x81}, {OPCODE_EXT_SLASH6, OPCODE_EXT_IMM_WORD},
                         {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}}
};
inst_t xor_rm32_imm32 = {"xor", "xor", 0, {0x81}, {OPCODE_EXT_SLASH6, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t xor_rm64_imm32 = {"xor", "xor", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH6, OPCODE_EXT_IMM_DWORD},
                         {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}}
};
inst_t xor_rm8_r8 = {"xor", "xor", 0, {0x30}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t xor_rex_rm8_r8 = {"xor", "xor", 0, {0x30}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}}
};
inst_t xor_rm16_r16 = {"xor", "xor", 0x66, {0x31}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}}
};
inst_t xor_rm32_r32 = {"xor", "xor", 0, {0x31}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}}
};
inst_t xor_rm64_r64 = {"xor", "xor", 0, {0x31}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}, {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}}
};
inst_t xor_r8_rm8 = {"xor", "xor", 0, {0x32}, {OPCODE_EXT_SLASHR},
                     {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t xor_rex_r8_rm8 = {"xor", "xor", 0, {0x32}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {{OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}}
};
inst_t xor_r16_rm16 = {"xor", "xor", 0x66, {0x33}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}}
};
inst_t xor_r32_rm32 = {"xor", "xor", 0, {0x33}, {OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}}
};
inst_t xor_r64_rm64 = {"xor", "xor", 0, {0x33}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {{OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}, {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}}
};

// or ------------------------------------------------------------------------------------------------------
inst_t or_rm8_imm8 = {"or", "or", 0, {0x80}, {OPCODE_EXT_SLASH1, OPCODE_EXT_IMM_BYTE},
                      {
                              {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                      }
};
inst_t or_rex_rm8_imm8 = {"or", "or", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH1, OPCODE_EXT_IMM_BYTE},
                          {
                                  {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                  {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                          }
};
inst_t or_rm16_imm16 = {"or", "or", 0x66, {0x81}, {OPCODE_EXT_SLASH1, OPCODE_EXT_IMM_WORD},
                        {
                                {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}
                        }
};
inst_t or_rm32_imm32 = {"or", "or", 0, {0x81}, {OPCODE_EXT_SLASH1, OPCODE_EXT_IMM_DWORD},
                        {
                                {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                        }
};
inst_t or_rm64_imm32 = {"or", "or", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH1, OPCODE_EXT_IMM_DWORD},
                        {
                                {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                        }
};
inst_t or_rm8_r8 = {"or", "or", 0, {0x08}, {OPCODE_EXT_SLASHR},
                    {
                            {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                            {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                    }
};
inst_t or_rex_rm8_r8 = {"or", "or", 0, {0x08}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                        {
                                {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                        }
};
inst_t or_rm16_r16 = {"or", "or", 0x66, {0x09}, {OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t or_rm32_r32 = {"or", "or", 0, {0x09}, {OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t or_rm64_r64 = {"or", "or", 0, {0x09}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t or_r8_rm8 = {"or", "or", 0, {0x0A}, {OPCODE_EXT_SLASHR},
                    {
                            {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                            {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                    }
};
inst_t or_rex_r8_rm8 = {"or", "or", 0, {0x0A}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                        {
                                {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                                {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                        }
};
inst_t or_r16_rm16 = {"or", "or", 0x66, {0x0B}, {OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                              {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                      }
};
inst_t or_r32_rm32 = {"or", "or", 0, {0x0B}, {OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG},
                              {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
                      }
};
inst_t or_r64_rm64 = {"or", "or", 0, {0x0B}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                      {
                              {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                              {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                      }
};

// and ------------------------------------------------------------------------------------------------------
inst_t and_rm8_imm8 = {"and", "and", 0, {0x80}, {OPCODE_EXT_SLASH4, OPCODE_EXT_IMM_BYTE},
                       {
                               {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                       }
};
inst_t and_rex_rm8_imm8 = {"and", "and", 0, {0x80}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH4, OPCODE_EXT_IMM_BYTE},
                           {
                                   {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                   {OPERAND_TYPE_IMM8, ENCODING_TYPE_IMM}
                           }
};
inst_t and_rm16_imm16 = {"and", "and", 0x66, {0x81}, {OPCODE_EXT_SLASH4, OPCODE_EXT_IMM_WORD},
                         {
                                 {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM16, ENCODING_TYPE_IMM}
                         }
};
inst_t and_rm32_imm32 = {"and", "and", 0, {0x81}, {OPCODE_EXT_SLASH4, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};
inst_t and_rm64_imm32 = {"and", "and", 0, {0x81}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH4, OPCODE_EXT_IMM_DWORD},
                         {
                                 {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_IMM32, ENCODING_TYPE_IMM}
                         }
};
inst_t and_rm8_r8 = {"and", "and", 0, {0x20}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                     }
};
inst_t and_rex_rm8_r8 = {"and", "and", 0, {0x20}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                         }
};
inst_t and_rm16_r16 = {"and", "and", 0x66, {0x21}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t and_rm32_r32 = {"and", "and", 0, {0x21}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t and_rm64_r64 = {"and", "and", 0, {0x21}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG}
                       }
};
inst_t and_r8_rm8 = {"and", "and", 0, {0x22}, {OPCODE_EXT_SLASHR},
                     {
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                     }
};
inst_t and_rex_r8_rm8 = {"and", "and", 0, {0x22}, {OPCODE_EXT_REX, OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM}
                         }
};
inst_t and_r16_rm16 = {"and", "and", 0x66, {0x23}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t and_r32_rm32 = {"and", "and", 0, {0x23}, {OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R32, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM}
                       }
};
inst_t and_r64_rm64 = {"and", "and", 0, {0x23}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASHR},
                       {
                               {OPERAND_TYPE_R64, ENCODING_TYPE_MODRM_REG},
                               {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM}
                       }
};

// shift ------------------------------------------------------------------------------------------------------
inst_t sal_rm8_cl = {"sal", "sal", 0, {0xD2}, {OPCODE_EXT_SLASH4},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                     }
};
inst_t sal_rex_rm8_cl = {"sal", "sal", 0, {0xD2}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH4},
                         {
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                         }
};
inst_t sal_rm16_cl = {"sal", "sal", 0x66, {0xD3}, {OPCODE_EXT_SLASH4},
                      {
                              {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t sal_rm32_cl = {"sal", "sal", 0, {0xD3}, {OPCODE_EXT_SLASH4},
                      {
                              {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t sal_rm64_cl = {"sal", "sal", 0, {0xD3}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH4},
                      {
                              {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};

inst_t sar_rm8_cl = {"sar", "sar", 0, {0xD2}, {OPCODE_EXT_SLASH7},
                     {
                             {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                             {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                     }
};
inst_t sar_rex_rm8_cl = {"sar", "sar", 0, {0xD2}, {OPCODE_EXT_REX, OPCODE_EXT_SLASH7},
                         {
                                 {OPERAND_TYPE_RM8, ENCODING_TYPE_MODRM_RM},
                                 {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                         }
};
inst_t sar_rm16_cl = {"sar", "sar", 0x66, {0xD3}, {OPCODE_EXT_SLASH7},
                      {
                              {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t sar_rm32_cl = {"sar", "sar", 0, {0xD3}, {OPCODE_EXT_SLASH7},
                      {
                              {OPERAND_TYPE_RM32, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};
inst_t sar_rm64_cl = {"sar", "sar", 0, {0xD3}, {OPCODE_EXT_REX_W, OPCODE_EXT_SLASH7},
                      {
                              {OPERAND_TYPE_RM64, ENCODING_TYPE_MODRM_RM},
                              {OPERAND_TYPE_R8, ENCODING_TYPE_MODRM_REG}
                      }
};

// float ------------------------------------------------------------------------------------------------------
// float xor ------------------------------------------------------------------------------------------------------
inst_t xorpd_xmm1_xmm2m128 = {"xor", "xorpd", 0, {0x66, 0x0F, 0x57}, {OPCODE_EXT_SLASHR},
                              {
                                      {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                      {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                              }
};
inst_t xorps_xmm1_xmm2m128 = {"xor", "xorps", 0, {0x0F, 0x57}, {OPCODE_EXT_SLASHR},
                              {
                                      {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                      {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                              }
};


// float mov ------------------------------------------------------------------------------------------------------
inst_t movsd_xmm1_xmm2 = {"mov", "movsd", 0, {0xF2, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                          {
                                  {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                  {OPERAND_TYPE_XMM2S64, ENCODING_TYPE_MODRM_RM}
                          }
};
inst_t movsd_xmm1_m64 = {"mov", "movsd", 0, {0xF2, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_M64, ENCODING_TYPE_MODRM_RM}
                         }
};
inst_t movsd_xmm1m64_xmm2 = {"mov", "movsd", 0, {0x0F2, 0x0F, 0x11}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1M64, ENCODING_TYPE_MODRM_RM},
                                     {OPERAND_TYPE_XMM2S64, ENCODING_TYPE_MODRM_REG}
                             }
};
inst_t movss_xmm1_xmm2 = {"mov", "movss", 0, {0xF3, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                          {
                                  {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                  {OPERAND_TYPE_XMM2S32, ENCODING_TYPE_MODRM_RM}
                          }
};
inst_t movss_xmm1_m32 = {"mov", "movss", 0, {0xF3, 0x0F, 0x10}, {OPCODE_EXT_SLASHR},
                         {
                                 {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                 {OPERAND_TYPE_M32, ENCODING_TYPE_MODRM_RM}}
};
inst_t movss_xmm2m32_xmm1 = {"mov", "movss", 0, {0x0F3, 0x0F, 0x11}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM},
                                     {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG}}
};

// float 算数运算 ------------------------------------------------------------------------------------------------------
inst_t addsd_xmm1_xmm2m64 = {"add", "addsd", 0, {0xF2, 0x0F, 0x58}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t addss_xmm1_xmm2m32 = {"add", "addss", 0, {0xF3, 0x0F, 0x58}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t subsd_xmm1_xmm2m64 = {"sub", "subsd", 0, {0xF2, 0x0F, 0x5C}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t subss_xmm1_xmm2m32 = {"sub", "subss", 0, {0xF3, 0x0F, 0x5C}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t mulsd_xmm1_xmm2m64 = {"mul", "mulsd", 0, {0xF2, 0x0F, 0x59}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t mulss_xmm1_xmm2m32 = {"mul", "mulss", 0, {0xF3, 0x0F, 0x59}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t divsd_xmm1_xmm2m64 = {"div", "divsd", 0, {0xF2, 0x0F, 0x5E}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t divss_xmm1_xmm2m32 = {"div", "divss", 0, {0xF2, 0x0F, 0x5E}, {OPCODE_EXT_SLASHR},
                             {
                                     {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                                     {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                             }
};
inst_t comisd = {"cmp", "comisd", 0, {0x66, 0x0F, 0x2F}, {OPCODE_EXT_SLASHR},
                 {
                         {OPERAND_TYPE_XMM1S64, ENCODING_TYPE_MODRM_REG},
                         {OPERAND_TYPE_XMM2M64, ENCODING_TYPE_MODRM_RM}
                 }
};
inst_t comiss = {"cmp", "comiss", 0, {0x66, 0x0F, 0x2F}, {OPCODE_EXT_SLASHR},
                 {
                         {OPERAND_TYPE_XMM1S32, ENCODING_TYPE_MODRM_REG},
                         {OPERAND_TYPE_XMM2M32, ENCODING_TYPE_MODRM_RM}
                 }
};



// opcode end ------------------------------------------------------------------------------------------------------

static amd64_opcode_tree_node_t *opcode_node_new() {
    amd64_opcode_tree_node_t *node = NEW(amd64_opcode_tree_node_t);
    node->key = "";
    node->insts = (amd64_insts_t) {
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
void amd64_opcode_init() {
    opcode_tree_root = opcode_node_new();
    opcode_tree_root->key = "root";
    // 收集所有指令，进行注册
    opcode_tree_build(&lea_r64_m);
    opcode_tree_build(&syscall_inst);
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

    // 整形算数运算
    opcode_tree_build(&add_rm8_imm8);
    opcode_tree_build(&add_rex_rm8_imm8);
    opcode_tree_build(&add_rm16_imm16);
    opcode_tree_build(&add_rm32_imm32);
    opcode_tree_build(&add_rm64_imm32);
    opcode_tree_build(&add_rm8_r8);
    opcode_tree_build(&add_rex_rm8_r8);
    opcode_tree_build(&add_rm16_r16);
    opcode_tree_build(&add_rm32_r32);
    opcode_tree_build(&add_rm64_r64);
    opcode_tree_build(&add_r8_rm8);
    opcode_tree_build(&add_rex_r8_rm8);
    opcode_tree_build(&add_r16_rm16);
    opcode_tree_build(&add_r32_rm32);
    opcode_tree_build(&add_r64_rm64);

    opcode_tree_build(&sub_rm8_imm8);
    opcode_tree_build(&sub_rex_rm8_imm8);
    opcode_tree_build(&sub_rm16_imm16);
    opcode_tree_build(&sub_rm32_imm32);
    opcode_tree_build(&sub_rm64_imm32);
    opcode_tree_build(&sub_rm8_r8);
    opcode_tree_build(&sub_rex_rm8_r8);
    opcode_tree_build(&sub_rm16_r16);
    opcode_tree_build(&sub_rm32_r32);
    opcode_tree_build(&sub_rm64_r64);
    opcode_tree_build(&sub_r8_rm8);
    opcode_tree_build(&sub_rex_r8_rm8);
    opcode_tree_build(&sub_r16_rm16);
    opcode_tree_build(&sub_r32_rm32);
    opcode_tree_build(&sub_r64_rm64);

    opcode_tree_build(&idiv_rm8);
    opcode_tree_build(&idiv_rex_rm8);
    opcode_tree_build(&idiv_rm16);
    opcode_tree_build(&idiv_rm32);
    opcode_tree_build(&idiv_rm64);
    opcode_tree_build(&imul_rm8);
    opcode_tree_build(&imul_rm16);
    opcode_tree_build(&imul_rm32);
    opcode_tree_build(&imul_rm64);


    // mov reg -> rm
    opcode_tree_build(&mov_rm8_r8);
    opcode_tree_build(&mov_rex_rm8_r8);
    opcode_tree_build(&mov_rm16_r16);
    opcode_tree_build(&mov_rm32_r32);
    opcode_tree_build(&mov_rm64_r64);

    // mov rm -> reg
    opcode_tree_build(&mov_r8_rm8);
    opcode_tree_build(&mov_rex_r8_rm8);
    opcode_tree_build(&mov_r16_rm16);
    opcode_tree_build(&mov_r64_rm64);
    opcode_tree_build(&mov_r32_rm32);

    // mov imm ->reg
    opcode_tree_build(&mov_rex_r8_imm8);
    opcode_tree_build(&mov_r8_imm8);
    opcode_tree_build(&mov_r16_imm16);
    opcode_tree_build(&mov_r32_imm32);
    opcode_tree_build(&mov_r64_imm64);

    // mov imm -> rm
    opcode_tree_build(&mov_rex_rm8_imm8);
    opcode_tree_build(&mov_rm8_imm8);
    opcode_tree_build(&mov_rm16_imm16);
    opcode_tree_build(&mov_rm32_imm32);
    opcode_tree_build(&mov_rm64_imm32);

    opcode_tree_build(&cmp_rm8_imm8);
    opcode_tree_build(&cmp_rex_rm8_imm8);
    opcode_tree_build(&cmp_rm16_imm16);
    opcode_tree_build(&cmp_rm32_imm32);
    opcode_tree_build(&cmp_rm64_imm32);
    opcode_tree_build(&cmp_rm8_r8);
    opcode_tree_build(&cmp_rex_rm8_r8);
    opcode_tree_build(&cmp_rm16_r16);
    opcode_tree_build(&cmp_rm32_r32);
    opcode_tree_build(&cmp_r64_rm64);
    opcode_tree_build(&cmp_r8_rm8);
    opcode_tree_build(&cmp_rex_r8_rm8);
    opcode_tree_build(&cmp_r16_rm16);
    opcode_tree_build(&cmp_r32_rm32);
    opcode_tree_build(&cmp_r64_rm64);

    opcode_tree_build(&seta_rm8);
    opcode_tree_build(&setae_rm8);
    opcode_tree_build(&setb_rm8);
    opcode_tree_build(&setbe_rm8);
    opcode_tree_build(&setg_rm8);
    opcode_tree_build(&setge_rm8);
    opcode_tree_build(&setl_rm8);
    opcode_tree_build(&setle_rm8);
    opcode_tree_build(&sete_rm8);
    opcode_tree_build(&setne_rm8);
    opcode_tree_build(&seta_rex_rm8);
    opcode_tree_build(&setae_rex_rm8);
    opcode_tree_build(&setb_rex_rm8);
    opcode_tree_build(&setbe_rex_rm8);
    opcode_tree_build(&setg_rex_rm8);
    opcode_tree_build(&setge_rex_rm8);
    opcode_tree_build(&setl_rex_rm8);
    opcode_tree_build(&setle_rex_rm8);
    opcode_tree_build(&sete_rex_rm8);
    opcode_tree_build(&setne_rex_rm8);

    opcode_tree_build(&neg_rm8);
    opcode_tree_build(&neg_rex_rm8);
    opcode_tree_build(&neg_rm16);
    opcode_tree_build(&neg_rm32);
    opcode_tree_build(&neg_rm64);

    // 位运算
    opcode_tree_build(&not_rm8);
    opcode_tree_build(&not_rex_rm8);
    opcode_tree_build(&not_rm16);
    opcode_tree_build(&not_rm32);
    opcode_tree_build(&not_rm64);

    opcode_tree_build(&xor_rm8_imm8);
    opcode_tree_build(&xor_rex_rm8_imm8);
    opcode_tree_build(&xor_rm16_imm16);
    opcode_tree_build(&xor_rm32_imm32);
    opcode_tree_build(&xor_rm64_imm32);
    opcode_tree_build(&xor_rm8_r8);
    opcode_tree_build(&xor_rex_rm8_r8);
    opcode_tree_build(&xor_rm16_r16);
    opcode_tree_build(&xor_rm32_r32);
    opcode_tree_build(&xor_rm64_r64);
    opcode_tree_build(&xor_r8_rm8);
    opcode_tree_build(&xor_rex_r8_rm8);
    opcode_tree_build(&xor_r16_rm16);
    opcode_tree_build(&xor_r32_rm32);
    opcode_tree_build(&xor_r64_rm64);

    opcode_tree_build(&or_rm8_imm8);
    opcode_tree_build(&or_rex_rm8_imm8);
    opcode_tree_build(&or_rm16_imm16);
    opcode_tree_build(&or_rm32_imm32);
    opcode_tree_build(&or_rm64_imm32);
    opcode_tree_build(&or_rm8_r8);
    opcode_tree_build(&or_rex_rm8_r8);
    opcode_tree_build(&or_rm16_r16);
    opcode_tree_build(&or_rm32_r32);
    opcode_tree_build(&or_rm64_r64);
    opcode_tree_build(&or_r8_rm8);
    opcode_tree_build(&or_rex_r8_rm8);
    opcode_tree_build(&or_r16_rm16);
    opcode_tree_build(&or_r32_rm32);
    opcode_tree_build(&or_r64_rm64);

    opcode_tree_build(&and_rm8_imm8);
    opcode_tree_build(&and_rex_rm8_imm8);
    opcode_tree_build(&and_rm16_imm16);
    opcode_tree_build(&and_rm32_imm32);
    opcode_tree_build(&and_rm64_imm32);
    opcode_tree_build(&and_rm8_r8);
    opcode_tree_build(&and_rex_rm8_r8);
    opcode_tree_build(&and_rm16_r16);
    opcode_tree_build(&and_rm32_r32);
    opcode_tree_build(&and_rm64_r64);
    opcode_tree_build(&and_r8_rm8);
    opcode_tree_build(&and_rex_r8_rm8);
    opcode_tree_build(&and_r16_rm16);
    opcode_tree_build(&and_r32_rm32);
    opcode_tree_build(&and_r64_rm64);

    opcode_tree_build(&sal_rm8_cl);
    opcode_tree_build(&sal_rex_rm8_cl);
    opcode_tree_build(&sal_rm16_cl);
    opcode_tree_build(&sal_rm32_cl);
    opcode_tree_build(&sal_rm64_cl);
    opcode_tree_build(&sar_rm8_cl);
    opcode_tree_build(&sar_rex_rm8_cl);
    opcode_tree_build(&sar_rm16_cl);
    opcode_tree_build(&sar_rm32_cl);
    opcode_tree_build(&sar_rm64_cl);

    // 浮点数算数运算
    opcode_tree_build(&movsd_xmm1_m64); // 内存到 xmm
    opcode_tree_build(&movsd_xmm1_xmm2); // 内存到 xmm
    opcode_tree_build(&movsd_xmm1m64_xmm2); // xmm 到内存或者xmm

    opcode_tree_build(&movss_xmm1_xmm2); // 内存到 xmm
    opcode_tree_build(&movss_xmm1_m32); // 内存到 xmm
    opcode_tree_build(&movss_xmm2m32_xmm1); // xmm 到内存或者xmm
    opcode_tree_build(&xorps_xmm1_xmm2m128);
    opcode_tree_build(&xorpd_xmm1_xmm2m128);

    // 浮点算数运算
    opcode_tree_build(&addss_xmm1_xmm2m32);
    opcode_tree_build(&addsd_xmm1_xmm2m64);
    opcode_tree_build(&subss_xmm1_xmm2m32);
    opcode_tree_build(&subsd_xmm1_xmm2m64);
    opcode_tree_build(&mulss_xmm1_xmm2m32);
    opcode_tree_build(&mulsd_xmm1_xmm2m64);
    opcode_tree_build(&divss_xmm1_xmm2m32);
    opcode_tree_build(&divsd_xmm1_xmm2m64);
    opcode_tree_build(&comiss);
    opcode_tree_build(&comisd);

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
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT8, BYTE);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_REL16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT16, WORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_REL32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT32, DWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM8) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, BYTE);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, BYTE);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, BYTE);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, BYTE);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, BYTE);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM16) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, WORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, WORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, WORD);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, WORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, WORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM32) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, DWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, DWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, DWORD);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, DWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, DWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_RM64) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, QWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, QWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, QWORD);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M) {
        res.count = 3;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, QWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, QWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, WORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M32) {
        res.count = 3;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, DWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, DWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, DWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_M64) {
        res.count = 3;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, QWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, QWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
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
        res.count = 2;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT32, DWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_UINT, QWORD);
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
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, BYTE);
        res.list = highs;
        return res;
    }
//    if (t == OPERAND_TYPE_AL) {
//        res.count = 1;
//        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
//        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, BYTE);
//        res.list = highs;
//        return res;
//    }


    if (t == OPERAND_TYPE_R16) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, 2);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, 4);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_R64) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REG, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_XMM1S64 || t == OPERAND_TYPE_XMM2S64) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, QWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_XMM1S32 || t == OPERAND_TYPE_XMM2S32) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, DWORD);
        res.list = highs;
        return res;
    }

    // 暂时没有支持 M128 的形式，所以必须使用 xmm2 部分
    if (t == OPERAND_TYPE_XMM2M128) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, OWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_XMM1M64 || t == OPERAND_TYPE_XMM2M64) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, QWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, QWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, QWORD);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, QWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, QWORD);
        res.list = highs;
        return res;
    }
    if (t == OPERAND_TYPE_XMM2M32 || t == OPERAND_TYPE_XMM1M32) {
        res.count = 5;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, DWORD);
        highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REG, DWORD);
        highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, DWORD);
        highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REG, DWORD);
        highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REG, DWORD);
        res.list = highs;
        return res;
    }

    if (t == OPERAND_TYPE_YMM1 || t == OPERAND_TYPE_YMM2) {
        res.count = 1;
        uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
        highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_FREG, YWORD);
        res.list = highs;
        return res;
    }

    error_exit("cannot identify operand_type index: %d", t);
    return res;
}

void opcode_tree_build(inst_t *inst) {
    // 第一层结构 指令名称
    amd64_opcode_tree_node_t *node = opcode_find_name(inst->group);

    // 其余层级结构,指令参数
    opcode_find_succs(node, inst, 0);
}

/**
 * @param name
 */
amd64_opcode_tree_node_t *opcode_find_name(string name) {
    bool exist = table_exist(opcode_tree_root->succs, name);
    if (exist) {
        return table_get(opcode_tree_root->succs, name);
    }

    amd64_opcode_tree_node_t *node = opcode_node_new();
    node->key = name;

    table_set(opcode_tree_root->succs, name, node);

    return node;
}

/**
 * @param node 树节点
 * @return
 */
void opcode_find_succs(amd64_opcode_tree_node_t *node, inst_t *inst, int operands_index) {
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
        amd64_opcode_tree_node_t *succ = table_get(node->succs, key);
        if (succ == NULL) {
            succ = opcode_node_new();
            succ->key = key;
            table_set(node->succs, key, succ);
        }
        // 继续寻找下一级node
        opcode_find_succs(succ, inst, operands_index + 1);
    }
}

// spl,bpl,sil,dil and reg index >= 8
// must has rex/rexw/vex128/vex256
static bool has_64_reg(reg_t *reg) {
    if (str_equal(reg->name, "spl")) {
        return true;
    }
    if (str_equal(reg->name, "bpl")) {
        return true;
    }
    if (str_equal(reg->name, "sil")) {
        return true;
    }
    if (str_equal(reg->name, "dil")) {
        return true;
    }
    if (reg->index >= 8) {
        return true;
    }
    return false;
}


// ah,ch,dh,bh
// must skip rex/rexw
static bool is_high_eight_reg(reg_t *reg) {
    if (str_equal(reg->name, "ah")) {
        return true;
    }
    if (str_equal(reg->name, "bh")) {
        return true;
    }
    if (str_equal(reg->name, "ch")) {
        return true;
    }
    if (str_equal(reg->name, "dh")) {
        return true;
    }

    return false;
}

static bool has_64_extension(opcode_ext *list) {
    for (int i = 0; i < 4; ++i) {
        if (list[i] == OPCODE_EXT_REX) {
            return true;
        }
        if (list[i] == OPCODE_EXT_REX_W) {
            return true;
        }
        if (list[i] == OPCODE_EXT_VEX_128) {
            return true;
        }
        if (list[i] == OPCODE_EXT_VEX_256) {
            return true;
        }
    }
    return false;
}

static bool has_rex_extension(opcode_ext *list) {
    for (int i = 0; i < 4; ++i) {
        if (list[i] == OPCODE_EXT_REX) {
            return true;
        }
        if (list[i] == OPCODE_EXT_REX_W) {
            return true;
        }
    }
    return false;
}

inst_t *opcode_select(asm_operation_t operation) {
    amd64_opcode_tree_node_t *current = table_get(opcode_tree_root->succs, operation.name);
    assert(current != NULL && dsprintf("cannot identify asm operation %s ", operation.name));

    // 这里仅使用了大小匹配，但是对于 r8 和 rm8 存在一些特殊情况需要处理
    // 比如 ah 寄存器对应的操作码必须包含扩展 rex
    // 比如 ah 和 al 具有同一寄存器位置，只是在 64 位下的不同扩展而已
    bool has64_reg = false;
    bool has_high_eight_reg = false;

    // TODO has ax 并优先选择携带 ax 的寄存器

    for (int i = 0; i < operation.count; ++i) {
        asm_operand_t *operand = operation.operands[i];
        if (operand->type == ASM_OPERAND_TYPE_REG) {
            reg_t *reg = operand->value;
            if (is_high_eight_reg(reg)) {
                has_high_eight_reg = true;
            }
            if (has_64_reg(reg)) {
                has64_reg = true;
            }
        }

        // 生成 key
        string key = itoa(asm_operand_to_key(operand->type, operand->size));

        // current 匹配
        bool exists = table_exist(current->succs, key);
        assertf(exists, "cannot identify asm operation %s with operand %d", operation.name, i);
        current = table_get(current->succs, key);
    }

    amd64_insts_t temps = current->insts;

    amd64_insts_t insts = {
            .count = 0,
            .list= malloc(sizeof(inst_t) * 10)
    };

    /**
     * 上面只进行了 size 和 type 等匹配，现在需要进行一些特殊规则的过滤
     * 1. 如果 asm 中使用了 sh/ch/dh/bh 这四个旧版本寄存器，则 inst 中不能包含 rex/rexw 64位标识(rex 标识会将数据编译到 al/bl/cl/dl 寄存器中)。
     */
    for (int i = 0; i < temps.count; ++i) {
        inst_t *inst = temps.list[i];
        if (has_high_eight_reg && has_rex_extension(inst->extensions)) {
            continue;
        }

        if (has64_reg && !has_64_extension(inst->extensions)) {
            // 找一个空闲到位置， 主动带上 EXT_REX ,暂时不需要这样到安全限制,默认就是优先支持 rex
            for (int j = 0; j < 4; ++j) {
                if (!inst->extensions[j]) {
                    inst->extensions[j] = OPCODE_EXT_REX;
                    break;
                }
            }
        }

        // 和 has ax 协作从而优先使用携带 ax 的寄存器

        insts.list[insts.count++] = inst;
    }
    if (insts.count == 0) {
        assertf(false,
                "[opcode_select] operation %s  not match insts,  has 64: %d, has high eight: %d",
                operation.name,
                has64_reg,
                has_high_eight_reg);
    }

    opcode_sort_insts(&insts);

    return insts.list[0];
}

void opcode_sort_insts(amd64_insts_t *insts) {
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

static void parser_ext(amd64_inst_format_t *format, opcode_ext ext) {
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

static void set_disp(amd64_inst_format_t *format, string reg, uint8_t *disps, uint8_t count) {
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

static void set_imm(amd64_inst_format_t *format, uint8_t *imms, uint8_t count) {
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

static amd64_inst_format_t *inst_format_new(uint8_t *opcode) {
    amd64_inst_format_t *format = NEW(amd64_inst_format_t);
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
amd64_inst_format_t *opcode_fill(inst_t *inst, asm_operation_t asm_inst) {
    amd64_inst_format_t *format = inst_format_new(inst->opcode);
    format->op_id = asm_inst.op_id;

    if (asm_inst.prefix > 0) {
        inst->prefix = asm_inst.prefix;
    }

    // format 填充 prefixes
    if (inst->prefix > 0) {
        format->prefix = inst->prefix;
    }

    // format 填充 extensions
    bool ext_exists[OPCODE_EXT_EOF] = {false};
    for (int j = 0; j < 4; ++j) {
        opcode_ext ext = inst->extensions[j];
        if (ext > 0) {
            parser_ext(format, ext);
            ext_exists[ext] = true;
        }
    }


    int i = 0;
    while (i <= 3 && inst->operands[i].type > 0) {
        opcode_operand_t operand = inst->operands[i];
        asm_operand_t *asm_operand = asm_inst.operands[i];
        // asm 参数填充
        if (asm_operand->type == ASM_OPERAND_TYPE_REG || asm_operand->type == ASM_OPERAND_TYPE_FREG) {
            reg_t *r = asm_operand->value;
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
            } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
                if (format->modrm == NULL) {
                    format->modrm = new_modrm();
                    format->modrm->mod = MODRM_MOD_DIRECT_REGISTER;
                }

                if (ext_exists[OPCODE_EXT_SLASHR]) {
                    format->modrm->reg = r->index;
                    if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                        format->rex_prefix->r = r->index > 7; // 添加 rex_prefix-b 前缀
                    } else if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
                        format->vex_prefix->r = r->index <= 7;
                    }
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
                error_exit("unsupported encoding %d", operand.encoding);
                return NULL;
            }

        } else if (asm_operand->type == ASM_OPERAND_TYPE_DISP_REG) {
            asm_disp_reg_t *r = asm_operand->value;
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
                assertf(false, "disp_reg is rm, cannot modrm to reg");
//                if (format->modrm == NULL) {
//                    format->modrm = new_modrm();
//                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
//                }
//
//                format->modrm->reg = r->reg->index;
//                uint8_t temp[] = {r->disp};
//                set_disp(format, r->reg->name, temp, 1);
//
//                if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
//                    format->rex_prefix->r = r->reg->index > 7;
//                }
            } else {
                error_exit("unsupported encoding %d", operand.encoding);
                return NULL;
            }

        } else if (asm_operand->type == ASM_OPERAND_TYPE_INDIRECT_REG) {
            asm_indirect_reg_t *r = asm_operand->value;
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
                error_exit("unsupported encoding %d", operand.encoding);
                return NULL;
            }
        } else if (asm_operand->type == ASM_OPERAND_TYPE_RIP_RELATIVE) { // 还会影响 modrm?
            asm_rip_relative_t *r = asm_operand->value;
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
            } else if (asm_operand->type == ASM_OPERAND_TYPE_SIB_REG) {
                asm_sib_reg_t *sib_reg = asm_operand->value;
                if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
                    if (format->modrm == NULL) {
                        format->modrm = new_modrm();
                    }

                    format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
                    format->modrm->rm = MODRM_MOD_SIB_FOLLOWS_RM;

                    format->sib = new_sib(sib_reg->scale, sib_reg->index->index, sib_reg->base->index);
                    if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
                        format->rex_prefix->x = sib_reg->index->index > 7;
                        format->rex_prefix->b = sib_reg->base->index > 7;
                    }

                    if (sib_reg->base->index == 13) {
                        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
                        uint8_t temp[1] = {0};
                        set_disp(format, sib_reg->base->name, temp, 0);
                    }
                }
            }
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT64) {
            asm_uint64_t *uint = asm_operand->value;
            uint8_t temp[8];
            memcpy(temp, &uint->value, sizeof(uint->value)); // 小端处理
            set_imm(format, temp, 8);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT64) {
            asm_float64_t *f = asm_operand->value;
            uint8_t temp[8];
            memcpy(temp, &f->value, sizeof(f->value));
            set_imm(format, temp, 8);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT32 || asm_operand->type == ASM_OPERAND_TYPE_UINT) {
            asm_uint32_t *uint = asm_operand->value;
            uint8_t temp[4];
            memcpy(temp, &uint->value, sizeof(uint->value)); // 小端处理
            set_imm(format, temp, 4);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT16) {
            asm_uint16_t *uint = asm_operand->value;
            uint8_t temp[2];
            memcpy(temp, &uint->value, sizeof(uint->value)); // 小端处理
            set_imm(format, temp, 2);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT8) {
            asm_uint8_t *uint = asm_operand->value;
            uint8_t temp[1] = {uint->value};
            set_imm(format, temp, 1);
        } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT32) {
            asm_float32_t *f = asm_operand->value;
            uint8_t temp[4];
            memcpy(temp, &f->value, sizeof(f->value));
            set_imm(format, temp, 8);
        } else {
            assertf(false, "unsupported asm operand code %d", asm_operand->type);
            return NULL;
        }
        /*else if (asm_operand->code == ASM_OPERAND_TYPE_INT32) {
          asm_operand_int32 *i = asm_operand->value;
          uint8_t temp[4];
          memcpy(temp, &i->value, sizeof(i->value));
        } else if (asm_operand->code == ASM_OPERAND_TYPE_INT8) {
          asm_operand_int32 *i = asm_operand->value;
          uint8_t temp[4];
          memcpy(temp, &i->value, sizeof(i->value));
        }*/

        i++;
    }
    return format;
}

static void opcode_vex_encoding(amd64_inst_format_t *format, uint8_t *data, uint8_t *count) {
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

static void opcode_rex_encoding(amd64_inst_format_t *format, uint8_t *result) {
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

static void opcode_modrm_encoding(amd64_inst_format_t *format, uint8_t *result) {
    modrm_t *modrm = format->modrm;
    // &7 = &00000111 避免值到大小超过 7
    *result = modrm->rm & 7;
    *result |= (modrm->reg & 7) << 3;

    int mod = (uint8_t) modrm->mod;
    *result |= (mod << 6);
}

static void opcode_sib_encoding(amd64_inst_format_t *format, uint8_t *result) {
    sib_t *sib = format->sib;
    *result = sib->base & 7;
    *result |= (sib->index & 7) << 3;
    *result |= (sib->scale << 6);
}

void opcode_format_encoding(amd64_inst_format_t *format, uint8_t *data, uint8_t *count) {
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
    while (j < 3 && format->opcode[j] > 0) {
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

inst_t *amd64_operation_encoding(asm_operation_t operation, uint8_t *data, uint8_t *count) {
    assert(opcode_tree_root);
    *count = 0;

    inst_t *inst = opcode_select(operation);
    amd64_inst_format_t *format = opcode_fill(inst, operation);
    opcode_format_encoding(format, data, count);
//    data = realloc(data, *count); // 这里如果修改 data 的地址会导致外面的引用位置改变

    return inst;
}



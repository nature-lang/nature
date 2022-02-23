#ifndef NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_
#define NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_

#include <stdlib.h>
#include "src/lib/table.h"
#include "asm.h"

typedef uint8_t size;

typedef enum {
  OPCODE_EXT_NO_EXT,
  OPCODE_EXT_IMM_BYTE,
  OPCODE_EXT_IMM_WORD,
  OPCODE_EXT_IMM_DWORD,
  OPCODE_EXT_SLASH0,
  OPCODE_EXT_SLASH1,
  OPCODE_EXT_SLASH2,
  OPCODE_EXT_SLASH3,
  OPCODE_EXT_SLASH4,
  OPCODE_EXT_SLASH5,
  OPCODE_EXT_SLASH6,
  OPCODE_EXT_SLASH7,
  OPCODE_EXT_SLASHR,
  OPCODE_EXT_REX,
  OPCODE_EXT_REX_W,
  OPCODE_EXT_VEX_128,
  OPCODE_EXT_VEX_256,
  OPCODE_EXT_VEX_66,
  OPCODE_EXT_VEX_F3,
  OPCODE_EXT_VEX_F2,
  OPCODE_EXT_VEX_0F,
  OPCODE_EXT_VEX_0F_38,
  OPCODE_EXT_VEX_0F_3A,
  OPCODE_EXT_VEX_W0,
  OPCODE_EXT_VEX_W1,
  OPCODE_EXT_VEX_WIG,
} opcode_ext;

typedef enum {
  OPERAND_TYPE_REL8,
  OPERAND_TYPE_REL16,
  OPERAND_TYPE_REL32,
  // 表示内存地址，用于 lea 指令中
  OPERAND_TYPE_M,
  OPERAND_TYPE_M16,
  OPERAND_TYPE_M32,
  OPERAND_TYPE_M64,
  OPERAND_TYPE_RM8,
  OPERAND_TYPE_RM16,
  OPERAND_TYPE_RM32,
  OPERAND_TYPE_RM64,
  OPERAND_TYPE_R8,
  OPERAND_TYPE_R16,
  OPERAND_TYPE_R32,
  OPERAND_TYPE_R64,
  OPERAND_TYPE_IMM8,
  OPERAND_TYPE_IMM16,
  OPERAND_TYPE_IMM32,
  OPERAND_TYPE_IMM64,
  OPERAND_TYPE_XMM1,
  OPERAND_TYPE_XMM1M64,
  OPERAND_TYPE_XMM2,
  OPERAND_TYPE_XMM2M64,
  OPERAND_TYPE_XMM2M128,
  OPERAND_TYPE_YMM1,
  OPERAND_TYPE_YMM2,
  OPERAND_TYPE_YMM2M128,
} operand_type;

typedef enum {
  ENCODING_TYPE_MODRM_RM,
  ENCODING_TYPE_MODRM_REG,
  ENCODING_TYPE_IMM,
  ENCODING_TYPE_VEX_VVVV,
  ENCODING_TYPE_OPCODE_PLUS,
} encoding_type;

typedef struct {
  uint8_t source;
  bool l;
  bool r;
  bool w;
  bool x;
  bool b;

  uint8_t vex_opcode_extension;
  uint8_t vex_legacy_byte;
} vex_prefix_t;

typedef struct {
  bool w;
  bool r;
  bool x;
  bool b;
} rex_prefix_t;

typedef struct {
  uint8_t mod; // 6,7
  uint8_t reg; // 3,4,5
  uint8_t rm; // 0,1,2
} modrm_t;

typedef struct {
  uint8_t scale;
  uint8_t index;
  uint8_t base;
} sib_t;

/**
 * 机器码指令参数结构
 */
typedef struct {
  operand_type type;
  encoding_type encoding; // 编码函数,将汇编参数编码成指令参数
  uint8_t size;
} opcode_operand_t; // reg 64

typedef struct {
  char *name; // 指令名称
  uint8_t prefix;
  uint8_t opcode[3];
  uint8_t extensions[4];
  opcode_operand_t operands[4]; // 形参
} inst_t; // 机器指令,中间表示, 该中间表示可以快速过渡到 inst_format?

/**
 * 机器码指令结构, 难道是这个的描述性不好？
 */
typedef struct {
  uint8_t prefix; // 不知道是干嘛的，命名都有 rex.prefix 和 vex.prefix 了
  vex_prefix_t *vex_prefix;
  rex_prefix_t *rex_prefix;
  uint8_t opcode[3];
  modrm_t *modrm;
  sib_t *sib;
  uint8_t displacement[4];
  uint8_t immediate[4];
} inst_format_t; // 机器编码类型

// 注册指令列表 asm operand
inst_t mov_r16_rm16 = {"mov", 0x66, {0xb8}, {OPCODE_EXT_SLASHR},
                       {
                           {OPERAND_TYPE_R16, ENCODING_TYPE_MODRM_REG},
                           {OPERAND_TYPE_RM16, ENCODING_TYPE_MODRM_RM}
                       }
};

// 注册到指令树 map[] + operand_tree
// 方式1： key 为 inst operand, 比如进入值为 rm16, 那么将会匹配一个 succs 的列表，然后继续递归啊查找，最终找到一个列表
// 方式2： key 为 asm operand, 也就是 jit-compiler 中的方式, 但是目前的 key 没有更加细腻的类型。比如 t_register 就没有明确的宽度字符串
// 如果需要完全实现的话，需要有宽度字符串的参与,才能构建 key

typedef struct {
  inst_t *opcodes[10]; // data 数据段，最终的叶子节点才会有该数据
  string key; // 筛选 key 为 inst 指令的 operand 部分比如 -> OPERAND_TYPE_R64, 如果深度为 1, key 为 opcode
  table *succs;
} opcode_tree_node_t;

opcode_tree_node_t *opcode_tree_root; // key = root

char *asm_operand_to_string(asm_operand_type type, int byte);

/**
 * 低级指令转换为高级指令。
 * rel8 => uint8
 * rel16 => uint16
 * rel32 => uint32
 * rm8 => register:1/indirect_register:1/disp_register:1/rip_relative:1/sib_register:8
 * rm16 => register:2/indirect_register:2/disp_register:2/rip_relative:2/sib_register:8
 * rm32 => register:4/indirect_register:4/disp_register:4/rip_relative:4/sib_register:8
 * rm64 => register:8/indirect_register:8/disp_register:8/rip_relative:8/sib_register:8
 * m => disp_register:8/rip_relative:8
 * m16 => indirect_register:2
 * m32 => indirect_register:4
 * m64 => indirect_register:8
 * r8 => register:1
 * r16 => register:2
 * r32 => register:4
 * r64 => register:8
 * xmm1 => register:16
 * xmm2 => register:16
 * xmm1m64 => register:16/register:8/indirect_reg:8/rip_relative:8/sib_register:8
 * xmm2m64 => register:16/register:8/indirect_reg:8/rip_relative:8/sib_register:8
 * xmm2m128 => register:16/register:8/indirect_reg:8/rip_relative:8
 * ymm1 => register:32
 * ymm2 => register:32
 *
 * 接受一个 type(int) 响应一个 n type + size 的字符串列表
 */
char **operand_low_to_high(operand_type t);

/**
 * 1. 初始化 opcode_root
 * 2. 将所有的指令注册到 tree 中
 * @return
 */
void *opcode_init();

void opcode_tree_build(inst_t *inst);

opcode_tree_node_t *opcode_find_name(string name);

void opcode_find_succs(opcode_tree_node_t node, opcode_operand_t operands[4]);

#endif //NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_

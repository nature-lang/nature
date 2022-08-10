#ifndef NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_
#define NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_

#include <stdlib.h>
#include <stdint.h>
#include "utils/table.h"
#include "asm.h"

typedef uint8_t size;

typedef enum {
    OPCODE_EXT_IMM_BYTE = 1,
    OPCODE_EXT_IMM_WORD,
    OPCODE_EXT_IMM_DWORD,
    OPCODE_EXT_IMM_QWORD, // 8byte
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
    OPCODE_EXT_EOF,
} opcode_ext;

/**
 * 当一个 asm 指令匹配下面的多个类型时，按从小到大顺序选择
 */
typedef enum {
    OPERAND_TYPE_REL8 = 1,
    OPERAND_TYPE_REL16,
    OPERAND_TYPE_REL32,
    // 表示内存地址，用于 lea 指令中
    OPERAND_TYPE_M,
    OPERAND_TYPE_M16,
    OPERAND_TYPE_M32,
    OPERAND_TYPE_M64,
    OPERAND_TYPE_AL,
    OPERAND_TYPE_R8,
    OPERAND_TYPE_R16,
    OPERAND_TYPE_R32,
    OPERAND_TYPE_RAX,
    OPERAND_TYPE_R64,
    OPERAND_TYPE_RM8,
    OPERAND_TYPE_RM16,
    OPERAND_TYPE_RM32,
    OPERAND_TYPE_RM64,
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
    ENCODING_TYPE_MODRM_RM = 1,
    ENCODING_TYPE_MODRM_REG,
    ENCODING_TYPE_MODRM_RAX,
    ENCODING_TYPE_MODRM_AL,
    ENCODING_TYPE_IMM,
    ENCODING_TYPE_VEX_VVVV,
    ENCODING_TYPE_OPCODE_PLUS,
} encoding_type;

typedef enum {
    VEX_OPCODE_EXT_66 = 1,
    VEX_OPCODE_EXT_F2,
    VEX_OPCODE_EXT_F3,
} vex_opcode_ext;

typedef enum {
    VEX_LEGACY_BYTE_0F = 1,
    VEX_LEGACY_BYTE_0F_38,
    VEX_LEGACY_BYTE_0F_3A,
} vex_legacy_byte;

typedef enum {
    MODRM_MOD_INDIRECT_REGISTER = 0, // 00
    MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP = 1, // 01
    MODRM_MOD_INDIRECT_REGISTER_DWORD_DISP = 2, // 10
    MODRM_MOD_DIRECT_REGISTER = 3, // 11
    MODRM_MOD_SIB_FOLLOWS_RM = 4,
} modrm_mod;

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
    modrm_mod mod; // 6,7
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
    opcode_ext extensions[4];
    opcode_operand_t operands[4]; // 形参
} inst_t; // 机器指令,中间表示, 该中间表示可以快速过渡到 inst_format?

/**
 * 机器码指令结构, 难道是这个的描述性不好？
 */
typedef struct {
    uint8_t prefix; // 不知道是干嘛的，明明都有 rex.prefix 和 vex.prefix 了
    vex_prefix_t *vex_prefix;
    rex_prefix_t *rex_prefix;
    uint8_t opcode[3];
    modrm_t *modrm;
    sib_t *sib;
    uint8_t disps[8]; // 64 位可能是 8 个字节才对
    uint8_t disp_count;
    uint8_t imms[8];
    uint8_t imm_count;
} amd64_inst_format_t; // 机器编码类型


/**
 * 存储多个 opcode 的数据结构
 */
typedef struct {
    inst_t **list; // 默认初始化 10 大小
    int count;
} amd64_insts_t;

// 注册到指令树 map[] + operand_tree
// 方式1： key 为 inst operand, 比如进入值为 rm16, 那么将会匹配一个 succs 的列表，然后继续递归啊查找，最终找到一个列表
// 方式2： key 为 asm operand, 也就是 jit-compiler 中的方式, 但是目前的 key 没有更加细腻的类型。比如 t_register 就没有明确的宽度字符串
// 如果需要完全实现的话，需要有宽度字符串的参与,才能构建 key
typedef struct {
    amd64_insts_t insts; // data 数据段，最终的叶子节点才会有该数据
    string key; // 筛选 key 为 inst 指令的 operand 部分比如 -> OPERAND_TYPE_R64, 如果深度为 1, key 为 opcode
    table *succs;
} amd64_opcode_tree_node_t;

typedef struct {
    uint16_t *list;
    int count;
} asm_keys_t;

amd64_opcode_tree_node_t *opcode_tree_root; // key = root

uint16_t asm_operand_to_key(uint8_t type, uint8_t byte);

/**
 * 低级指令转换为高级指令。
 * rel8 => uint8:1
 * rel16 => uint16:2
 * rel32 => uint32:4
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
asm_keys_t operand_low_to_high(operand_type t);

/**
 * 1. 初始化 opcode_root
 * 2. 将所有的指令注册到 tree 中
 * @return
 */
void amd64_opcode_init();

void opcode_tree_build(inst_t *inst);

amd64_opcode_tree_node_t *opcode_find_name(string name);

void opcode_find_succs(amd64_opcode_tree_node_t *node, inst_t *inst, int operands_index);

/**
 * 指令选择
 * 根据 asm 指令选择 opcode
 * 1. map 结构选择指令
 * 2. tree 结构进一步选择
 * 3. 得到 opcodes 列表，堆一些特殊 inst 做简单过滤
 */
inst_t *opcode_select(amd64_asm_inst_t asm_inst);

/**
 * 指令填充
 * asm_inst + inst = inst_format
 * 主要是 inst 填充到 inst_format
 * @param asm_inst
 * @param inst
 */
amd64_inst_format_t *opcode_fill(inst_t *inst, amd64_asm_inst_t asm_inst);

void opcode_format_encoding(amd64_inst_format_t *format, uint8_t *data, uint8_t *count);

void opcode_sort_insts(amd64_insts_t *insts);

uint8_t *amd64_opcode_encoding(amd64_asm_inst_t asm_inst, uint8_t *count);

#endif //NATURE_SRC_ASSEMBLER_AMD64_OPCODE_H_

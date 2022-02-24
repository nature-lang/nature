#include "opcode.h"
#include "src/lib/error.h"
#include "src/lib/helper.h"

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
void *opcode_init() {
  // 收集所有指令，进行注册
}

uint16_t asm_operand_to_key(uint8_t type, uint8_t byte) {
  uint16_t flag = ((uint16_t) type << 8) | byte;
  return flag;
}

asm_keys_t operand_low_to_high(operand_type t) {
  asm_keys_t res;

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
    highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 1);
    highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_RM16) {
    res.count = 5;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 2);
    highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 2);
    highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 2);
    highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 2);
    highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_RM32) {
    res.count = 5;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 4);
    highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 4);
    highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 4);
    highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 4);
    highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_RM64) {
    res.count = 5;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 8);
    highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 8);
    highs[2] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 8);
    highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 8);
    highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_M) {
    res.count = 2;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_DISP_REGISTER, 8);
    highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 8);
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
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 8);
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
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 8);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_XMM1 || t == OPERAND_TYPE_XMM2) {
    res.count = 1;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 16);
    res.list = highs;
    return res;
  }

  if (t == OPERAND_TYPE_XMM1M64 || t == OPERAND_TYPE_XMM1M64) {
    res.count = 5;
    uint16_t *highs = malloc(sizeof(uint16_t) * res.count);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 8);
    highs[0] = asm_operand_to_key(ASM_OPERAND_TYPE_REGISTER, 16);
    highs[1] = asm_operand_to_key(ASM_OPERAND_TYPE_INDIRECT_REGISTER, 8);
    highs[3] = asm_operand_to_key(ASM_OPERAND_TYPE_RIP_RELATIVE, 8);
    highs[4] = asm_operand_to_key(ASM_OPERAND_TYPE_SIB_REGISTER, 8);
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

  error_exit(1, "cannot identify operand_type");
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

  opcode_tree_node_t *node = NEW(opcode_tree_node_t);
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
  // 表示已经找到头了，深圳有可能溢出
  if (operand.type == 0) {
    node->opcodes[node->opcodes_count++] = inst;
    return;
  }

  // 将一个底层指令转换成高层指令
  asm_keys_t asm_keys = operand_low_to_high(operand.type);
  for (int i = 0; i < asm_keys.count; ++i) {
    uint16_t key_int = asm_keys.list[i];
    char *key = itoa(key_int);
    bool exists = table_exist(node->succs, key);
    if (!exists) {
      opcode_tree_node_t *node = NEW(opcode_tree_node_t);
      node->key = key;
      table_set(node->succs, key, node);
    }
    // 继续寻找下一级node
    opcode_find_succs(table_get(node->succs, key), inst, operands_index + 1);
  }
}

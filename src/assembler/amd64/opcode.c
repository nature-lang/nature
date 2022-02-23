#include "opcode.h"

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

void opcode_tree_build(inst_t *inst) {
  // 第一层结构 指令名称
  node = find_name(inst->name);

  // 其余层级结构,指令参数
  find_succs(node, inst->operands, 0)
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
  return node;
}

char **operand_low_to_high(operand_type t) {
  
}

/**
 * @param node 树节点
 * @return
 */
void *find_succs(node, operands, index) {
  // 读取 node
  operand = operands[index];

  // 将一个底层指令转换成高层指令
  asm_operands = operand_low_to_high[operand.type];

  // 将高层指令列表注册到下一级树中
  for (asm_operands as asm_operand) {
    // 如果不存在就添加到 nodes 中
    exists := node.succs[asm_operand]
    if (!exists) {
      node.success[asm_operand] = new(node)
    }

    find_succ(node.succs[asm_operand], operands, index + 1)
  }
}

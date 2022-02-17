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
void *tree_build(inst_t *inst) {
  // 第一层结构 指令名称
  node = find_or_append_name(inst->name);

  uint8_t i = 0;
  while (inst->operands[i] != NULL) {
    find_or_append_operand(node, inst->operands)
  }

  // 其余层级结构,指令参数
}

// operands
void *find_succ(node, operands, index) {
  // 一个 operand 可以产生多个 node, 下一个 operand 应该递归继续生成
  operand = operands[index];

  // 这一步是干什么？
  asm_operands = map[operand.type];
  for
  asm_operands
  {
    // 判断是否存在，存在就找不出，不存在就创建
    current, exists := node.succs[asm_operand]
    if !exists
    {

    }
    find_succ(current, operands, index + 1)
  }
}

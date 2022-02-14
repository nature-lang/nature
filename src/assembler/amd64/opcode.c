#include "opcode.h"

void *tree_build(inst_t *inst) {
  // 第一层结构 指令名称
  node = find_or_append_name(inst->name);

  uint8_t i = 0;
  while (inst->operands[i] != NULL) {
    find_or_append_operand(node, inst->operands)
  }

  // 其余层级结构,指令参数
}

// operand 使用链表结构
void *find_succ(node, operands, index) {
  // 一个 operand 可以产生多个 node, 下一个 operand 应该递归继续生成
  operand = operands[index];
  asm_operands = map[operand.type];
  for {
    // 判断是否存在，存在就找不出，不存在就创建
    current, exists := node.succs[asm_operand]
    if !exists {

    }
    find_succ(current, operands, index + 1)
  }
}

#include "lib_elf.h"
#include "elf.h"
#include "src/lib/helper.h"

/**
 *
 * 输入：text 代码段 (byte 列表)
 * .data 变量定义段是否需要？
 */
void elf_target_build() {
  // elf header 文件头表构建

  // .text 代码段

  // 重定位表

  // section header table 段表构建
}

void elf_text_inst_build(asm_inst_t asm_inst) {
  uint64_t *offset = elf_new_current_offset();
  if (strequal(asm_inst.name, "label")) {
    // text 中唯一需要注册到符号表的数据, 且不需要编译进 elf_text_item
    asm_operand_symbol_t *s = asm_inst.operands[0]->value;
    elf_symbol_t symbol = {
        .name = s->name,
        .type = ELF_SYMBOL_TYPE_FN,
        .section = ELF_SECTION_TEXT,
        .offset = offset,
        .size = 0,
        .is_rel = false,
    };
    elf_symbol_insert(symbol);
    elf_confirm_text_rel(symbol.name);
  }

  // label 引用处理(使用相对跳转)
  asm_operand_t *label_operand = asm_has_label_operand(asm_inst);
  if (label_operand != NULL) {
    // 判断符号是否在符号表中,在则计算 rel 并改写指令(大于 128 使用 rel32, 小于 128 使用 rel8)
    // 不在则使用 rel32 占位

    // label 改写为 new_operands
    // symbol_operand to rel operand
  }

  uint8_t *data = malloc(sizeof(uint8_t) * 30);
  uint8_t count = 0;
  opcode_encoding(asm_inst, data, &count);

  // 注册 elf_text_inst_t 到 elf_text_inst list 中
}

/**
 * 倒推符号表，如果找到符号占用引用则记录位置
 *
 *
 */
void elf_confirm_text_rel(string name) {
  if (list_empty(elf_text_inst_list)) {
    return;
  }

  list_node *current = elf_text_inst_list->rear->prev; // rear 为 empty 占位
  uint8_t find_count = 0; // 每找到一个 offset 距离将缩短 3 个
  while (true) {
    elf_text_inst_t *inst = current->value;
    if (current_offset - (find_count * 3) - *inst->offset > 128) {
      break;
    }

    if (inst->may_rel_change && strequal(inst->rel_symbol, name)) {
      find_count += 1;
    }

    // current 保存当前值
    if (current->prev == NULL) {
      break;
    }
    current = current->prev;
  }

  if (find_count == 0) {
    return;
  }

  uint64_t *offset = ((elf_text_inst_t *) current->value)->offset;
  // 从 current 开始左 rewrite
  while (current->value != NULL) {
    elf_text_inst_t *inst = current->value;
    if (strequal(inst->rel_symbol, name)) {
      // 重写 inst 指令为 rel8
      // jmp 的具体位置可以不计算，等到二次遍历再计算
      // 届时符号表已经全部收集完毕
      elf_rewrite_text_rel(inst);
      *inst->offset = *offset; // 值替换，而不是指针地址替换
    }

    *offset += inst->count; // 重新计算 offset
    current = current->next;
  }

  // 最新的 offset
  current_offset = *offset;
}

/**
 * inst rel32 to rel8
 * @param inst
 */
void elf_rewrite_text_rel(elf_text_inst_t *inst) {
  asm_operand_uint8 *operand = NEW(asm_operand_uint8);
  operand->value = 0; // 仅占位即可
  inst->asm_inst.count = 1;
  inst->asm_inst.operands[0]->type = ASM_OPERAND_TYPE_UINT8;
  inst->asm_inst.operands[0]->size = BYTE;
  inst->asm_inst.operands[0]->value = operand;

  inst->data = malloc(sizeof(uint8_t) * 30);
  inst->count = 0;
  opcode_encoding(inst->asm_inst, inst->data, &inst->count);
  inst->rel_symbol = "";
  inst->may_rel_change = false;
}

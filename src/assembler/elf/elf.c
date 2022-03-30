#include "lib_elf.h"
#include "elf.h"
#include "src/lib/helper.h"

static void elf_fn_operand_rewrite(asm_operand_t *operand, int rel) {
  if (rel == 0 || abs(rel) > 128) {
    operand->type = ASM_OPERAND_TYPE_UINT32;
    operand->size = DWORD;
    asm_operand_uint32 *v = NEW(asm_operand_uint32);
    v->value = (uint32_t) rel;
    operand->value = v;
    return;
  }
  operand->type = ASM_OPERAND_TYPE_UINT8;
  operand->size = BYTE;
  asm_operand_uint8 *v = NEW(asm_operand_uint8);
  v->value = (uint8_t) rel;
  operand->value = v;
}

void elf_text_inst_build(asm_inst_t asm_inst) {
  uint64_t *offset = elf_new_current_offset();
  if (strequal(asm_inst.name, "label")) {
    // text 中唯一需要注册到符号表的数据, 且不需要编译进 elf_text_item
    asm_operand_symbol_t *s = asm_inst.operands[0]->value;
    elf_symbol_t *symbol = NEW(elf_symbol_t);
    symbol->name = s->name;
    symbol->type = ELF_SYMBOL_TYPE_FN;
    symbol->section = ELF_SECTION_TEXT;
    symbol->offset = offset;
    symbol->size = 0;
    symbol->is_rel = false;
    elf_symbol_insert(symbol);
    elf_confirm_text_rel(symbol->name);
    return; // label 不需要编译成指令
  }

  elf_text_inst_t *inst = NEW(elf_text_inst_t);
  inst->rel_operand = NULL;
  inst->rel_symbol = NULL;

  // 外部符号引用处理
  asm_operand_t *fn_operand = asm_has_fn_operand(asm_inst);
  if (fn_operand != NULL) {
    asm_operand_symbol_t *symbol_operand = fn_operand->value;

    if (table_exist(elf_symbol_table, symbol_operand->name)) {
      elf_symbol_t *symbol = table_get(elf_symbol_table, symbol_operand->name);
      // 计算 offset 并填充
      int rel_diff = global_offset - *symbol->offset;
      elf_fn_operand_rewrite(fn_operand, rel_diff);
    } else {
      // 引用了 label 符号，但是符号确不在符号表中,也需要改写
      elf_fn_operand_rewrite(fn_operand, 0);
      inst->rel_operand = fn_operand;
      inst->rel_symbol = symbol_operand->name;
    }
  }

  // mov var,reg 处理，处理方式略显不同(暂时不用处理)
  uint8_t *data = malloc(sizeof(uint8_t) * 30);
  uint8_t count = 0;
  opcode_encoding(asm_inst, data, &count);

  inst->data = data;
  inst->count = count;
  inst->offset = offset;
  inst->asm_inst = asm_inst;

  // 注册 elf_text_inst_t 到 elf_text_inst_list 和 elf_text_table 中
  list_push(elf_text_inst_list, inst);
}

/**
 * 倒推符号表，如果找到符号占用引用则记录位置
 */
void elf_confirm_text_rel(string name) {
  if (list_empty(elf_text_inst_list)) {
    return;
  }

  list_node *current = elf_text_inst_list->rear->prev; // rear 为 empty 占位
  uint8_t find_count = 0; // 每找到一个 offset 距离将缩短 3 个
  while (true) {
    elf_text_inst_t *inst = current->value;
    if (global_offset - (find_count * 3) - *inst->offset > 128) {
      break;
    }

    if (inst->rel_symbol != NULL && strequal(inst->rel_symbol, name)) {
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
  global_offset = *offset;
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
  inst->rel_symbol = NULL;
}

/**
 * 遍历  elf_text_inst_list 如果其存在 rel_symbol,即符号引用
 * 则判断其是否在符号表中，如果其在符号表中，则填充指令 value 部分(此时可以选择重新编译)
 * 如果其依旧不在符号表中，则表示其引用了外部符号，此时直接添加一条 rel 记录即可
 * @param elf_text_inst_list
 */
void elf_text_second_build(list *text_inst_list) {
  if (list_empty(text_inst_list)) {
    return;
  }

  list_node *current = text_inst_list->front;
  while (current->value != NULL) {
    elf_text_inst_t *inst = current->value;
    if (inst->rel_symbol == NULL) {
      current = current->next;
      continue;
    }

    if (table_exist(elf_symbol_table, inst->rel_symbol)) {
      // 计算 rel
      elf_symbol_t *symbol = table_get(elf_symbol_table, inst->rel_symbol);
      int rel_diff = *inst->offset - *symbol->offset;

      elf_fn_operand_rewrite(inst->rel_operand, rel_diff);
      // 重新 encoding 指令
      opcode_encoding(inst->asm_inst, inst->data, &inst->count);
    } else {
      // 二次扫描都没有在符号表中找到，说明引用了外部符号(是否需要区分引用的外部符号的类型不同？section 填写的又是什么段？)
      elf_rel_t *rel = NEW(elf_rel_t);
      rel->name = inst->rel_symbol;
      rel->offset = inst->offset;
      rel->section = ELF_SECTION_TEXT;
      list_push(elf_rel_list, rel);
    }

  }
}

void elf_text_build(list *asm_inst_list) {
  if (list_empty(asm_inst_list)) {
    return;
  }
  list_node *current = asm_inst_list->front;
  while (current->value != NULL) {
    asm_inst_t *inst = current->value;
    elf_text_inst_build(*inst);
  }
}

void elf_symbol_insert(elf_symbol_t *symbol) {
  table_set(elf_symbol_table, symbol->name, symbol);
  list_push(elf_symbol_list, symbol);
}

uint64_t *elf_new_current_offset() {
  uint64_t *offset = NEW(uint64_t);
  *offset = global_offset;
  return offset;
}

/**
 * 文件头表
 * 代码段 (.text)
 * 数据段 (.data)
 * 段表字符串表 (.shstrtab)
 * 段表 (section header table)
 * 符号表 (.symtab)
 * 字符串表(.strtab)
 * 重定位表(.rel.text)
 * 重定位表(.rel.data)
 * @return
 */
uint8_t *elf_encoding() {
  return NULL;
}

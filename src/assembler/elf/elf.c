#include "lib_elf.h"
#include "elf.h"
#include "src/lib/helper.h"
#include <string.h>

static void elf_var_operand_rewrite(asm_operand_t *operand) {
  operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;
  operand->size = QWORD;
  asm_operand_rip_relative *r = NEW(asm_operand_rip_relative);
  r->disp = 0;
  operand->value = r;
}

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
    symbol->is_local = s->is_local; // 局部 label 在生成符号表的时候可以忽略
    elf_symbol_insert(symbol);
    elf_confirm_text_rel(symbol->name);
    return; // label 不需要编译成指令
  }

  elf_text_inst_t *inst = NEW(elf_text_inst_t);
  inst->rel_operand = NULL;
  inst->rel_symbol = NULL;


  // 外部标签引用处理
  asm_operand_t *operand = asm_has_symbol_operand(asm_inst);
  if (operand != NULL) {
    // 1. 数据符号引用(直接改写成 0x0(rip))
    // 2. 标签符号引用(在符号表中,表明为内部符号,否则使用 rel32 占位)
    asm_operand_symbol_t *symbol_operand = operand->value;
    if (symbol_operand->is_label) {
      if (table_exist(elf_symbol_table, symbol_operand->name)) {
        elf_symbol_t *symbol = table_get(elf_symbol_table, symbol_operand->name);
        // 计算 offset 并填充
        int rel_diff = global_offset - *symbol->offset;
        elf_fn_operand_rewrite(operand, rel_diff);
      } else {
        // 引用了 label 符号，但是符号确不在符号表中,也需要改写
        elf_fn_operand_rewrite(operand, 0);
        inst->rel_operand = operand;
        inst->rel_symbol = symbol_operand->name;
      }
    } else {
      // 数据符号重写
      elf_var_operand_rewrite(operand);

      // 添加到重定位表
      elf_rel_t *rel = NEW(elf_rel_t);
      rel->name = symbol_operand->name;
      rel->offset = offset; // 引用的具体位置
      rel->section = ELF_SECTION_TEXT;
      rel->type = ELF_SYMBOL_TYPE_VAR;
      list_push(elf_rel_list, rel);
    }
  }

  uint8_t *data = malloc(sizeof(uint8_t) * 30);
  uint8_t count = 0;
  opcode_encoding(asm_inst, data, &count);
  global_offset += count; // advance global offset

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
void elf_text_inst_list_second_build(list *elf_text_inst_list) {
  if (list_empty(elf_text_inst_list)) {
    return;
  }

  list_node *current = elf_text_inst_list->front;
  while (current->value != NULL) {
    elf_text_inst_t *inst = current->value;
    if (inst->rel_symbol == NULL) {
      current = current->next;
      continue;
    }

    // 计算 rel
    elf_symbol_t *symbol = table_get(elf_symbol_table, inst->rel_symbol);
    if (symbol != NULL && !symbol->is_rel) {
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
      rel->type = ELF_SYMBOL_TYPE_FN;
      list_push(elf_rel_list, rel);

      if (symbol == NULL) {
        elf_symbol_t *symbol = NEW(elf_symbol_t);
        symbol->name = inst->rel_symbol;
        symbol->type = 0; // 外部符号引用直接 no type ?
        symbol->section = 0;
        symbol->offset = 0;
        symbol->size = 0;
        symbol->is_rel = true; // 是否为外部引用符号(避免重复添加)
        symbol->is_local = false; // 局部 label 在生成符号表的时候可以忽略
        elf_symbol_insert(symbol);
      }
    }

  }
}

void elf_text_inst_list_build(list *asm_inst_list) {
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

static char *elf_header_ident() {
  char *ident = NEW(sizeof(char) * EI_NIDENT);
  memset(ident, 0, EI_NIDENT);

  ident[0] = 0x7f; // del 符号的编码
  ident[1] = 'E';
  ident[2] = 'L';
  ident[3] = 'F';
  ident[4] = ELFCLASS64; // elf 文件类型: 64 位
  ident[5] = ELFDATA2LSB; // 字节序： 小端
  ident[6] = EV_CURRENT; // elf 版本号
  ident[7] = ELFOSABI_NONE; // os abi = unix v
  ident[8] = 0; // ABI version
  return ident;
}

void elf_build() {
  // 代码段构建 .text

  // 文件头构建
  Elf64_Ehdr header = {
      .e_ident = elf_header_ident(),
      .e_type = ET_REL, // elf 文件类型 = 可重定位文件
      .e_machine = EM_X86_64,
      .e_version = EV_CURRENT,
      .e_entry = 0, // elf 文件程序入口的线性绝对地址，一般用于可执行文件，可重定位文件配置为 0 即可
      .e_phoff = 0, // 程序头表在文件中的偏移，对于可重定位文件来说，值同样为 0，
      .e_shoff = 0, // 段表在文件头表的偏移地址，现在还不能计算出来
      .e_flags = 0, // elf 平台相关熟悉，设置为 0 即可
      .e_ehsize = sizeof(Elf64_Ehdr), // 文件头表的大小
      .e_phentsize = 0, // 程序头表项的大小, 可重定位表没有这个头
      .e_phnum = 0, // 程序头表项, 这个只能是 0
      .e_shentsize = sizeof(Elf64_Shdr), // 段表项的大小
      .e_shstrndx = 0 // 段表字符串表在段表中的索引
  };
}

/**
 * 包含的项：
 * .text.data/.rel.text/.shstrtab/.symtab/.strtab
 * @param text_size
 * @param symbol_table_size
 * @param strtab_size
 * @param rel_text_size
 * @return
 */
char *elf_section_table_build(uint64_t text_size,
                              uint64_t symbol_table_size,
                              uint64_t symbol_last_local_index,
                              uint64_t strtab_size,
                              uint64_t rel_text_size,
                              Elf64_Shdr **section_table,
                              uint8_t *count) {
  // 段表字符串表
  char *shstrtab_data = "\0";
  uint64_t rela_text_name = strlen(shstrtab_data);
  uint64_t text_name = 5;
  shstrtab_data = str_connect(shstrtab_data, ".rela.text\0");
  uint64_t data_name = strlen(shstrtab_data);
  shstrtab_data = str_connect(shstrtab_data, ".data\0");
  uint64_t shstrtab_name = strlen(shstrtab_data);
  shstrtab_data = str_connect(shstrtab_data, ".shstrtab\0");
  uint64_t symtab_name = strlen(shstrtab_data);
  shstrtab_data = str_connect(shstrtab_data, ".symtab\0");
  uint64_t strtab_name = strlen(shstrtab_data);
  shstrtab_data = str_connect(shstrtab_data, ".strtab\0");

  uint64_t offset = sizeof(Elf64_Ehdr);
  uint64_t text_offset = offset;
  offset += text_size;
  uint64_t data_offset = offset;
  offset += 0;
  uint64_t shstrtab_offset = offset;
  offset += strlen(shstrtab_data);


//  Elf64_Shdr **section_table = malloc(sizeof(Elf64_Shdr) * 5);

  // 空段
  Elf64_Shdr *empty = NEW(Elf64_Shdr);
  empty->sh_name = 0;
  empty->sh_type = 0; // 表示程序段
  empty->sh_flags = 0;
  empty->sh_addr = 0; // 可执行文件才有该地址
  empty->sh_offset = 0;
  empty->sh_size = 0;
  empty->sh_link = 0;
  empty->sh_info = 0;
  empty->sh_addralign = 0;
  empty->sh_entsize = 0;
  section_table[0] = empty;

  // 代码段
  Elf64_Shdr *text = NEW(Elf64_Shdr);
  text->sh_name = text_name;
  text->sh_type = SHT_PROGBITS; // 表示程序段
  text->sh_flags = SHF_ALLOC | SHF_EXCLUDE;
  text->sh_addr = 0; // 可执行文件才有该地址
  text->sh_offset = offset;
  text->sh_size = text_size;
  offset += text_size;
  text->sh_link = 0;
  text->sh_info = 0;
  text->sh_addralign = 1;
  text->sh_entsize = 0;
  section_table[1] = text;

  // 代码段重定位表
  Elf64_Shdr *rela_text = NEW(Elf64_Shdr);
  rela_text->sh_name = rela_text_name;
  rela_text->sh_type = SHT_RELA; // 表示程序段
  rela_text->sh_flags = SHF_INFO_LINK;
  rela_text->sh_addr = 0; // 可执行文件才有该地址
  rela_text->sh_offset = offset;
  rela_text->sh_size = rel_text_size;
  offset += rela_text->sh_size;
  rela_text->sh_link = 4; // 使用的符号表所在段(为啥重定位表要在符号表定义)
  rela_text->sh_info = 1; // 重定位表作用的段的段表索引
  rela_text->sh_addralign = 8;
  rela_text->sh_entsize = sizeof(Elf64_Rel);
  section_table[2] = rela_text;

  // 数据段
  Elf64_Shdr *data = NEW(Elf64_Shdr);
  data->sh_name = data_name;
  data->sh_type = SHT_PROGBITS; // 表示程序段
  data->sh_flags = SHF_ALLOC | SHF_WRITE;
  data->sh_addr = 0; // 可执行文件才有该地址
  data->sh_offset = offset;
  data->sh_size = 0;
  offset += data->sh_size;
  data->sh_link = 0;
  data->sh_info = 0;
  data->sh_addralign = 4;
  data->sh_entsize = 0;
  section_table[3] = data;

  // 符号表段
  Elf64_Shdr *symtab = NEW(Elf64_Shdr);
  symtab->sh_name = symtab_name;
  symtab->sh_type = SHT_SYMTAB; // 表示程序段
  symtab->sh_flags = 0;
  symtab->sh_addr = 0; // 可执行文件才有该地址
  symtab->sh_offset = offset;
  symtab->sh_size = symbol_table_size;
  offset += symtab->sh_size;
  symtab->sh_link = 5;
  symtab->sh_info = symbol_last_local_index + 1; // 符号表最后一个 local 符号的索引
  symtab->sh_addralign = 8;
  symtab->sh_entsize = sizeof(Elf64_Sym);
  section_table[4] = symtab;

  // 字符串串表 5
  Elf64_Shdr *strtab = NEW(Elf64_Shdr);
  symtab->sh_name = strtab_name;
  symtab->sh_type = SHT_STRTAB; // 表示程序段
  symtab->sh_flags = 0;
  symtab->sh_addr = 0; // 可执行文件才有该地址
  symtab->sh_offset = offset;
  symtab->sh_size = strtab_size;
  offset += symtab->sh_size;
  symtab->sh_link = 0;
  symtab->sh_info = 0; // 符号表最后一个 local 符号的索引
  symtab->sh_addralign = 1;
  symtab->sh_entsize = 0;
  section_table[5] = symtab;

  // 段表字符串表 6
  Elf64_Shdr *shstrtab = NEW(Elf64_Shdr);
  shstrtab->sh_name = shstrtab_name;
  shstrtab->sh_type = SHT_STRTAB; // 表示程序段
  shstrtab->sh_flags = 0;
  shstrtab->sh_addr = 0; // 可执行文件才有该地址
  shstrtab->sh_offset = offset;
  shstrtab->sh_size = strlen(shstrtab_data);
  offset += shstrtab->sh_size;
  shstrtab->sh_link = 0;
  shstrtab->sh_info = 0;
  shstrtab->sh_addralign = 1;
  shstrtab->sh_entsize = 0;
  section_table[6] = shstrtab;

  *count = 7;

  return shstrtab_data;
}

char *elf_symbol_table_build(Elf64_Sym *symbol, uint8_t *count) {
  // 0: NULL

  // 1: file

  // 2: section: 1 = .text
  // 3: section: 3 = .data
  return NULL;
}



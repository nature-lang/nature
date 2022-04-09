#include "elf.h"
#include "src/lib/helper.h"
#include <string.h>
#include <stdio.h>

static void elf_var_operand_rewrite(asm_operand_t *operand) {
  operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;
  operand->size = QWORD;
  asm_operand_rip_relative_t *r = NEW(asm_operand_rip_relative_t);
  r->disp = 0;
  operand->value = r;
}

static void elf_fn_operand_rewrite(asm_operand_t *operand, int rel) {
  if (rel == 0 || abs(rel) > 128) {
    operand->type = ASM_OPERAND_TYPE_UINT32;
    operand->size = DWORD;
    asm_operand_uint32_t *v = NEW(asm_operand_uint32_t);
    v->value = (uint32_t) rel;
    operand->value = v;
    return;
  }
  operand->type = ASM_OPERAND_TYPE_UINT8;
  operand->size = BYTE;
  asm_operand_uint8_t *v = NEW(asm_operand_uint8_t);
  v->value = (uint8_t) rel;
  operand->value = v;
}

static bool elf_is_jmp(asm_inst_t asm_inst) {
  return asm_inst.name[0] == 'j';
}

void elf_text_inst_build(asm_inst_t asm_inst) {
  uint64_t *offset = elf_current_text_offset();
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
  asm_operand_t *operand = asm_symbol_operand(asm_inst);
  if (operand != NULL) {
    // 1. 数据符号引用(直接改写成 0x0(rip))
    // 2. 标签符号引用(在符号表中,表明为内部符号,否则使用 rel32 占位)
    asm_operand_symbol_t *symbol_operand = operand->value;
    if (symbol_operand->is_label) {
      if (table_exist(elf_symbol_table, symbol_operand->name)) {
        elf_symbol_t *symbol = table_get(elf_symbol_table, symbol_operand->name);
        // 计算 offset 并填充
        int rel_diff = global_text_offset - *symbol->offset;
        elf_fn_operand_rewrite(operand, rel_diff);
      } else {
        // 引用了 label 符号，但是符号确不在符号表中
        // 此时使用 rel32 占位，如果是 jmp 指令后续可能需要替换
        elf_fn_operand_rewrite(operand, 0);
        inst->rel_operand = operand;
        inst->rel_symbol = symbol_operand->name;
        inst->is_jmp_symbol = elf_is_jmp(asm_inst);
      }
    } else {
      // 数据符号重写(symbol to 0(%rip))
      elf_var_operand_rewrite(operand);

      // 添加到重定位表
      elf_rel_t *rel = NEW(elf_rel_t);
      rel->name = symbol_operand->name;
      rel->offset = offset + 2; // 引用的具体位置
      rel->addend = -4;
      rel->section = ELF_SECTION_TEXT;
      rel->type = ELF_SYMBOL_TYPE_VAR;
      list_push(elf_rel_list, rel);
    }
  }

  inst->data = opcode_encoding(asm_inst, &inst->count);
  global_text_offset += inst->count; // advance global offset

  inst->offset = offset;
  inst->asm_inst = asm_inst;

  // 注册 elf_text_inst_t 到 elf_text_inst_list 和 elf_text_table 中
  list_push(elf_text_inst_list, inst);
}

/**
 * 指令重写， jmp/jcc rel32 重写为 rel8
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
    if (global_text_offset - (find_count * 3) - *inst->offset > 128) {
      break;
    }

    if (inst->is_jmp_symbol && strequal(inst->rel_symbol, name)) {
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
    if (inst->is_jmp_symbol != NULL && strequal(inst->rel_symbol, name)) {
      // 重写 inst 指令 rel32 to rel8
      // jmp 的具体位置可以不计算，等到二次遍历再计算
      // 届时符号表已经全部收集完毕
      elf_rewrite_text_rel(inst);
      *inst->offset = *offset; // 值替换，而不是指针地址替换
    }

    *offset += inst->count; // 重新计算 offset
    current = current->next;
  }

  // 最新的 offset
  global_text_offset = *offset;
}

/**
 * inst rel32 to rel8
 * @param t
 */
void elf_rewrite_text_rel(elf_text_inst_t *t) {
  asm_operand_uint8_t *operand = NEW(asm_operand_uint8_t);
  operand->value = 0; // 仅占位即可
  t->asm_inst.count = 1;
  t->asm_inst.operands[0]->type = ASM_OPERAND_TYPE_UINT8;
  t->asm_inst.operands[0]->size = BYTE;
  t->asm_inst.operands[0]->value = operand;
  t->data = opcode_encoding(t->asm_inst, &t->count);
  t->rel_symbol = NULL;
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
      inst->data = opcode_encoding(inst->asm_inst, &inst->count);
    } else {
      // 二次扫描都没有在符号表中找到，说明引用了外部符号(是否需要区分引用的外部符号的类型不同？section 填写的又是什么段？)
      elf_rel_t *rel = NEW(elf_rel_t);
      rel->name = inst->rel_symbol;
      rel->offset = inst->offset;
      rel->addend = 0; // 符号表如果都使用 rip,则占 4 个偏移
      rel->section = ELF_SECTION_TEXT;
      rel->type = ELF_SYMBOL_TYPE_FN;
      list_push(elf_rel_list, rel);

      // 重定位表
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

void elf_text_inst_list_build(list *inst_list) {
  if (list_empty(inst_list)) {
    return;
  }
  list_node *current = inst_list->front;
  while (current->value != NULL) {
    asm_inst_t *inst = current->value;
    elf_text_inst_build(*inst);
  }
}

void elf_symbol_insert(elf_symbol_t *symbol) {
  table_set(elf_symbol_table, symbol->name, symbol);
  list_push(elf_symbol_list, symbol);
}

uint64_t *elf_current_text_offset() {
  uint64_t *offset = NEW(uint64_t);
  *offset = global_text_offset;
  return offset;
}

uint64_t *elf_current_data_offset() {
  uint64_t *offset = NEW(uint64_t);
  *offset = global_data_offset;
  return offset;
}

static char *elf_header_ident() {
  char *ident = malloc(sizeof(char) * EI_NIDENT);
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

elf_t elf_new() {
  // 数据段构建(依旧是遍历符号表)
  uint64_t data_size = 0;
  uint8_t *data = elf_data_build(&data_size);

  // 代码段构建 .text
  uint64_t text_size = 0;
  uint8_t *text = elf_text_build(&text_size);

  // 符号表构建(首先计算符号的数量)
  uint64_t symtab_count = 3;
  list_node *current = elf_symbol_list->front;
  while (current->value != NULL) {
    elf_symbol_t *s = current->value;
    if (!s->is_local) {
      symtab_count++;
    }
    current = current->next;
  }
  Elf64_Sym *symtab = malloc(sizeof(Elf64_Sym) * symtab_count);
  string strtab = elf_symtab_build(symtab);

  // 代码段重定位表构建
  uint64_t rel_text_count;
  Elf64_Rela *rel_text = elf_rela_text_build(&rel_text_count);

  // 段表构建
  Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr) * SHDR_COUNT);
  string shstrtab = elf_shdr_build(text_size,
                                   data_size,
                                   symtab_count * sizeof(Elf64_Sym),
                                   strlen(strtab),
                                   rel_text_count * sizeof(Elf64_Rela),
                                   shdr);

  // 文件头构建
  Elf64_Ehdr ehdr = {
      .e_ident = elf_header_ident(),
      .e_type = ET_REL, // elf 文件类型 = 可重定位文件
      .e_machine = EM_X86_64,
      .e_version = EV_CURRENT,
      .e_entry = 0, // elf 文件程序入口的线性绝对地址，一般用于可执行文件，可重定位文件配置为 0 即可
      .e_phoff = 0, // 程序头表在文件中的偏移，对于可重定位文件来说，值同样为 0，
      .e_shoff = sizeof(Elf64_Ehdr) + text_size + strlen(shstrtab), // 段表在文件中偏移地址，现在还不能计算出来
      .e_flags = 0, // elf 平台相关熟悉，设置为 0 即可
      .e_ehsize = sizeof(Elf64_Ehdr), // 文件头表的大小
      .e_phentsize = 0, // 程序头表项的大小, 可重定位表没有这个头
      .e_phnum = 0, // 程序头表项, 这个只能是 0
      .e_shentsize = sizeof(Elf64_Shdr), // 段表项的大小
      .e_shstrndx = SHSTRTAB_INDEX,
  };

  // 输出二进制
  return (elf_t) {
      .ehdr = ehdr,
      .text = text,
      .text_size = text_size,
      .data = data,
      .data_size = data_size,
      .shstrtab = shstrtab,
      .shdr = shdr,
      .shdr_count = SHDR_COUNT,
      .symtab = symtab,
      .symtab_count = symtab_count,
      .strtab = strtab,
      .rela_text = rel_text,
      .real_text_count = rel_text_count
  };
}

/**
 * 包含的项：
 * .text/.data/.rel.text/.shstrtab/.symtab/.strtab
 * @param text_size
 * @param symtab_size
 * @param strtab_size
 * @param rel_text_size
 * @return
 */
char *elf_shdr_build(uint64_t text_size,
                     uint64_t data_size,
                     uint64_t symtab_size,
                     uint64_t strtab_size,
                     uint64_t rela_text_size,
                     Elf64_Shdr *shdr) {

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
  // 符号表偏移
  uint64_t text_offset = offset;
  offset += text_size;
  // 数据段在文件中的偏移
  uint64_t data_offset = offset;
  offset += data_size;
  // 段表字符串表偏移
  uint64_t shstrtab_offset = offset;
  offset += strlen(shstrtab_data);
  // 段表偏移
  uint64_t shdr_offset = offset;
  offset += SHDR_COUNT * sizeof(Elf64_Shdr);
  // 符号表偏移
  uint64_t symtab_offset = offset;
  offset += symtab_size;
  // 字符串表偏移
  uint64_t strtab_offset = offset;
  offset += strtab_size;
  // 重定位表偏移
  uint64_t rela_text_offset = offset;
  offset += rela_text_size;



//  Elf64_Shdr **section_table = malloc(sizeof(Elf64_Shdr) * 5);

  // 空段
  shdr[0] = (Elf64_Shdr) {
      .sh_name = 0,
      .sh_type = 0, // 表示程序段
      .sh_flags = 0,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = 0,
      .sh_size = 0,
      .sh_link = 0,
      .sh_info = 0,
      .sh_addralign = 0,
      .sh_entsize = 0
  };

  // 代码段
  shdr[1] = (Elf64_Shdr) {
      .sh_name = text_name,
      .sh_type = SHT_PROGBITS, // 表示程序段
      .sh_flags = SHF_ALLOC | SHF_EXCLUDE,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = text_offset,
      .sh_size = text_size,
      .sh_link = 0,
      .sh_info = 0,
      .sh_addralign = 1,
      .sh_entsize = 0
  };

  // 代码段重定位表
  shdr[2] = (Elf64_Shdr) {
      .sh_name = rela_text_name,
      .sh_type = SHT_RELA, // 表示程序段
      .sh_flags = SHF_INFO_LINK,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = rela_text_offset,
      .sh_size = rela_text_size,
      .sh_link = 4,
      .sh_info = 1,
      .sh_addralign = 8,
      .sh_entsize = sizeof(Elf64_Rel)
  };

  // 数据段
  shdr[3] = (Elf64_Shdr) {
      .sh_name = data_name,
      .sh_type = SHT_PROGBITS, // 表示程序段
      .sh_flags =  SHF_ALLOC | SHF_WRITE,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = data_offset,
      .sh_size = data_size,
      .sh_link = 0,
      .sh_info = 0,
      .sh_addralign = 4,
      .sh_entsize = 0
  };

  // 符号表段
  shdr[4] = (Elf64_Shdr) {
      .sh_name = symtab_name,
      .sh_type = SHT_SYMTAB, // 表示程序段
      .sh_flags =  0,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = symtab_offset,
      .sh_size = symtab_size,
      .sh_link = 5,
      .sh_info = SYMTAB_LAST_LOCAL_INDEX + 1, // 符号表最后一个 local 符号的索引
      .sh_addralign = 8,
      .sh_entsize = sizeof(Elf64_Sym)
  };

  // 字符串串表 5
  shdr[5] = (Elf64_Shdr) {
      .sh_name = strtab_name,
      .sh_type = SHT_STRTAB, // 表示程序段
      .sh_flags =  0,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = strtab_offset,
      .sh_size = strtab_size,
      .sh_link = 0,
      .sh_info = 0,
      .sh_addralign = 1,
      .sh_entsize = 0,
  };


  // 段表字符串表 6
  shdr[6] = (Elf64_Shdr) {
      .sh_name = shstrtab_name,
      .sh_type = SHT_STRTAB, // 表示程序段
      .sh_flags =  0,
      .sh_addr = 0, // 可执行文件才有该地址
      .sh_offset = shstrtab_offset,
      .sh_size = strlen(shstrtab_data),
      .sh_link = 0,
      .sh_info = 0,
      .sh_addralign = 1,
      .sh_entsize = 0,
  };

  return shstrtab_data;
}

char *elf_symtab_build(Elf64_Sym *symbol) {
  // 计算需要添加仅符号表的符号的数量(rel/全局 var/外部的 label)
//  int size = 3;
//  list_node *current = elf_symbol_list->front;
//  while (current->value != NULL) {
//    elf_symbol_t *s = current->value;
//    if (!s->is_local) {
//      size++;
//    }
//    current = current->next;
//  }

  // 内部初始化
//  symbol = malloc(sizeof(symbol) * size);
  int count = 0;

  // 字符串表
  char *strtab_data = "\0";

  // 0: NULL
  symbol[count++] = (Elf64_Sym) {
      .st_name = 0, // 字符串表的偏移
      .st_value = 0, // 符号相对于所在段基址的偏移
      .st_size = 0, // 符号的大小，单位字节
      .st_info = ELF64_ST_INFO(ELF64_ST_BIND(STB_LOCAL), ELF64_ST_TYPE(STT_NOTYPE)),
      .st_other = 0,
      .st_shndx = 0, // 符号所在段，在段表内的索引
  };

  // 1: file
  symbol[count++] = (Elf64_Sym) {
      .st_name = strlen(strtab_data),
      .st_value = 0,
      .st_size = 0,
      .st_info = ELF64_ST_INFO(ELF64_ST_BIND(STB_LOCAL), ELF64_ST_TYPE(STT_FILE)),
      .st_other = 0,
      .st_shndx = SHN_ABS,
  };
  strtab_data = str_connect(strtab_data, filename);
  strtab_data = str_connect(strtab_data, "\0");

  // 2: section: 1 = .text
  symbol[count++] = (Elf64_Sym) {
      .st_name = 0,
      .st_value = 0,
      .st_size = 0,
      .st_info = ELF64_ST_INFO(ELF64_ST_BIND(STB_LOCAL), ELF64_ST_TYPE(STT_SECTION)),
      .st_other = 0,
      .st_shndx = TEXT_INDEX,
  };

  // 3: section: 3 = .data
  symbol[count++] = (Elf64_Sym) {
      .st_name = 0,
      .st_value = 0,
      .st_size = 0,
      .st_info = ELF64_ST_INFO(ELF64_ST_BIND(STB_LOCAL), ELF64_ST_TYPE(STT_SECTION)),
      .st_other = 0,
      .st_shndx = DATA_INDEX,
  };

  // 4. 填充其余符号(list 遍历)
  list_node *current = elf_symbol_list->front;
  while (current->value != NULL) {
    elf_symbol_t *s = current->value;
    if (!s->is_local) {
      Elf64_Sym sym = {
          .st_name = strlen(strtab_data),
          .st_value = *s->offset,
          .st_size = s->size,
          .st_info = ELF64_ST_INFO(ELF64_ST_BIND(STB_GLOBAL), ELF64_ST_TYPE(s->type)),
          .st_other = 0,
          .st_shndx = s->section,
      };
      int index = count++;
      symbol[index] = sym;
      s->symtab_index = index;
      strtab_data = str_connect(strtab_data, s->name);
      strtab_data = str_connect(strtab_data, "\0");
    }
    current = current->next;
  }

  return strtab_data;
}

Elf64_Rela *elf_rela_text_build(uint64_t *count) {
  Elf64_Rela *r = malloc(sizeof(Elf64_Rela) * elf_rel_list->count);
  *count = elf_rel_list->count;
  list_node *current = elf_rel_list->front;
  int i = 0;
  while (current->value != NULL) {
    elf_rel_t *rel = current->value;
    elf_symbol_t *s = table_get(elf_symbol_table, rel->name);
    uint64_t index = s->symtab_index;
    // r_sym 表示重定位项在符号表内的索引(?)
    r[i] = (Elf64_Rela) {
        .r_offset = *rel->offset,
        .r_info = ELF64_R_INFO(ELF64_R_SYM(index), ELF64_R_TYPE(R_X86_64_PC32)),
        .r_addend = rel->addend,
    };
  }

  return r;
}

uint8_t *elf_encoding(elf_t elf, uint64_t *count) {
  *count = sizeof(Elf64_Ehdr) +
      elf.data_size +
      elf.text_size +
      sizeof(elf.shstrtab) +
      sizeof(Elf64_Shdr) * elf.shdr_count +
      sizeof(Elf64_Sym) * elf.symtab_count +
      sizeof(elf.strtab) +
      sizeof(Elf64_Rela) * elf.real_text_count;
  uint8_t *binary = malloc(*count);

  // 文件头
  uint8_t *p = binary;
  memcpy(p, &elf.ehdr, sizeof(Elf64_Ehdr));
  p += sizeof(Elf64_Ehdr);

  // 代码段
  memcpy(p, elf.text, elf.text_size);
  p += elf.text_size;

  // 符号表段
  memcpy(p, elf.data, elf.data_size);
  p += elf.data_size;

  // 段表字符串表
  memcpy(p, elf.shstrtab, sizeof(elf.shstrtab));
  p += sizeof(elf.shstrtab);

  // 段表
  memcpy(p, elf.shdr, sizeof(Elf64_Shdr) * elf.shdr_count);
  p += sizeof(Elf64_Shdr) * elf.shdr_count;

  // 符号表
  memcpy(p, elf.symtab, sizeof(Elf64_Sym) * elf.symtab_count);
  p += sizeof(Elf64_Sym) * elf.symtab_count;

  // 字符串表
  memcpy(p, elf.strtab, sizeof(elf.strtab));
  p += sizeof(elf.strtab);

  // 重定位表
  memcpy(p, elf.rela_text, sizeof(Elf64_Rela) * elf.real_text_count);
  p += sizeof(Elf64_Rela) * elf.real_text_count;

  return binary;
}

void elf_to_file(uint8_t *binary, uint64_t count, char *target_filename) {
  FILE *f = fopen(target_filename, "w+b");
  fwrite(binary, 1, count, f);
  fclose(f);
}

uint8_t *elf_text_build(uint64_t *size) {
  *size = elf_text_inst_list->count;
  uint8_t *text = malloc(sizeof(uint8_t) * *size);
  if (*size == 0) {
    return text;
  }

  uint8_t *p = text;

  list_node *current = elf_text_inst_list->front;
  while (current->value != NULL) {
    elf_text_inst_t *inst = current->value;
    memcpy(p, inst->data, inst->count);
    p += inst->count;
  }

  return text;
}

/**
 * 写入 custom 符号表即可
 * @param decl
 */
void elf_var_decl_build(asm_var_decl decl) {
  elf_symbol_t *symbol = NEW(elf_symbol_t);
  symbol->name = decl.name;
  symbol->type = ELF_SYMBOL_TYPE_VAR;
  symbol->section = ELF_SECTION_DATA;
  symbol->offset = elf_current_data_offset();
  global_data_offset += decl.size;
  symbol->size = decl.size;
  symbol->value = decl.value;
  symbol->is_rel = false;
  symbol->is_local = false; // data 段的都是全局符号，可以被其他文件引用
  elf_symbol_insert(symbol);
  elf_confirm_text_rel(symbol->name);
}

void elf_var_decl_list_build(list *decl_list) {
  if (list_empty(decl_list)) {
    return;
  }
  list_node *current = decl_list->front;
  while (current->value != NULL) {
    asm_var_decl *inst = current->value;
    elf_var_decl_build(*inst);
  }
}

uint8_t elf_data_build(uint64_t *size) {
  // 遍历符号表计算数量并申请内存
  list_node *current = elf_symbol_list->front;
  while (current->value != NULL) {
    elf_symbol_t *t = current->value;
    if (t->type != ELF_SYMBOL_TYPE_VAR) {
      continue;
    }

    *size += t->size;
  }
  uint8_t *data = malloc(*size);
  uint8_t *p = data;

  current = elf_symbol_list->front;
  while (current->value != NULL) {
    elf_symbol_t *symbol = current->value;
    if (symbol->type != ELF_SYMBOL_TYPE_VAR) {
      continue;
    }

    if (symbol->value != NULL) {
      memcpy(p, symbol->value, symbol->size);
    }
    p += symbol->size;
  }

  return 0;
}



#include "opcode.h"
#include "string.h"
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
void opcode_init() {
  // 收集所有指令，进行注册
  opcode_tree_build(&mov_rm8_r8);
  opcode_tree_build(&mov_r16_rm16);
  opcode_tree_build(&push_r64);
  // .... TODO
}

/**
 * @param type
 * @param byte
 * @return
 */
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
  // 表示已经找到头了，甚至有可能溢出
  if (operand.type == 0) {
    node->insts.list[node->insts.count++] = inst;
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

inst_t *opcode_select(asm_inst_t asm_inst) {
  opcode_tree_node_t *current = table_get(opcode_tree_root->succs, asm_inst.name);
  if (current == NULL) {
    error_exit(0, "cannot identify asm opcode %s ", asm_inst.name);
    return NULL;
  }

  for (int i = 0; i < asm_inst.count; ++i) {
    asm_operand_t *operand = asm_inst.operands[i];
    // 生成 key
    string key = itoa(asm_operand_to_key(operand->type, operand->size));

    // current 匹配
    bool exists = table_exist(current->succs, key);
    if (!exists) {
      error_exit(0, "cannot identify asm opcode %s with operands", asm_inst.name);
      return NULL;
    }
    current = table_get(current->succs, key);
  }

  insts_t insts = current->insts;
  opcode_sort_insts(&insts);

  return insts.list[0];
}

void opcode_sort_insts(insts_t *insts) {
  if (insts->count == 0) {
    return;
  }

  for (int i; i < insts->count - 1; ++i) {
    bool change = false;
    for (int j = 0; j < insts->count - 1 - i; ++j) {
      if (insts->list[j]->operands[0].type > insts->list[j + 1]->operands[0].type) {
        inst_t *temp = insts->list[j];
        insts->list[j] = insts->list[j + 1];
        insts->list[j + 1] = temp;
        change = true;
      }
    }

    if (!change) {
      break;
    }
  }
}

static rex_prefix_t *new_rex_prefix() {
  rex_prefix_t *r = NEW(rex_prefix_t);
  r->b = false;
  r->r = false;
  r->w = false;
  r->x = false;
  return r;
}

static vex_prefix_t *new_vex_prefix() {
  vex_prefix_t *v = NEW(vex_prefix_t);
  v->source = 0;
  v->vex_legacy_byte = 0;
  v->vex_opcode_extension = 0;
  v->l = false;
  v->r = true;
  v->w = true;
  v->x = true;
  v->b = true;
  return v;
}

static modrm_t *new_modrm() {
  modrm_t *m = NEW(modrm_t);
  m->mod = 0;
  m->reg = 0;
  m->rm = 0;
  return m;
}

static void parser_ext(inst_format_t *format, opcode_ext ext) {
  if (ext == OPCODE_EXT_SLASH0) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 0;
  } else if (ext == OPCODE_EXT_SLASH1) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 1;
  } else if (ext == OPCODE_EXT_SLASH2) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 2;
  } else if (ext == OPCODE_EXT_SLASH3) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 3;
  } else if (ext == OPCODE_EXT_SLASH4) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 4;
  } else if (ext == OPCODE_EXT_SLASH5) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 5;
  } else if (ext == OPCODE_EXT_SLASH6) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 6;
  } else if (ext == OPCODE_EXT_SLASH7) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
    format->modrm->reg = 7;
  } else if (ext == OPCODE_EXT_SLASHR) {
    if (format->modrm == NULL) {
      format->modrm = new_modrm();
    }
  } else if (ext == OPCODE_EXT_REX_W) {
    if (format->rex_prefix == NULL) {
      format->rex_prefix = new_rex_prefix();
    }
    format->rex_prefix->w = true;
  } else if (ext == OPCODE_EXT_REX) {
    if (format->rex_prefix == NULL) {
      format->rex_prefix = new_rex_prefix();
    }
  } else if (ext == OPCODE_EXT_VEX_128) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
  } else if (ext == OPCODE_EXT_VEX_256) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->l = true;
  } else if (ext == OPCODE_EXT_VEX_66) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_66;
  } else if (ext == OPCODE_EXT_VEX_F2) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_F2;
  } else if (ext == OPCODE_EXT_VEX_F3) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_OPCODE_EXT_F3;
  } else if (ext == OPCODE_EXT_VEX_0F) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F;
  } else if (ext == OPCODE_EXT_VEX_0F_38) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F_38;
  } else if (ext == OPCODE_EXT_VEX_0F_3A) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->vex_opcode_extension = VEX_LEGACY_BYTE_0F_3A;
  } else if (ext == OPCODE_EXT_VEX_W0) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->w = false;
  } else if (ext == OPCODE_EXT_VEX_W1) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->w = true;
  } else if (ext == OPCODE_EXT_VEX_WIG) {
    if (format->vex_prefix == NULL) {
      format->vex_prefix = new_vex_prefix();
    }
    format->vex_prefix->w = false;
  }
}

static void set_disp(inst_format_t *format, string reg, uint8_t disps[], uint8_t count) {
  // 特殊 register 处理
  int j = 0;
  if (strcmp(reg, "rsp") == 0) {
    format->disps[j++] = 0x24;
  }

  for (int i = 0; i < count; i++) {
    format->disps[j++] = disps[i];
  };
}

static void set_imm(inst_format_t *format, uint8_t *imms, uint8_t count) {
  for (int i = 0; i < count; ++i) {
    format->imms[i] = imms[i];
  }
}

/**
 * 小端处理
 * @param src
 * @param dst
 */
static void int32_to_uint8(int32_t src, uint8_t dst[4]) {
//  memcpy(dst, &src, sizeof(src));
  dst[0] = src;
  dst[1] = src >> 8;
  dst[2] = src >> 16;
  dst[3] = src >> 24;
}

static sib_t *new_sib(uint8_t scale, uint8_t index, uint8_t base) {
  sib_t *s = NEW(sib_t);
  s->scale = scale;
  s->index = index;
  s->base = base;
  return s;
}

/**
 *
 * @param asm_inst
 * @param inst
 * @return
 */
inst_format_t *opcode_fill(inst_t *inst, asm_inst_t asm_inst) {
  inst_format_t *format = NEW(inst_format_t);
  // format 填充 prefixes
  if (inst->prefix > 0) {
    format->prefix = inst->prefix;
  }

  // format 填充 extensions
  int i = 0;
  bool ext_exists[OPCODE_EXT_EOF] = {false};
  while (inst->extensions[i] > 0) {
    opcode_ext ext = inst->extensions[i++];

    parser_ext(format, ext);
    ext_exists[ext] = true;
  }

  i = 0;
  while (inst->operands[i].type > 0) {
    opcode_operand_t operand = inst->operands[i];
    asm_operand_t *asm_operand = asm_inst.operands[i];
    // asm 参数填充
    if (asm_operand->type == ASM_OPERAND_TYPE_REGISTER) {
      asm_operand_register *r = asm_operand->value;
      if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_DIRECT_REGISTER;
        format->modrm->rm = r->index;
        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->index > 7;
        } else if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
          format->vex_prefix->b = r->index <= 7;
        }
      } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_DIRECT_REGISTER;
        format->modrm->reg = r->index;
        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->index > 7;
        } else if (ext_exists[OPCODE_EXT_VEX_128] || ext_exists[OPCODE_EXT_VEX_256]) {
          format->vex_prefix->b = r->index <= 7;
        }
      } else if (operand.encoding == ENCODING_TYPE_OPCODE_PLUS) { // opcode = opcode + reg
        format->opcode[0] += r->index & 7;

        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->index > 7;
        }
      } else if (operand.encoding == ENCODING_TYPE_VEX_VVVV) {
        if (format->vex_prefix == NULL) {
          format->vex_prefix = new_vex_prefix();
        }
        format->vex_prefix->source = 15 - r->index; // two's complement
        format->vex_prefix->r = r->index <= 7;
      } else {
        error_exit(0, "unsupported encoding %v", operand.encoding);
        return NULL;
      }

    } else if (asm_operand->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
      asm_operand_disp_register *r = asm_operand->value;
      if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
        format->modrm->rm = r->reg.index;
        // 设置 displacement 部分(disp 最多 8个字节，通过 8 字节拆分的形式传参)
        uint8_t temp[] = {r->disp};
        set_disp(format, r->reg.name, temp, 1);

        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->reg.index > 7;
        }
      } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
          format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
        }

        format->modrm->reg = r->reg.index;
        uint8_t temp[] = {r->disp};
        set_disp(format, r->reg.name, temp, 1);

        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->reg.index > 7;
        }
      } else {
        error_exit(0, "unsupported encoding %v", operand.encoding);
        return NULL;
      }

    } else if (asm_operand->type == ASM_OPERAND_TYPE_INDIRECT_REGISTER) {
      asm_operand_indirect_register *r = asm_operand->value;
      if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
        format->modrm->rm = r->reg.index;
        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->reg.index > 7;
        }
      } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
          format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
        }

        format->modrm->reg = r->reg.index;
        if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
          format->rex_prefix->b = r->reg.index > 7;
        }
      } else {
        error_exit(0, "unsupported encoding %v", operand.encoding);
        return NULL;
      }
    } else if (asm_operand->type == ASM_OPERAND_TYPE_RIP_RELATIVE) {
      asm_operand_rip_relative *r = asm_operand->value;
      if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
        format->modrm->rm = 5;

        // 32 to uint8 []
        uint8_t temp[4];
        int32_to_uint8(r->disp, temp);
        set_disp(format, "", temp, 4);
      } else if (operand.encoding == ENCODING_TYPE_MODRM_REG) {
        if (format->modrm == NULL) {
          format->modrm = new_modrm();
        }

        format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
        format->modrm->reg = 5;

        // 小端处理
        uint8_t temp[4];
        int32_to_uint8(r->disp, temp);
        set_disp(format, "", temp, 4);
      } else if (asm_operand->type == ASM_OPERAND_TYPE_SIB_REGISTER) {
        asm_operand_sib_register *r = asm_operand->value;
        if (operand.encoding == ENCODING_TYPE_MODRM_RM) {
          if (format->modrm == NULL) {
            format->modrm = new_modrm();
          }

          format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER;
          format->modrm->rm = MODRM_MOD_SIB_FOLLOWS_RM;

          format->sib = new_sib(r->scale, r->index.index, r->base.index);
          if (ext_exists[OPCODE_EXT_REX_W] || ext_exists[OPCODE_EXT_REX]) {
            format->rex_prefix->x = r->index.index > 7;
            format->rex_prefix->b = r->base.index > 7;
          }

          if (r->base.index == 13) {
            format->modrm->mod = MODRM_MOD_INDIRECT_REGISTER_BYTE_DISP;
            uint8_t temp = {0};
            set_disp(format, r->base.name, temp, 0);
          }
        }
      }
    } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT64) {
      asm_operand_uint64 *i = asm_operand->value;
      uint8_t temp[8];
      memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
      set_imm(format, temp, 8);
    } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT64) {
      asm_operand_float64 *f = asm_operand->value;
      uint8_t temp[8];
      memcpy(temp, &f->value, sizeof(f->value));
      set_imm(format, temp, 8);
    } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT32) {
      asm_operand_uint32 *i = asm_operand->value;
      uint8_t temp[4];
      memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
      set_imm(format, temp, 4);
    } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT16) {
      asm_operand_uint16 *i = asm_operand->value;
      uint8_t temp[2];
      memcpy(temp, &i->value, sizeof(i->value)); // 小端处理
      set_imm(format, temp, 2);
    } else if (asm_operand->type == ASM_OPERAND_TYPE_UINT8) {
      asm_operand_uint8 *i = asm_operand->value;
      uint8_t temp[1] = {i->value};
      set_imm(format, temp, 1);
    } else if (asm_operand->type == ASM_OPERAND_TYPE_FLOAT32) {
      asm_operand_float32 *f = asm_operand->value;
      uint8_t temp[4];
      memcpy(temp, &f->value, sizeof(f->value));
    } else if (asm_operand->type == ASM_OPERAND_TYPE_INT32) {
      asm_operand_int32 *i = asm_operand->value;
      uint8_t temp[4];
      memcpy(temp, &i->value, sizeof(i->value));
    } else {
      error_exit(0, "unsupported asm operand type %v", asm_operand->type);
      return NULL;
    }

    return NULL;
  }
}

static void opcode_vex_encoding(inst_format_t *format, uint8_t *data, uint8_t *count) {
  vex_prefix_t *v = format->vex_prefix;
  if ((v->vex_legacy_byte == 0 || v->vex_legacy_byte == VEX_LEGACY_BYTE_0F) && v->x && v->b) {
    uint8_t byte0 = 0xc5;
    uint8_t byte1 = 0;
    if (v->r) {
      byte1 = 1 << 7;
    }

    byte1 += (v->source << 3);
    if (v->l) {
      byte1 += (1 << 2);
    }

    byte1 += v->vex_opcode_extension;
    data[*count++] = byte0;
    data[*count++] = byte1;
    return;
  }

  // three byte form
  uint8_t byte0 = 0xc4;
  uint8_t byte1 = 0;
  uint8_t byte2 = 0;
  if (v->r) {
    byte1 = 1 << 7;
  }

  if (v->x) {
    byte1 += (1 << 6);
  }

  if (v->b) {
    byte1 += (1 << 5);
  }

  byte1 += v->vex_legacy_byte;

  if (v->w) {
    byte2 = (1 << 7);
  }

  byte2 += (v->source << 3);
  if (v->l) {
    byte2 += (1 << 2);
  }

  byte2 += v->vex_opcode_extension;
  data[*count++] = byte0; // count = 1
  data[*count++] = byte1; // count = 2
  data[*count++] = byte2; // count = 3
}

static void opcode_rex_encoding(inst_format_t *format, uint8_t *result) {
  *result = 0;
  if (format->rex_prefix->b) {
    *result = 1;
  }

  if (format->rex_prefix->x) {
    *result += 1 << 1;
  }

  if (format->rex_prefix->r) {
    *result += 1 << 2;
  }
  if (format->rex_prefix->w) {
    *result += 1 << 3;
  }

  *result += (1 << 6);
}

static void opcode_modrm_encoding(inst_format_t *format, uint8_t *result) {
  modrm_t *modrm = format->modrm;
  // &7 = &00000111 让其余未归零处理
  *result = modrm->rm & 7;
  *result |= (modrm->reg & 7) << 3;

  int mod = (uint8_t) modrm->mod;
  *result |= (mod << 6);
}

static void opcode_sib_encoding(inst_format_t *format, uint8_t *result) {
  sib_t *sib = format->sib;
  *result = sib->base & 7;
  *result |= (sib->index & 7) << 3;
  *result |= (sib->scale << 6);
}

void opcode_format_encoding(inst_format_t *format, uint8_t *data, uint8_t *count) {
  *count = 0;
  if (format->prefix > 0) {
    data[*count++] = format->prefix;
  }

  if (format->vex_prefix != NULL) {
    opcode_vex_encoding(format, data, count);
  }

  if (format->rex_prefix != NULL) {
    opcode_rex_encoding(format, &data[*count++]);
  }

  uint8_t i = 0;
  while (format->opcode[i] > 0) {
    data[*count++] = format->opcode[i++];
  }

  if (format->modrm != NULL) {
    opcode_modrm_encoding(format, &data[*count++]);
  }

  if (format->sib != NULL) {
    opcode_sib_encoding(format, &data[*count++]);
  }

  i = 0;
  while (format->disps[i] > 0) {
    data[*count++] = format->disps[i++];
  }

  i = 0;
  while (format->imms[i] > 0) {
    data[*count++] = format;
  }
}



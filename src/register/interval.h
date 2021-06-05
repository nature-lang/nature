#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir.h"
#include "src/lib/slice.h"

typedef enum {
  LOOP_DETECTION_FLAG_VISITED,
  LOOP_DETECTION_FLAG_ACTIVE,
  LOOP_DETECTION_FLAG_NULL,
} loop_detection_flag;

typedef struct {
  int index; // 根据平台不同而不同，比如intel 0~40
  string name;
  string type; // int/float
} reg;

typedef struct {
  int from;
  int to;
} interval_range;

// interval 分为两种，一种是虚拟寄存器，一种是固定寄存器
typedef struct interval {
  int first_from;
  int last_to;
  slice *ranges;
  slice *use_positions;
  struct interval *split_parent;
  slice *split_children; // 动态数组

  lir_operand_var *var; // 变量名称
  reg *reg;
  reg *assigned;

  bool fixed; // 是否是固定寄存器,固定寄存器有 reg 没有 var
} interval;

void interval_loop_detection(closure *c);
void interval_block_order(closure *c);
void interval_mark_number(closure *c);
void interval_build(closure *c);
interval *interval_new(lir_operand_var *var);
void interval_add_range(closure *c, lir_operand_var *var, int from, int to);
void interval_add_first_range_from(closure *c, lir_operand_var *var, int from);
void interval_add_use_position(closure *c, lir_operand_var *var, int position);
void interval_split_interval();
bool interval_is_covers(interval *i, int position);
int interval_next_intersection(interval *current, interval *other);

#endif

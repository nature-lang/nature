#include "allocate.h"

void allocate(closure *c) {
  list *unhandled = init_unhandled(c);
  slice *active = slice_new();
  slice *inactive = slice_new();
}

list *init_unhandled(closure *c) {
  list *unhandled = list_new();
  // 遍历所有变量
  for (int i = 0; i < c->globals.count; ++i) {
    void *raw = table_get(c->interval_table, c->globals.list[i]->ident);
    if (raw == NULL) {
      continue;
    }
    interval *item = (interval *) raw;
    to_unhandled(unhandled, item);
  }
  // 遍历所有固定寄存器
  for (int i = 0; i < c->fixed_regs.count; ++i) {
    void *raw = table_get(c->interval_table, c->fixed_regs.list[i]->ident);
    if (raw == NULL) {
      continue;
    }
    interval *item = (interval *) raw;
    to_unhandled(unhandled, item);
  }

  return unhandled;
}
void to_unhandled(list *unhandled, interval *i) {
//  current = li
//  while (unhandled)
}


#include "allocate.h"

static void handle_active(allocate *a) {
  int position = a->current->first_from;
  list_node *active_curr = a->active->front;
  list_node *active_prev = NULL;
  while (active_curr->value != NULL) {
    interval *select = (interval *) active_curr->value;
    bool is_expired = select->last_to < position;
    bool is_covers = interval_is_covers(select, position);

    if (!is_covers || is_expired) {
      if (active_prev == NULL) {
        a->active->front = active_curr->next;
      } else {
        active_prev->next = active_curr->next;
      }
      a->active->count--;
      if (is_expired) {
        list_push(a->handled, select);
      } else {
        list_push(a->inactive, select);
      }
    }

    active_prev = active_curr;
    active_curr = active_curr->next;
  }
}

static void handle_inactive(allocate *a) {
  int position = a->current->first_from;
  list_node *inactive_curr = a->inactive->front;
  list_node *inactive_prev = NULL;
  while (inactive_curr->value != NULL) {
    interval *select = (interval *) inactive_curr->value;
    bool is_expired = select->last_to < position;
    bool is_covers = interval_is_covers(select, position);

    if (is_covers || is_expired) {
      if (inactive_prev == NULL) {
        a->inactive->front = inactive_curr->next;
      } else {
        inactive_prev->next = inactive_curr->next;
      }
      a->inactive->count--;
      if (is_expired) {
        list_push(a->handled, select);
      } else {
        list_push(a->active, select);
      }
    }

    inactive_prev = inactive_curr;
    inactive_curr = inactive_curr->next;
  }
}

void allocate_walk(closure *c) {
  allocate *a = malloc(sizeof(allocate));
  a->unhandled = init_unhandled(c);
  a->handled = list_new();
  a->active = list_new();
  a->inactive = list_new();

  while (a->unhandled->count != 0) {
    a->current = (interval *) list_pop(a->unhandled);
    // handle active
    handle_active(a);
    // handle inactive
    handle_inactive(a);

    // 尝试为 current 分配寄存器
    bool allocated = allocate_free_reg(a);
    if (allocated) {
      list_push(a->active, a->current);
      continue;
    }

    allocated = allocate_block_reg(a);
    if (allocated) {
      list_push(a->active, a->current);
    }
  }
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

void to_unhandled(list *unhandled, interval *to) {
  // 从头部取出,最小的元素
  list_node *temp = unhandled->front;
  list_node *prev = NULL;
  while (temp->value != NULL && ((interval *) temp->value)->first_from < to->first_from) {
    prev = temp;
    temp = temp->next;
  }

  // temp is rear
  if (temp->value == NULL) {
    list_node *empty = list_new_node();
    temp->value = to;
    temp->next = empty;
    unhandled->rear = empty;
    unhandled->count++;
    return;
  }

  // temp->value->first_from > await_to->first_from > prev->value->first_from
  list_node *await_node = list_new_node();
  await_node->value = to;
  await_node->next = temp;
  unhandled->count++;
  if (prev != NULL) {
    prev->next = await_node;
  }
}


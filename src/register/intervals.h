#ifndef NATURE_SRC_REGISTER_INTERVALS_H_
#define NATURE_SRC_REGISTER_INTERVALS_H_

#include "src/lir.h"

typedef enum {
  LOOP_DETECTION_FLAG_VISITED,
  LOOP_DETECTION_FLAG_ACTIVE,
  LOOP_DETECTION_FLAG_NULL,
} loop_detection_flag;

// 队列形式,尾进头出
typedef struct work_node {
  lir_basic_block *block;
  struct work_node *next;
} list_block_node; // 单向链表结构

void intervals_loop_detection(closure *c);

#endif

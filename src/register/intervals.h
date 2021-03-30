#ifndef NATURE_SRC_REGISTER_INTERVALS_H_
#define NATURE_SRC_REGISTER_INTERVALS_H_

#include "src/lir.h"

typedef enum {
  LOOP_DETECTION_FLAG_VISITED,
  LOOP_DETECTION_FLAG_ACTIVE,
  LOOP_DETECTION_FLAG_NULL,
} loop_detection_flag;

void intervals_loop_detection(closure *c);
void intervals_block_order(closure *c);

#endif

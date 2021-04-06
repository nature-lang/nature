#ifndef NATURE_SRC_LIB_SLICE_H_
#define NATURE_SRC_LIB_SLICE_H_
#include <stdlib.h>

typedef struct {
  int count;
  int capacity;
  void *take;
} slice;

slice *slice_new();
void slice_insert(slice *s, void *value);
void slice_free(slice *s);

#endif //NATURE_SRC_LIB_SLICE_H_

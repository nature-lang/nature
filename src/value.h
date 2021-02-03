#ifndef NATURE_SRC_VALUE_H_
#define NATURE_SRC_VALUE_H_
#include <stdlib.h>

#define string char *

#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity)*2)

#endif //NATURE_SRC_VALUE_H_

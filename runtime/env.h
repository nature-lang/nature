#ifndef NATURE_ENV_H
#define NATURE_ENV_H

#include "src/lib/table.h"

table *env_table;

void env_new(char *env_name, int capacity);

#endif //NATURE_ENV_H

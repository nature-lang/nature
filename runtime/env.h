#ifndef NATURE_ENV_H
#define NATURE_ENV_H

#include "utils/table.h"

table *env_table;

void env_new(char *env_name, int capacity);

void set_env(char *env_name, int index, void *value);

void *get_env(char *env_name, int index);

#endif //NATURE_ENV_H

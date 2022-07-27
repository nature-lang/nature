#include "env.h"

void env_new(char *env_name, int capacity) {
    if (env_table == NULL) {
        env_table = table_new();
    }
}

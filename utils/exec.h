#ifndef NATURE_EXEC_H
#define NATURE_EXEC_H

#include <stdlib.h>
#include <stdint.h>
#include "slice.h"

void exec_process(char *work_dir, char *file, slice_t *list);

int exec_imm(char *work_dir, char *file, slice_t *list);

char *exec(char *work_dir, char *file, slice_t *list, int32_t *pid, int32_t *status);

char *command_output(const char *work_dir, const char *command);

#endif //NATURE_EXEC_H

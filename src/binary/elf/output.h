#ifndef NATURE_OUTPUT_H
#define NATURE_OUTPUT_H

#include "linker.h"

#ifndef O_BINARY
# define O_BINARY 0
#endif

/**
 * 包含可重定位文件和可执行文件的输出
 */
void output_executable_file(linker_t *l, char *filename, uint64_t file_offset);

#endif //NATURE_OUTPUT_H

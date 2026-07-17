#ifndef NATURE_LD_OUTPUT_H
#define NATURE_LD_OUTPUT_H

#include "ld.h"

#include <stddef.h>
#include <stdint.h>

int ld_write_file_atomic(const ld_options_t *options, const char *path,
                         const uint8_t *data, size_t size, bool executable);
int ld_write_output_atomic(const ld_options_t *options, const uint8_t *image,
                           size_t size);

#endif

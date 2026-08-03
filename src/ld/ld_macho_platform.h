#ifndef NATURE_LD_MACHO_PLATFORM_H
#define NATURE_LD_MACHO_PLATFORM_H

#include "ld_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool seen;
    uint32_t platform;
    uint32_t minimum_version;
    uint32_t sdk_version;
} ld_macho_platform_info_t;

/* Parse and validate one platform load command.  Commands unrelated to the
   deployment target leave *recognized false. */
int ld_macho_platform_parse_command(ld_context_t *ctx, const char *input_name,
                                    const uint8_t *command_bytes,
                                    size_t command_size,
                                    ld_macho_platform_info_t *info,
                                    bool *recognized);

#endif

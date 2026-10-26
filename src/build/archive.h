#ifndef NAX_BUILD_ARCHIVE_H
#define NAX_BUILD_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>

/* Write a deterministic ar archive containing the files in member_paths.
 * Members are emitted in caller-provided order with normalized metadata.
 * The archive intentionally has no external-tool symbol index: Nax's COFF
 * linker parses every member and performs its own lazy symbol registration. */
bool build_archive_write(const char *path,
                         const char *const *member_paths,
                         size_t member_count,
                         char *error,
                         size_t error_capacity);

#endif

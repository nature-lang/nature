#include "src/build/config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char executable_path[PATH_MAX];
    snprintf(executable_path, sizeof(executable_path),
             "%s/build/Debug/nature.exe", NATURE_SOURCE_DIR);

    char *root = nature_root_from_executable_path(executable_path);
    assert(root != NULL);
    assert(strcmp(root, NATURE_SOURCE_DIR) == 0);
    free(root);

    snprintf(executable_path, sizeof(executable_path), "%s/bin/nature.exe",
             NATURE_SOURCE_DIR);
    root = nature_root_from_executable_path(executable_path);
    assert(root != NULL);
    assert(strcmp(root, NATURE_SOURCE_DIR) == 0);
    free(root);

    assert(nature_root_from_executable_path(NULL) == NULL);
    assert(nature_root_from_executable_path("") == NULL);
    assert(nature_root_from_executable_path("/tmp/nature") == NULL);
    return 0;
}

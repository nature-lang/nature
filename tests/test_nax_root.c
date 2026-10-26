#include "src/build/config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char executable_path[PATH_MAX];
    snprintf(executable_path, sizeof(executable_path),
             "%s/build/Debug/nax.exe", NAX_SOURCE_DIR);

    char *root = nax_root_from_executable_path(executable_path);
    assert(root != NULL);
    assert(strcmp(root, NAX_SOURCE_DIR) == 0);
    free(root);

    snprintf(executable_path, sizeof(executable_path), "%s/bin/nax.exe",
             NAX_SOURCE_DIR);
    root = nax_root_from_executable_path(executable_path);
    assert(root != NULL);
    assert(strcmp(root, NAX_SOURCE_DIR) == 0);
    free(root);

    assert(nax_root_from_executable_path(NULL) == NULL);
    assert(nax_root_from_executable_path("") == NULL);
    assert(nax_root_from_executable_path("/tmp/nax") == NULL);
    return 0;
}

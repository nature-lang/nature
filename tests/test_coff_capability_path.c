#include "cmd/coff_capability.h"
#include "utils/helper.h"

#include <assert.h>
#include <string.h>

int main(void) {
    char result[PATH_MAX];

    assert(coff_capability_absolute_path("/repo", "/sysroot", result));
    assert(strcmp(result, "/sysroot") == 0);
    assert(coff_capability_absolute_path("/repo", "sysroot", result));
    assert(strcmp(result, "/repo/sysroot") == 0);

#ifdef __WINDOWS
    assert(coff_capability_absolute_path("C:/repo", "D:/sysroot", result));
    assert(strcmp(result, "D:/sysroot") == 0);
    assert(coff_capability_absolute_path("C:\\repo", "D:\\sysroot", result));
    assert(strcmp(result, "D:\\sysroot") == 0);
    assert(coff_capability_absolute_path("C:\\repo", "\\\\server\\share",
                                         result));
    assert(strcmp(result, "\\\\server\\share") == 0);

    char windows_path[] = "C:\\repo\\src\\main.n";
    char *windows_dir = path_dir(windows_path);
    assert(strcmp(windows_dir, "C:\\repo\\src") == 0);
    free(windows_dir);
    assert(strcmp(file_name(windows_path), "main.n") == 0);
#endif

    return 0;
}

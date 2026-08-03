#include "src/module_index.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "src/package.h"
#include "utils/helper.h"

static void test_package_instance_key(void) {
    char *left = module_key_join("/tmp/nature-package-left", "codec");
    char *right = module_key_join("/tmp/nature-package-right", "codec");

    assert(!str_equal(left, right));
    assert(str_equal(module_ident_join("shared", "codec"), "shared.codec"));
}

#ifndef _WIN32
static void write_file(char *path, char *content) {
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(content, file) >= 0);
    assert(fclose(file) == 0);
}

static void test_symlink_cycle(void) {
    char temp[] = "/tmp/nature-module-index-XXXXXX";
    char *root = mkdtemp(temp);
    assert(root);

    char *package_path = path_join(root, "package.toml");
    char *root_source = path_join(root, "shared.n");
    char *subdir = path_join(root, "sub");
    char *sub_source = path_join(subdir, "sub.n");
    char *loop = path_join(subdir, "loop");

    assert(mkdir(subdir, 0700) == 0);
    write_file(package_path, "name = \"shared\"\nversion = \"1.0.0\"\ntype = \"lib\"\n");
    write_file(root_source, "mod shared\n");
    write_file(sub_source, "fn value():int { return 1 }\n");
    assert(symlink(".", loop) == 0);

    package_unit_reset();
    package_unit_t *unit = package_unit_load(root, package_parser(package_path));
    assert(unit->units->count == 2);

    assert(unlink(loop) == 0);
    assert(unlink(sub_source) == 0);
    assert(unlink(root_source) == 0);
    assert(unlink(package_path) == 0);
    assert(rmdir(subdir) == 0);
    assert(rmdir(root) == 0);
}
#endif

int main(void) {
    test_package_instance_key();
#ifndef _WIN32
    test_symlink_cycle();
#endif
    return 0;
}

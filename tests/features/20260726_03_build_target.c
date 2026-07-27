#include "tests/test.h"

/**
 * nature build <path.n> first maps the source path to a logical source slot, then to its owning
 * module, builds that whole module plus its transitive dependencies, and looks for the single
 * valid main in the target module only
 */
/**
 * config_init only rewrites the default output name "main" to "main.exe", so a custom name
 * keeps whatever it is given. On Windows a PE binary without the .exe extension cannot be
 * executed, so the suffix has to be supplied here.
 */
static void set_build_output(char *output_name) {
    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));
#ifdef __WINDOWS
    snprintf(BUILD_OUTPUT_NAME, sizeof(BUILD_OUTPUT_NAME), "%s.exe", output_name);
#else
    strcpy(BUILD_OUTPUT_NAME, output_name);
#endif
}

static void build_entry(char *entry, char *output_name) {
    set_build_output(output_name);

    COMPILER_TRY {
        build(entry, false);
    }
    else {
        assertf(false, "build entry '%s' failed: %s", entry, (char *) test_error_msg);
    }
}

static void assert_entry_output(char *entry, char *output_name, char *expect) {
    build_entry(entry, output_name);

    char *output = exec_output();
    assertf(str_equal(output, expect), "entry '%s' output mismatch\nexpect: %s\nactual: %s", entry, expect,
            output);
}

/**
 * Expect the build to fail with exactly the given error message
 */
static void assert_entry_error(char *entry, char *output_name, char *expect) {
    set_build_output(output_name);

    COMPILER_TRY {
        build(entry, false);
        assertf(false, "entry '%s' should not build successfully", entry);
    }
    else {
        assertf(str_equal(test_error_msg, expect), "entry '%s' error mismatch\nexpect: %s\nactual: %s", entry,
                expect, (char *) test_error_msg);
    }
}

int main(void) {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // the package-root target is named after package.toml and carries mod
    assert_entry_output("buildtarget.n", "target_main", "entry:buildtarget\n");

    // the target is the whole of buildtarget.cmd, with main defined in cmd/start.n;
    // a plain path selects plat.<os>.n by the normal target priority
    assert_entry_output("cmd/cmd.n", "target_cmd_server", "entry:buildtarget.cmd serving variant\n");
    assert_entry_output("cmd/start.n", "target_cmd_start", "entry:buildtarget.cmd serving variant\n");

    // explicitly passing the variant active for the current target points at the same module
#ifdef __DARWIN
    assert_entry_output("cmd/plat.darwin.n", "target_cmd_plat",
                        "entry:buildtarget.cmd serving variant\n");
    assert_entry_error("cmd/plat.linux.n", "target_cmd_plat_linux",
                       "cmd/plat.linux.n:1:1: build entry 'cmd/plat.linux.n' is not active for target "
                       "darwin_arm64, active source is 'cmd/plat.darwin.n'\n");
#endif
#ifdef __LINUX
    assert_entry_output("cmd/plat.linux.n", "target_cmd_plat",
                        "entry:buildtarget.cmd serving variant\n");
#endif

    // a standalone file module builds only itself, a main in a dependency (buildtarget.lib) is not an entry candidate
    assert_entry_output("standalone.n", "target_standalone", "entry:buildtarget.standalone helper\n");

    // when that same buildtarget.lib is the explicit build target, its own main becomes the entry
    assert_entry_output("lib/lib.n", "target_lib", "should never be selected\n");

    // a target module without a valid main must error
    // diagnostics use the real physical path while the module ident comes from package.toml.name
    assert_entry_error("nomain/nomain.n", "target_nomain",
                       "20260726_03_build_target/nomain/nomain.n:5:15: fn 'main' is undeclared in module "
                       "buildtarget.nomain\n");

    return 0;
}

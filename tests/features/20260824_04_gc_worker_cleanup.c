#include "tests/test.h"

#ifndef __WINDOWS
#include <signal.h>
#include <sys/wait.h>

#ifdef __DARWIN
#include <libproc.h>
#include <sys/resource.h>

static uint64_t process_footprint(pid_t pid) {
    struct rusage_info_v4 usage = {0};
    int result = proc_pid_rusage(pid, RUSAGE_INFO_V4, (rusage_info_t *) &usage);
    assertf(result == 0, "proc_pid_rusage failed: %s", strerror(errno));
    return usage.ri_phys_footprint;
}
#endif

static bool wait_for_marker(FILE *output, const char *marker) {
    char line[256];
    while (fgets(line, sizeof(line), output)) {
        if (strstr(line, marker)) {
            return true;
        }
    }
    return false;
}

int main(void) {
    feature_test_build();

    int output_pipe[2];
    assertf(pipe(output_pipe) == 0, "pipe failed: %s", strerror(errno));

    pid_t child = fork();
    assertf(child >= 0, "fork failed: %s", strerror(errno));
    if (child == 0) {
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(output_pipe[1]);
        if (WORKDIR) {
            VOID chdir(WORKDIR);
        }
        execl(BUILD_OUTPUT, BUILD_OUTPUT, NULL);
        _exit(127);
    }

    close(output_pipe[1]);
    FILE *output = fdopen(output_pipe[0], "r");
    assert(output);

    bool baseline_ready = wait_for_marker(output, "gc-worker-baseline");
#ifdef __DARWIN
    uint64_t baseline = baseline_ready ? process_footprint(child) : 0;
#endif
    bool completed = baseline_ready && wait_for_marker(output, "gc-worker-done");
#ifdef __DARWIN
    uint64_t after = completed ? process_footprint(child) : 0;
#endif

    kill(child, SIGKILL);
    fclose(output);

    int status = 0;
    waitpid(child, &status, 0);
    assertf(baseline_ready, "GC worker fixture exited before reporting baseline");
    assertf(completed, "GC worker fixture exited before completing GC cycles");

#ifdef __DARWIN
    uint64_t growth = after > baseline ? after - baseline : 0;
    const uint64_t max_growth = 16 * 1024 * 1024;
    LOGF("GC worker footprint growth: %llu bytes", (unsigned long long) growth);
    assertf(growth < max_growth,
            "physical footprint grew across GC cycles: %llu bytes (limit %llu)",
            (unsigned long long) growth, (unsigned long long) max_growth);
#endif
    return 0;
}
#else
int main(void) {
    TEST_EXEC_IMM
}
#endif

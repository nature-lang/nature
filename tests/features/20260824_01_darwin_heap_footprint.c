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

    bool ready = false;
    char line[256];
    while (fgets(line, sizeof(line), output)) {
        if (strstr(line, "heap-ready")) {
            ready = true;
            break;
        }
    }

#ifdef __DARWIN
    uint64_t footprint = ready ? process_footprint(child) : 0;
#endif
    kill(child, SIGKILL);
    fclose(output);

    int status = 0;
    waitpid(child, &status, 0);
    assertf(ready, "Nature heap footprint fixture exited before reporting readiness");

#ifdef __DARWIN
    // The string churn also triggers the independent GC worker resource leak
    // tracked by #328. Leave room for that fixed per-cycle overhead while
    // detecting arena metadata retained from the historical heap peak.
    const uint64_t max_footprint = 80 * 1024 * 1024;
    assertf(footprint < max_footprint,
            "heap footprint remains too large after GC: %llu bytes (limit %llu)",
            (unsigned long long) footprint, (unsigned long long) max_footprint);
#endif
    return 0;
}
#else
int main(void) {
    TEST_EXEC_IMM
}
#endif

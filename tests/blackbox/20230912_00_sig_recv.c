#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

// pkill -USR2 main
static void fork_and_kill() {
    pid_t pid = fork();

    if (pid < 0) {
        // fork failed
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process
        sleep(3);

        execlp("pkill", "pkill", "-USR2", "main", (char *) NULL);
        // If execlp fails
        perror("execlp failed");
        exit(EXIT_FAILURE);
    } else {
//        DEBUGF("forked child process %d success", pid);
        return;
    }
}

static void test_basic() {
    fork_and_kill();
    char *raw = exec_output();
//    printf("%s", raw);
    char *str = "not received any sig, will sleep\n"
                "not received any sig, will sleep\n"
                "read sig 12 success\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
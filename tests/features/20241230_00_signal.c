#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/wait.h>

#include "tests/test.h"

#define BUFFER_SIZE 1024

// 子进程函数：发送信号
static void child_kill_process(pid_t target_pid) {
    // 等待目标进程启动
    sleep(1);

    assert(target_pid > 0);

    kill(target_pid, SIGUSR1);
    usleep(100 * 1000);
    kill(target_pid, SIGUSR2);
    usleep(100 * 1000);
    kill(target_pid, SIGTERM);
    usleep(100 * 1000);
    kill(target_pid, SIGINT);
    usleep(100 * 1000);
    kill(target_pid, SIGUSR1);
    usleep(100 * 1000);
    kill(target_pid, SIGUSR2);
    usleep(100 * 1000);
    kill(target_pid, SIGKILL);

    exit(0);
}

int main(int argc, char *argv[]) {
    feature_test_build();

    // 创建管道用于获取目标进程的输出
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    // 第一个子进程：执行目标程序
    pid_t exec_pid = fork();
    if (exec_pid == 0) {
        // exec子进程：执行目标程序
        close(fd[0]); // 关闭读端
        dup2(fd[1], STDOUT_FILENO); // 重定向stdout到管道
        close(fd[1]);

        // 修改工作目录
        if (WORKDIR) {
            chdir(WORKDIR);
        }

        // 执行目标程序
        char *argv[] = {BUILD_OUTPUT, NULL};
        execvp(BUILD_OUTPUT, argv);
        exit(EXIT_FAILURE);
    }

    // 父进程：现在我们有了exec_pid，创建信号发送子进程
    pid_t signal_pid = fork();
    if (signal_pid == -1) {
        perror("fork failed for signal process");
        exit(1);
    } else if (signal_pid == 0) {
        // 信号发送子进程
        close(fd[0]); // 关闭管道
        close(fd[1]);
        child_kill_process(exec_pid);
    }

    // 父进程：关闭写端，从管道读取输出
    close(fd[1]);

    char *buf = malloc(8192 * 2);
    if (buf == NULL) {
        perror("malloc failed");
        exit(1);
    }

    // 读取目标进程的输出
    ssize_t total_read = 0;
    ssize_t bytes_read;
    while ((bytes_read = read(fd[0], buf + total_read, 8192 * 2 - total_read - 1)) > 0) {
        total_read += bytes_read;
        if (total_read >= 8192 * 2 - 1) break;
    }
    buf[total_read] = '\0';

    close(fd[0]);

    // 等待两个子进程结束
    int exec_status, signal_status;
    waitpid(exec_pid, &exec_status, 0);
    waitpid(signal_pid, &signal_status, 0);
    char *str = dsprintf("received signal:  %d\n"
                         "received signal:  %d\n"
                         "received signal:  %d\n"
                         "received signal:  %d\n"
                         "received signal:  %d\n"
                         "received signal:  %d\n", SIGUSR1, SIGUSR2, SIGTERM, SIGINT, SIGUSR1, SIGUSR2);
    // 验证输出
    assert_string_equal(buf, str);

    free(buf);
    return 0;
}

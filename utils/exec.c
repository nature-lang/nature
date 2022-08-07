#include "exec.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>

// 结尾必须是 NULL,开头必须是重复命令
char *exec(char *file, slice_t *argv) {
    int fd[2]; // write to fd[1], read by fd[0]
    pipe(fd);

    int fid = fork();
    if (fid == 0) {
        // 子进程
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        // exec 一旦执行成功，紫禁城就会自己推出，执行失败这会返回错误
        int result = execvp(file, argv);
        exit(result);
    }
    close(fd[1]);

    char *buf = malloc(1024);
    ssize_t len = read(fd[0], buf, 1024);

    int exec_status;
    wait(&exec_status);

    return buf;
}

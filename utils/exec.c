#include "exec.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include "helper.h"

// 结尾必须是 NULL,开头必须是重复命令
char *exec(char *work_dir, char *file, slice_t *list) {
    int fd[2]; // write to fd[1], read by fd[0]
    VOID pipe(fd);

    size_t count = list->count + 2;
    char *argv[count];
    argv[0] = file;
    for (int i = 0; i < list->count; ++i) {
        argv[i + 1] = list->take[i];
    }
    argv[count - 1] = NULL;

    int fid = fork();
    if (fid == 0) {
        // 子进程
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
//        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        if (work_dir) {
            // 修改执行的工作目录
            VOID chdir(work_dir);
        }

        // exec 一旦执行成功，当前子进程就会自己推出，执行失败这会返回错误
        int result = execvp(file, argv);
        exit(result);
    }
    close(fd[1]);

    char *buf = malloc(1024);
    memset(buf, 0, 1024);

    full_read(fd[0], buf, 1024);

    int exec_status;
    wait(&exec_status);

    return buf;
}

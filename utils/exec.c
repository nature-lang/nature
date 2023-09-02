#include "exec.h"
#include <unistd.h>
#include <stdlib.h>
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
//        perror("failed message");
        exit(result);
    }

    close(fd[1]);

    char *buf = mallocz(4096);

    full_read(fd[0], buf, 4096);

    int exec_status;
    wait(&exec_status);

    // 测试时需要测试异常退出的清空, 此时 status != 0
//    if (exec_status != 0) {
//        sprintf(buf, "exec file='%s' failed", file);
//        return buf;
//    }

    return buf;
}

char *command_output(const char *work_dir, const char *command) {
    char *result = NULL;
    FILE *pipe = NULL;

    // Change the working directory
    if (chdir(work_dir) != 0) {
        perror("chdir");
        return NULL;
    }

    // Open a pipe to the command's output using popen
    pipe = popen(command, "r");
    if (!pipe) {
        perror("popen");
        return NULL;
    }

    // Read the output from the pipe into a dynamically allocated buffer
    char buffer[128];
    size_t size = 0;
    size_t capacity = 128;
    result = (char *) malloc(capacity * sizeof(char));
    if (!result) {
        perror("malloc");
        pclose(pipe);
        return NULL;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        // Check if the buffer needs to be resized
        if (size + len >= capacity) {
            capacity *= 2;
            char *temp = (char *) realloc(result, capacity * sizeof(char));
            if (!temp) {
                perror("realloc");
                free(result);
                pclose(pipe);
                return NULL;
            }
            result = temp;
        }
        // Append the read data to the result buffer
        strcpy(result + size, buffer);
        size += len;
    }

    // Close the pipe
    pclose(pipe);

    return result;
}


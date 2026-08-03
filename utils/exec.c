#include "exec.h"

#ifdef __WINDOWS

#include <direct.h>
#include <errno.h>
#include <io.h>
#include <process.h>

#include "helper.h"

static char **windows_exec_argv(char *file, slice_t *list) {
    size_t count = list->count + 2;
    char **argv = mallocz(sizeof(char *) * count);
    argv[0] = file;
    for (int i = 0; i < list->count; ++i) {
        argv[i + 1] = list->take[i];
    }
    argv[count - 1] = NULL;
    return argv;
}

static int32_t windows_spawn_wait(char *file, char **argv, int32_t *pid) {
    intptr_t raw_handle =
            _spawnvp(_P_NOWAIT, file, (const char *const *) argv);
    if (raw_handle == -1) {
        if (pid) *pid = -1;
        return -1;
    }

    HANDLE process = (HANDLE) raw_handle;
    if (pid) *pid = (int32_t) GetProcessId(process);

    DWORD exit_code = UINT32_MAX;
    if (WaitForSingleObject(process, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process, &exit_code)) {
        exit_code = UINT32_MAX;
    }
    CloseHandle(process);
    return (int32_t) exit_code;
}

void exec_process(char *work_dir, char *file, slice_t *list) {
    char **argv = windows_exec_argv(file, list);
    char *previous_dir = NULL;

    if (work_dir) {
        previous_dir = _getcwd(NULL, 0);
        if (_chdir(work_dir) != 0) {
            free(previous_dir);
            free(argv);
            return;
        }
    }

    (void) _spawnvp(_P_WAIT, file, (const char *const *) argv);

    if (previous_dir) {
        (void) _chdir(previous_dir);
        free(previous_dir);
    }
    free(argv);
}

int exec_imm(char *work_dir, char *file, slice_t *list) {
    char **argv = windows_exec_argv(file, list);
    if (work_dir && _chdir(work_dir) != 0) {
        free(argv);
        return -1;
    }

    int result = _execvp(file, (const char *const *) argv);
    free(argv);
    return result;
}

char *exec(char *work_dir, char *file, slice_t *list, int32_t *pid, int32_t *status) {
    char **argv = windows_exec_argv(file, list);
    char *previous_dir = NULL;
    FILE *capture = tmpfile();
    if (!capture) {
        free(argv);
        if (pid) {
            *pid = -1;
        }
        if (status) {
            *status = -1;
        }
        return NULL;
    }

    if (work_dir) {
        previous_dir = _getcwd(NULL, 0);
        if (_chdir(work_dir) != 0) {
            free(previous_dir);
            free(argv);
            fclose(capture);
            if (pid) {
                *pid = -1;
            }
            if (status) {
                *status = -1;
            }
            return NULL;
        }
    }

    fflush(stdout);
    int stdout_fd = _fileno(stdout);
    int saved_stdout = _dup(stdout_fd);
    if (saved_stdout < 0 || _dup2(_fileno(capture), stdout_fd) != 0) {
        if (saved_stdout >= 0) {
            _close(saved_stdout);
        }
        if (previous_dir) {
            (void) _chdir(previous_dir);
            free(previous_dir);
        }
        free(argv);
        fclose(capture);
        if (pid) {
            *pid = -1;
        }
        if (status) {
            *status = -1;
        }
        return NULL;
    }

    int32_t child_status = windows_spawn_wait(file, argv, pid);
    fflush(stdout);
    (void) _dup2(saved_stdout, stdout_fd);
    _close(saved_stdout);

    if (previous_dir) {
        (void) _chdir(previous_dir);
        free(previous_dir);
    }
    free(argv);

    if (status) {
        *status = child_status;
    }

    if (fseek(capture, 0, SEEK_END) != 0) {
        fclose(capture);
        return NULL;
    }
    long length = ftell(capture);
    if (length < 0 || fseek(capture, 0, SEEK_SET) != 0) {
        fclose(capture);
        return NULL;
    }

    char *result = mallocz((size_t) length + 1);
    size_t read_size = fread(result, 1, (size_t) length, capture);
    size_t write_index = 0;
    for (size_t read_index = 0; read_index < read_size; ++read_index) {
        if (result[read_index] == '\r' && read_index + 1U < read_size &&
            result[read_index + 1U] == '\n') {
            continue;
        }
        result[write_index++] = result[read_index];
    }
    result[write_index] = '\0';
    fclose(capture);
    return result;
}

char *command_output(const char *work_dir, const char *command) {
    char *previous_dir = NULL;
    if (work_dir) {
        previous_dir = _getcwd(NULL, 0);
        if (_chdir(work_dir) != 0) {
            free(previous_dir);
            return NULL;
        }
    }

    FILE *pipe = _popen(command, "r");
    if (!pipe) {
        if (previous_dir) {
            (void) _chdir(previous_dir);
            free(previous_dir);
        }
        return NULL;
    }

    size_t size = 0;
    size_t capacity = 128;
    char *result = mallocz(capacity);
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        while (size + len >= capacity) {
            capacity *= 2;
        }
        result = reallocator(result, capacity);
        memcpy(result + size, buffer, len);
        size += len;
        result[size] = '\0';
    }

    (void) _pclose(pipe);
    if (previous_dir) {
        (void) _chdir(previous_dir);
        free(previous_dir);
    }
    return result;
}

#else

#include <sys/wait.h>
#include <unistd.h>

#include "helper.h"

void exec_process(char *work_dir, char *file, slice_t *list) {
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
        if (work_dir) {
            // 修改执行的工作目录
            VOID chdir(work_dir);
        }

        int dev_null_fd = open("/dev/null", O_WRONLY);
        if (dev_null_fd == -1) {
            exit(1);// 打开 /dev/null 失败
        }
        dup2(dev_null_fd, STDOUT_FILENO);
        dup2(dev_null_fd, STDERR_FILENO);
        close(dev_null_fd);

        // exec 一旦执行成功，当前子进程就会自己推出，执行失败这会返回错误
        int result = execvp(file, argv);
        exit(result);
    } else if (fid > 0) {
        // 父进程
        int status;
        waitpid(fid, &status, 0);// 等待子进程执行完成
    }
}

int exec_imm(char *work_dir, char *file, slice_t *list) {
    size_t count = list->count + 2;
    char *argv[count];
    argv[0] = file;
    for (int i = 0; i < list->count; ++i) {
        argv[i + 1] = list->take[i];
    }
    argv[count - 1] = NULL;

    // 子进程
    if (work_dir) {
        // 修改执行的工作目录
        VOID chdir(work_dir);
    }

    int result = execvp(file, argv);
    exit(result);
}


// 结尾必须是 NULL,开头必须是重复命令
char *exec(char *work_dir, char *file, slice_t *list, int32_t *pid, int32_t *status) {
    int fd[2];// write to fd[1], read by fd[0]
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
        execvp(file, argv);
        exit(EXIT_FAILURE);
    }

    if (pid) {
        *pid = fid;
    }
    close(fd[1]);

    char *buf = mallocz(8192 * 2);
    full_read(fd[0], buf, 8192 * 2);

    int exec_status = 0;
    if (status == NULL) {
        status = &exec_status;
    }
    waitpid(fid, status, 0);  // 等待子进程执行完成
    // wait(status);

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
    result = (char *) mallocz(capacity * sizeof(char));
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

#endif

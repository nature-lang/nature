#include "syscall.h"
#include "fcntl.h"
#include "string.h"
#include "runtime/processor.h"
#include "errno.h"
#include "list.h"
#include "basic.h"
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

struct timespec_t {
    int64_t sec;  // seconds
    int64_t nsec; // nanoseconds
};

typedef struct {
    struct timespec_t *atim;
    struct timespec_t *mtim;
    struct timespec_t *ctim;
    uint64_t dev;
    uint64_t ino;
    uint64_t nlink;
    uint64_t rdev;
    int64_t size;
    int64_t blksize;
    int64_t blocks;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t x__pad0;
} stat_t;

n_struct_t *stat_to_n_struct(struct stat *s) {
    rtype_t *n_stat_rtype = gc_rtype(TYPE_STRUCT, 12,
                                     TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                                     TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                                     TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);

    stat_t *n_stat = runtime_malloc(n_stat_rtype->size, n_stat_rtype);
    n_stat->dev = (uint64_t) s->st_dev;
    n_stat->ino = (uint64_t) s->st_ino;
    n_stat->mode = s->st_mode;
    n_stat->nlink = (uint64_t) s->st_nlink;
    n_stat->uid = (uint32_t) s->st_uid;
    n_stat->gid = (uint32_t) s->st_gid;
    n_stat->rdev = (uint64_t) s->st_rdev;
    n_stat->size = (int64_t) s->st_size;
    n_stat->blksize = (int64_t) s->st_blksize;
    n_stat->blocks = (int64_t) s->st_blocks;
    n_stat->atim = runtime_malloc(sizeof(struct timespec_t), NULL);
    n_stat->atim->sec = (int64_t) s->st_atim.tv_sec;
    n_stat->atim->nsec = (int64_t) s->st_atim.tv_nsec;
    n_stat->mtim = runtime_malloc(sizeof(struct timespec_t), NULL);
    n_stat->mtim->sec = (int64_t) s->st_mtim.tv_sec;
    n_stat->mtim->nsec = (int64_t) s->st_mtim.tv_nsec;
    n_stat->ctim = runtime_malloc(sizeof(struct timespec_t), NULL);
    n_stat->ctim->sec = (int64_t) s->st_ctim.tv_sec;
    n_stat->ctim->nsec = (int64_t) s->st_ctim.tv_nsec;


    // Return the pointer to the allocated and filled stat_t structure
    return (n_struct_t *) n_stat;
}

// 基于 execve 进行改造
/**
 * n_list_t 中的元素是 string_t
 * @param path
 * @param argv
 * @param envp
 */
void syscall_exec(n_string_t *path, n_list_t *argv, n_list_t *envp) {
    char *p_str = string_raw(path);

    // args 转换成 char* 格式并给到 execve
    char **c_args = mallocz(sizeof(char *) * argv->length + 1);
    for (int i = 0; i < argv->length; ++i) {
        n_string_t *arg;
        list_access(argv, i, &arg);
        if (arg == NULL) {
            return;
        }

        char *c_arg = string_raw(arg);
        c_args[i] = c_arg;
    }
    c_args[argv->length] = NULL; // 最后一个元素为 NULL

    // envs
    char **c_envs = mallocz(sizeof(char *) * envp->length);
    for (int i = 0; i < envp->length; ++i) {
        n_string_t *env;
        list_access(envp, i, &env);
        if (env == NULL) {
            return;
        }

        char *c_env = string_raw(env);
        c_envs[i] = c_env;
    }
    c_envs[envp->length] = NULL;

    // 一旦调用成功,当前进程会被占用
    int result = execve(p_str, c_args, c_envs);
    if (result == -1) {
        rt_processor_attach_errort(strerror(errno));
        return;
    }

    rt_processor_attach_errort("execve failed");
}

n_struct_t *syscall_stat(n_string_t *filename) {
    char *f_str = string_raw(filename);

    struct stat *buf = mallocz(sizeof(struct stat));
    int s = stat(f_str, buf);
    if (s == -1) {
        rt_processor_attach_errort(strerror(errno));
        return NULL;
    }
    n_struct_t *result = stat_to_n_struct(buf);
    free(buf);
    return result;
}

n_struct_t *syscall_fstat(n_int_t fd) {
    struct stat *buf = mallocz(sizeof(struct stat));
    int s = fstat((int) fd, buf);
    if (s == -1) {
        rt_processor_attach_errort(strerror(errno));
        return NULL;
    }
    n_struct_t *result = stat_to_n_struct(buf);
    free(buf);
    return result;
}

// 使用 waitpid, 返回值为 exit status
n_u32_t syscall_wait(n_int_t pid) {
    int status;
    int result = waitpid((pid_t) pid, &status, 0);
    if (result == -1) {
        rt_processor_attach_errort(strerror(errno));
        return 0;
    }

    return (n_u32_t) status;
}

n_string_t *syscall_getcwd() {
    char *buf = mallocz(sizeof(char) * 1024);
    char *result = getcwd(buf, 1024);
    if (result == NULL) {
        rt_processor_attach_errort(strerror(errno));
        return NULL;
    }
    n_string_t *s = string_new(result, strlen(result));
    free(buf);
    return s;
}

n_int_t syscall_call6(n_int_t number, n_uint_t a1, n_uint_t a2, n_uint_t a3, n_uint_t a4, n_uint_t a5, n_uint_t a6) {
    int64_t result = syscall(number, a1, a2, a3, a4, a5, a6);
    if (result == -1) {
        rt_processor_attach_errort(strerror(errno));
        return 0;
    }
    return (n_int_t) result;
}


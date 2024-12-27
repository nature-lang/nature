#include "tests/test.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// 执行 curl 命令并获取输出
char *http_get(const char *path) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "curl -s http://127.0.0.1:8888%s", path);

    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char *response = mallocz(1024);
    size_t len = fread(response, 1, 1023, fp);
    response[len] = '\0';

    pclose(fp);
    return response;
}

int main(void) {
    blackbox_setup();
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程运行 http 服务器
        exec_imm_param();
    } else {
        // 父进程
        sleep(1);
        char *response = http_get("/");
        assert_string_equal(response, "hello nature");
        // 关闭 http 服务器
        http_get("/close");

        int status;
        waitpid(pid, &status, 0); // 等待子进程结束
        log_debug("http server %d exit with status %d", pid, status);
    }
}

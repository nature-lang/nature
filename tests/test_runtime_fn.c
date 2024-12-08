#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>  // 基础 socket 定义
#include <netinet/in.h>  // sockaddr_in 和 sockaddr_in6 的定义
#include <sys/un.h>      // sockaddr_un 的定义
#include <sys/stat.h>

#include "test_runtime.h"

int main() {
    const char *filename = "test.txt";
    int flags = O_RDONLY;  // 以只读方式打开
    mode_t mode = 0644;    // 文件权限

    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    struct sockaddr_un sun;

    // 使用 SYS_openat
    int fd = syscall(SYS_openat, AT_FDCWD, filename, flags, mode);

    if (fd < 0) {
        perror("打开文件失败");
        return -1;
    }

    printf("文件描述符: %d\n", fd);

    // 关闭文件
    syscall(SYS_close, fd);

    return 0;
}
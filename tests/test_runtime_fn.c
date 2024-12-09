#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>  // 基础 socket 定义
#include <netinet/in.h>  // sockaddr_in 和 sockaddr_in6 的定义
#include <sys/un.h>      // sockaddr_un 的定义
#include <sys/stat.h>
#include <dirent.h>

#include "test_runtime.h"

int main() {
    DIR *dir;
    struct dirent *entry;

    // 打开当前目录 "."
    dir = opendir(".");

    if (dir == NULL) {
        printf("无法打开目录\n");
        return 1;
    }

    // 读取目录中的所有项目
    while ((entry = readdir(dir)) != NULL) {
        printf("文件名: %s\n", entry->d_name);
    }

    // 关闭目录
    closedir(dir);
    return 0;
}
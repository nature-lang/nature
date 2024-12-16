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
    char dest[25]; // 目标缓冲区大小为 25 字节

    // 测试用例 1：源字符串长度小于 n
    const char *src1 = "Hello, World!";
    str_rcpy(dest, src1, 24);
    printf("Test 1: %s\n", dest); // 预期输出: "Hello, World!"

    // 测试用例 2：源字符串长度等于 n
    const char *src2 = "This is a test string.";
    str_rcpy(dest, src2, 24);
    printf("Test 2: %s\n", dest); // 预期输出: "This is a test string."

    // 测试用例 3：源字符串长度大于 n
    const char *src3 = "abcdefghijklmnopqrstuvwxyz";
    str_rcpy(dest, src3, 24);
    printf("Test 3: %s\n", dest); // 预期输出: "cdefghijklmnopqrstuvwxyz"

    return 0;
}
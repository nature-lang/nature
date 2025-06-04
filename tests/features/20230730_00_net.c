#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests/test.h"

#define BUFFER_SIZE 4096
#define SERVER_PORT 8080
#define SERVER_HOST "127.0.0.1"

char output[1024];

static char *get_http_body(const char *response) {
    // 查找空行分隔的响应体数据
    const char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        return strdup(body_start + 4); // 复制响应体数据到堆上的新内存
    }
    return NULL;
}

// 简化的HTTP客户端，发送请求并接收响应
static int send_http_request() {
    int sockfd;
    struct sockaddr_in server_addr;
    char *host = SERVER_HOST;
    int port = SERVER_PORT;
    char *path = "/";

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_debug("socket creation failed: %s", strerror(errno));
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &(server_addr.sin_addr));

    if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        log_debug("connect failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path, host, port);

    if (send(sockfd, request, strlen(request), 0) < 0) {
        log_debug("send failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    char response[BUFFER_SIZE];
    int bytes_received;
    size_t total_size = 0;

    while ((bytes_received = recv(sockfd, response, BUFFER_SIZE - 1, 0)) > 0) {
        response[bytes_received] = '\0';
        total_size += bytes_received;

        char *http_body = get_http_body(response);
        if (http_body) {
            log_debug("%d, %s\n", (int) total_size, http_body);
            assert_string_equal(http_body, "Hello, World!");
            free(http_body);
        }
    }

    close(sockfd);
    return 0;
}

// 启动HTTP服务器的子进程
static pid_t start_server() {
    pid_t pid = fork();

    if (pid == 0) {
        // 子进程
        char *temp = exec_output();
        strcpy(output, temp);
        exit(0);
    } else if (pid > 0) {
        // 父进程：等待服务器启动
        sleep(1); // 给服务器启动时间
        return pid;
    } else {
        log_debug("fork failed: %s", strerror(errno));
        return -1;
    }
}

// 停止HTTP服务器
static void stop_server(pid_t server_pid) {
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        int status;
        waitpid(server_pid, &status, 0);
    }
}

// 简化的curl函数，使用进程而不是线程
static void curl() {
    pid_t server_pid = start_server();
    if (server_pid < 0) {
        log_debug("Failed to start server");
        return;
    }

    log_debug("child start successful");

    // 发送 HTTP 请求
    send_http_request();

    // 停止服务器
    stop_server(server_pid);
}

// 保持原有的test_basic函数不变
static void test_basic() {
    curl();
}

int main(void) {
    TEST_BASIC
}
#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 4096

static char *get_http_body(const char *response) {
    // 查找空行分隔的响应体数据
    const char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        return strdup(body_start + 4); // 复制响应体数据到堆上的新内存
    }
    return NULL;
}

static void curl() {
    int fid = fork();
    if (fid == 0) {
        sleep(1);

        int sockfd;
        struct sockaddr_in server_addr;
        char *host = "127.0.0.1";
        int port = 8080;
        char *path = "/"; // 设置请求的路径，根据需要修改

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &(server_addr.sin_addr));

        if (connect(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            perror("connect");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        char request[BUFFER_SIZE];
        snprintf(request, BUFFER_SIZE, "GET %s HTTP/1.1\r\nHost: %s:%d\r\n\r\n", path, host, port);

        if (send(sockfd, request, strlen(request), 0) < 0) {
            perror("send");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        char response[BUFFER_SIZE];
        int bytes_received;
        size_t total_size = 0;

        while ((bytes_received = recv(sockfd, response, BUFFER_SIZE - 1, 0)) > 0) {
            response[bytes_received] = '\0';
            total_size += bytes_received;

            char *http_body = get_http_body(response);
            DEBUGF("%s\n", http_body);
        }

        close(sockfd);
        exit(0);
    }

    DEBUGF("child start successful");
}

static void test_basic() {
    curl();

    char *raw = exec_output();
    char *str = "create socket success\n"
                "bind :8080 success\n"
                "listen success\n"
                "accept success\n"
                "send success, len: 78\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
#include "tests/test.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

char *code1 = "import fs\n"
              "\n"
              "fn main() {\n"
              "    println(\"hello world\")\n"
              "    var stderr = fs.stderr()\n"
              "    stderr.write(\"hello error\\n\" as [u8])\n"
              "}";

char *code2 = "import fmt  \n"
              "\n"
              "fn fib(int n):int {\n"
              "    if (n <= 1) {\n"
              "        return n\n"
              "    }\n"
              "    return fib(n - 1) + fib(n - 2)\n"
              "}\n"
              "\n"
              "fn main() {\n"
              "    var result = fib(10)\n"
              "    fmt.printf(\"result=%d\", result)\n"
              "}";

char *code3 = "type box<T, U> = struct {\n"
              "\tT width\n"
              "\tU length\n"
              "}\n"
              "  \n"
              "fn box<T, U>.perimeter():T {\n"
              "    return (self.width + self.length as T) * 2\n"
              "}\n"
              "\n"
              "fn main() {\n"
              "    var b = box<i16, i32>{width=10, length=20}\n"
              "    println('perimeter =', b.perimeter())\n"
              "}";

char *code4 = "import co\n"
              "\n"
              "fn delay_sum(int a, int b):int {\n"
              "    co.sleep(1000)\n"
              "\n"
              "    return a + b\n"
              "}\n"
              "\n"
              "fn main():void! {\n"
              "  var fut = go delay_sum(1, 1) // eq: future_t<int> fut = @async(delay_sum(1, 1), 0)\n"
              "  var result = fut.await()\n"
              "\n"
              "  println('result =', result)\n"
              "}";

char *code5 = "fn main() {\n"
              "     var result = rem(10, 0) catch err {\n"
              "        println(err.msg)\n"
              "        break 1\n"
              "    }\n"
              "    println(result)\n"
              "}\n"
              "\n"
              "fn rem(int dividend, int divisor):int! {\n"
              "\tif divisor == 0 {\n"
              "\t\tthrow 'divisor cannot zero'\n"
              "\t}\n"
              "\treturn dividend % divisor\n"
              "}";

int main(void) {
    feature_test_build();
    //    exec_imm_param();

    pid_t pid = fork();
    if (pid == 0) {
        // 子进程运行 http 服务器
        exec_imm_param();
    }

    sleep(1); // 等待子进程启动完成

    // 测试编译内容
    // 测试 code1
    char curl_cmd[4096];
    char buf[2048] = {0};

    snprintf(curl_cmd, sizeof(curl_cmd),
             "curl -s -X POST http://127.0.0.1:8888/playgrounds/run "
             "-H 'content-type: text/plain' --data-binary '%s'",
             code1);
    FILE *fp = popen(curl_cmd, "r");
    fread(buf, 1, sizeof(buf) - 1, fp);
    pclose(fp);
    if (!strstr(buf, "hello world\\nhello error\\n")) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0); // 等待子进程退出
        assertf(false, "buf: %s", buf);
    }

    // kill pid
    kill(pid, SIGKILL); // 发送 SIGKILL 信号给子进程
    waitpid(pid, NULL, 0); // 等待子进程退出
}

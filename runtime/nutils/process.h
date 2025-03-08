#ifndef NATURE_PROCESS_H
#define NATURE_PROCESS_H

#include "vec.h"
#include <uv.h>

typedef struct {
} process_context_t;


process_context_t *rt_uv_process_run() {


//    char* args[3];
//    args[0] = "mkdir";
//    args[1] = "test-dir";
//    args[2] = NULL;
//
//    uv_process_t child_req;
//    uv_process_options_t options = {0};
//
//    options.exit_cb = on_exit; // exit cb?
//    options.file = "mkdir";
//    options.cwd = "/tmp";
//    options.env = NULL; // 字符串数组，NULL 会继承父环境变量, 还能有一组 flags
//    options.args = args;
    // 底层使用 execvp 所以不需要提供完整路径，会根据环境变量进程查找
    // 参数数组的数量必须比实际参数数量多一个
}



#endif //NATURE_PROCESS_H

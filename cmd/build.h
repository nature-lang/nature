#ifndef NATURE_BUILD_H
#define NATURE_BUILD_H

#include "src/value.h"

string binary_path;

/**
 * 1. 解析环境变量
 * 2. 解析 work dir
 * 3. 解析 base namespace
 * 4. 解析 文件完整路径, 丢给 path build(写死 module_name = main)
 * @param path
 */
void build(string build_target);

#endif //NATURE_BUILD_H

#ifndef NATURE_IMPORT_H
#define NATURE_IMPORT_H

#include "value.h"

typedef struct {
    string namespace;
    string path;
    string full_path; // 全路径拼写
    string alias; // 别名
} import_t;

#endif //NATURE_IMPORT_H

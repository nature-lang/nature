#ifndef NATURE_ANY_H
#define NATURE_ANY_H

#include "utils/type.h"

// 主要是为了表达两个字节的数据,这里用数组，用啥结构都是可以的，用结构体纯属为了方便
// 一定要注意其和类型描述信息的区别
typedef struct {
    reflect_type_t *type;
    void *value;
} any_t;

any_t *any_trans(uint rtype_index, void *value);

#endif //NATURE_ANY_H

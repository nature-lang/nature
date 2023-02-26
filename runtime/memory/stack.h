//
// 用户态将有单独的虚拟栈，避免 runtime 中的 gc 扫描到多余的 runtime fn
// 进入到 runtime 后需要尽快切换到系统栈
//

#ifndef NATURE_STACK_H
#define NATURE_STACK_H

typedef struct {

} stack_t;

#endif //NATURE_STACK_H

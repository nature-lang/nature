#ifndef NATURE_REGISTER_ARM64_H
#define NATURE_REGISTER_ARM64_H

#include "src/types.h"

// 通用寄存器
extern reg_t *x0, *x1, *x2, *x3, *x4, *x5, *x6, *x7;
extern reg_t *x8, *x9, *x10, *x11, *x12, *x13, *x14, *x15;
extern reg_t *x16, *x17, *x18, *x19, *x20, *x21, *x22, *x23;
extern reg_t *x24, *x25, *x26, *x27, *x28, *x29, *x30;


// 特殊寄存器
extern reg_t *sp;  // 栈指针
extern reg_t *pc;  // 程序计数器
extern reg_t *xzr; // 零寄存器
extern reg_t *wzr; // 32位零寄存器

// 浮点寄存器
extern reg_t *v0, *v1, *v2, *v3, *v4, *v5, *v6, *v7;
extern reg_t *v8, *v9, *v10, *v11, *v12, *v13, *v14, *v15;
extern reg_t *v16, *v17, *v18, *v19, *v20, *v21, *v22, *v23;
extern reg_t *v24, *v25, *v26, *v27, *v28, *v29, *v30, *v31;


void arm64_reg_init();

alloc_kind_e arm64_alloc_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var);

alloc_kind_e arm64_alloc_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var);


#endif //NATURE_ARM64_H

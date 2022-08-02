#include "amd64.h"
#include "src/type.h"
#include "src/assembler/amd64/register.h"
#include "src/lib/error.h"
#include "src/lib/helper.h"
#include "src/symbol.h"

amd64_lower_fn amd64_lower_table[] = {
        [LIR_OP_TYPE_ADD] = amd64_lower_add,
        [LIR_OP_TYPE_CALL] = amd64_lower_call,
        [LIR_OP_TYPE_BUILTIN_CALL] = amd64_lower_call,
        [LIR_OP_TYPE_RUNTIME_CALL] = amd64_lower_call,
        [LIR_OP_TYPE_LABEL] = amd64_lower_label,
        [LIR_OP_TYPE_RETURN] = amd64_lower_return,
        [LIR_OP_TYPE_BEQ] = amd64_lower_cmp_goto,
        [LIR_OP_TYPE_BAL] = amd64_lower_bal,
        [LIR_OP_TYPE_SGT] = amd64_lower_sgt,
        [LIR_OP_TYPE_MOVE] = amd64_lower_mov,
        [LIR_OP_TYPE_LEA] = amd64_lower_lea,
        [LIR_OP_TYPE_FN_BEGIN] = amd64_lower_fn_begin,
        [LIR_OP_TYPE_FN_END] = amd64_lower_fn_end,
};

static asm_operand_t *amd64_lower_operand_var_transform(lir_operand_var *var, uint8_t force_size) {
    uint8_t size = type_base_sizeof(var->infer_size_type);
    if (force_size > 0) {
        size = force_size;
    }
    if (var->reg_id > 0) {
        // 如果是 bool 类型
        asm_operand_register_t *reg = amd64_register_find(var->reg_id, size);
        return REG(reg);
    }
    if (var->decl->stack_offset > 0) {
        return DISP_REG(rbp, -(*var->decl->stack_offset), size);
    }

    error_exit("[amd64_lower_var_operand] var %d not reg_id or stack offset", var->ident);
}

/**
 * LIR_OP_TYPE_CALL base, param => result
 * base 有哪些形态？
 * http.get() // 外部符号引用,无非就是是否在符号表中
 * sum.n() // 内部符号引用, sum.n 是否属于 var? 所有的符号都被记录为 var
 * test[0]() // 经过计算后的左值，其值最终存储在了一个临时变量中 => temp var
 * 所以 base 最终只有一个形态，那就是 var, 如果 var 在当前 closure 有定义，那其至少会被
 * - 会被堆栈分配,此时如何调用 call 指令？
 * asm call + stack[1]([indirect addr]) or asm call + reg ?
 * 没啥可以担心的，这都是被支持的
 *
 * - 但是如果其如果没有被寄存器分配，就属于外部符号(label)
 * asm call + SYMBOL 即可
 * 所以最终
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_call(closure *c, lir_op *op) {
    list *insts = list_new();

    uint8_t used[2] = {0};
    asm_operand_t *result = NULL;
    if (op->result != NULL) {
        result = NEW(asm_operand_t);
        list_append(insts, amd64_lower_operand_transform(op->result, result, used));
    }

    // 实参传递(封装一个 static 函数处理),
    asm_operand_t *first = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->first, first, used));

    uint8_t actual_used[2] = {0};
    // 2. 参数处理  lir_ope type->second;
    lir_operand_actual_param *v = op->second->value;
    list *param_insts = list_new();
    // 计算 push 总长度，进行栈对齐
    int push_length = 0;
    for (int i = 0; i < v->count; ++i) {
        lir_operand *operand = v->list[i];

        // 如果是 bool, source 存在 1bit, 但是不影响寄存器或者堆栈，寄存器和堆栈在 amd64 位下都是统一 8bit
        list *temp_insts = list_new();
        asm_operand_t *source = NEW(asm_operand_t);
        uint8_t actual_transform_used[2] = {0};
        list_append(temp_insts, amd64_lower_operand_transform(operand, source, actual_transform_used));

        reg_t *target_reg = amd64_lower_fn_next_reg_target(actual_used, source->size); // source 和 target 大小要匹配
        if (target_reg == NULL) {
            // push
            list_push(temp_insts, ASM_INST("push", { source })); // push 会导致 rsp 栈不对齐
            push_length += source->size;
        } else {
            list_push(temp_insts, ASM_INST("mov", { REG(target_reg), source }));
        }
        list_append(temp_insts, param_insts);
        param_insts = temp_insts;
    }
    // 栈对齐
    uint64_t diff_length = memory_align(push_length, 16) - push_length;
    if (diff_length > 0) {
        list_push(insts, ASM_INST("sub", { REG(rsp), UINT8(diff_length) }));
    }

    list_append(insts, param_insts);

    if (first->type == ASM_OPERAND_TYPE_SYMBOL && is_print_symbol(((asm_operand_symbol_t *) first->value)->name)) {
        // x. TODO 仅调用变成函数之前，需要将 rax 置为 0, 如何判断调用目标是否为变长函数？
        list_push(insts, ASM_INST("mov", { REG(rax), UINT32(0) }));
    }


    // 3. 调用 call 指令(处理地址), 响应的结果在 rax 中
    list_push(insts, ASM_INST("call", { first }));

    // 4. 响应处理(取出响应值传递给 result)
    if (op->result != NULL) {
        if (lir_operand_type_base(op->result) == TYPE_FLOAT) {
            list_push(insts, ASM_INST("mov", { result, REG(xmm0) }));
        } else {
            list_push(insts, ASM_INST("mov", { result, REG(rax) }));
        }
    }

    return insts;
}

/**
 * 核心问题是: 在结构体作为返回值时，当外部调用将函数的返回地址作为参数 rdi 传递给函数时，
 * 根据 ABI 规定，函数操作的第一步就是对 rdi 入栈，但是当 lower return 时,我并不知道 rdi 被存储在了栈的什么位置？
 * 但是实际上是能够知道的，包括初始化时 sub rbp,n 的 n 也是可以在寄存器分配阶段就确定下来的。
 * n 的信息作为 closure 的属性存储在 closure 中，如何将相关信息传递给 lower ?, 参数 1 改成 closure?
 * 在结构体中 temp_var 存储的是结构体的起始地址。不能直接 return 起始地址，大数据会随着函数栈帧消亡。 而是将结构体作为整个值传递。
 * 既然已经知道了结构体的起始位置，以及隐藏参数的所在栈帧。 就可以直接进行结构体返回值的构建。
 * @param c
 * @param ast
 * @return
 */
list *amd64_lower_return(closure *c, lir_op *op) {
    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
    list *insts = list_new();
    uint8_t used[2] = {0};

    asm_operand_t *result = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->result, result, used));

    if (lir_operand_type_base(op->result) == TYPE_FLOAT) {
        list_push(insts, ASM_INST("mov", { REG(xmm0), result }));
    } else {
        list_push(insts, ASM_INST("mov", { REG(rax), result }));
    }

    return insts;
}


list *amd64_lower_bal(closure *c, lir_op *op) {
    list *insts = list_new();
    uint8_t used[2] = {0};

    asm_operand_t *result = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->result, result, used));
    list_push(insts, ASM_INST("jmp", { result }));
    return insts;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_add(closure *c, lir_op *op) {
    list *insts = list_new();
    uint8_t used[2] = {0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_operand_transform(op->first, first, used);
    list_append(insts, temp);
    asm_operand_t *second = NEW(asm_operand_t);
    temp = amd64_lower_operand_transform(op->second, second, used);
    list_append(insts, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_operand_transform(op->result, result, used);
    list_append(insts, temp);

    reg_t *reg = amd64_lower_next_reg(used, lir_operand_sizeof(op->result));
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("add", { REG(reg), second }));
    list_push(insts, ASM_INST("mov", { result, REG(reg) }));

    return insts;
//    error_exit("[amd64_lower_add] type->result_type not identify");
//    return NULL;
}

// lir GT foo,bar => result
list *amd64_lower_sgt(closure *c, lir_op *op) {
    list *insts = list_new();
    uint8_t used[2] = {0};

    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_operand_transform(op->first, first, used);
    list_append(insts, temp);

    asm_operand_t *second = NEW(asm_operand_t);
    temp = amd64_lower_operand_transform(op->second, second, used);
    list_append(insts, temp);

    // result 用于 setg, 必须强制 byte 大小
    asm_operand_t *result = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->result, result, used));

    // bool = int64 > int64
    reg_t *reg = amd64_lower_next_reg(used, first->size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("cmp", { REG(reg), second }));

    // setg r/m8(TODO 强制要求 1bit 用于 setg)
    list_push(insts, ASM_INST("setg", { result }));

    return insts;

    // float
//    error_exit("[amd64_lower_gt] type->result_type not identify, only support TYPE_INT");
//    return NULL;
}


list *amd64_lower_label(closure *c, lir_op *op) {
    list *insts = list_new();
    lir_operand_symbol_label *label_operand = op->result->value;
    list_push(insts, ASM_INST("label", { SYMBOL(label_operand->ident, label_operand->is_local) }));
    return insts;
}


/**
 * mov foo => bar
 * mov 1 => var
 * mov 1.1 => var
 * @param c
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_mov(closure *c, lir_op *op) {
    list *insts = list_new();

    uint8_t used[2] = {0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_operand_transform(op->first, first, used);
    list_append(insts, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_operand_transform(op->result, result, used);
    list_append(insts, temp);

    uint8_t size = result->size;

    reg_t *reg = amd64_lower_next_reg(used, size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("mov", { result, REG(reg) }));

    return insts;
}


/**
 * lir_operand 中不能直接转换为 asm_operand 的参数
 * type_string/lir_operand_memory
 * @param operand
 * @param asm_operand
 * @param used_regs
 * @return
 */
list *amd64_lower_operand_transform(lir_operand *operand,
                                    asm_operand_t *asm_operand,
                                    uint8_t used[2]) {
    list *insts = list_new();

    if (operand->type == LIR_OPERAND_TYPE_IMM) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_STRING_RAW) {
            // 生成符号表(TODO 使用字符串 md5 代替)
            char *unique_name = AMD64_DECL_UNIQUE_NAME();
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = unique_name;
            decl->size = strlen(v->string_value) + 1; // + 1 表示 \0
            decl->value = (uint8_t *) v->string_value;
            decl->type = ASM_VAR_DECL_TYPE_STRING;
            list_push(amd64_decl_list, decl);

            // 使用临时寄存器保存结果(会增加一条 lea 指令)
            reg_t *reg = amd64_lower_next_reg(used, QWORD);

            list_push(insts, ASM_INST("lea", { REG(reg), SYMBOL(unique_name, false) }));

            // asm_copy
            ASM_OPERAND_COPY(asm_operand, REG(reg));
        } else if (v->type == TYPE_FLOAT) {
            char *unique_name = AMD64_DECL_UNIQUE_NAME();
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = unique_name;
            decl->size = QWORD;
            decl->value = (uint8_t *) &v->float_value; // float to uint8
            decl->type = ASM_VAR_DECL_TYPE_FLOAT;
            list_push(amd64_decl_list, decl);

            // 使用临时寄存器保存结果
            reg_t *reg = amd64_lower_next_reg(used, OWORD);

            // movq xmm1,rm64
            list_push(insts, ASM_INST("mov", { REG(reg), SYMBOL(unique_name, false) }));

            ASM_OPERAND_COPY(asm_operand, REG(reg));
        } else if (v->type == TYPE_INT) {
            ASM_OPERAND_COPY(asm_operand, UINT(v->int_value)); // 默认使用 UINT32,v->int_value 真的大于 32 位时使用 64
        } else if (v->type == TYPE_INT8) {
            ASM_OPERAND_COPY(asm_operand, UINT8(v->int_value));
        } else if (v->type == TYPE_INT16) {
            ASM_OPERAND_COPY(asm_operand, UINT16(v->int_value));
        } else if (v->type == TYPE_INT32) {
            ASM_OPERAND_COPY(asm_operand, UINT32(v->int_value));
        } else if (v->type == TYPE_INT64) {
            ASM_OPERAND_COPY(asm_operand, UINT64(v->int_value));
        } else if (v->type == TYPE_FLOAT) {
            ASM_OPERAND_COPY(asm_operand, FLOAT32(v->float_value));
        } else if (v->type == TYPE_BOOL) {
            ASM_OPERAND_COPY(asm_operand, UINT8(v->bool_value));
        } else {
            error_exit("[amd64_lower_to_asm_operand] type immediate not expected");
        }
        return insts;
    }

    if (operand->type == LIR_OPERAND_TYPE_ADDR) {
        lir_operand_addr *v = operand->value;
        // base 类型必须为 var
        if (v->base->type != LIR_OPERAND_TYPE_VAR) {
            error_exit("[amd64_lir_to_asm_operand] operand type memory, but that base not type var");
        }

        lir_operand_var *base_var = v->base->value;

        // rbp 存储了 base,
        if (base_var->decl->stack_offset == 0) {
            error_exit("[amd64_lir_to_asm_operand]  var cannot stack_frame_offset in var %s", base_var->ident);
        }
        // 需要占用一个临时寄存器
        reg_t *reg = amd64_lower_next_reg(used, QWORD);

        // 如果设置了 indirect_addr, 则编译成 [rxx+offset]
        // 否则应该编译成 ADD  rxx -> offset, asm_operand 配置成 rxx
        if (v->indirect_addr) {
            // 生成 mov 指令（asm_mov）
            list_push(insts, ASM_INST("mov", { REG(reg), DISP_REG(rbp, -(*base_var->decl->stack_offset), QWORD) }));
            asm_operand_t *temp = DISP_REG(reg, v->offset, type_base_sizeof(v->infer_size_type));
            ASM_OPERAND_COPY(asm_operand, temp);
        } else {
            list_push(insts, ASM_INST("add", { REG(reg), UINT32(v->offset) }));
            ASM_OPERAND_COPY(asm_operand, REG(reg));
        }

        return insts;
    }

    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *v = operand->value;
        // 解引用处理
        if (v->indirect_addr) {
            // bool* a; 为例子 *a 能够承载的大小 = var->infer_type_size
            // 非指针则不允许走到这里
            if (v->decl->ast_type.point == 0) {
                error_exit("[amd64_lir_to_asm_operand]  indirect addr var %s must be point", v->ident);
            }

            reg_t *reg = amd64_lower_next_reg(used, QWORD);
            asm_operand_t *var_operand = amd64_lower_operand_var_transform(v, 0);
            list_push(insts, ASM_INST("mov", { REG(reg), var_operand }));

            // 解引用
            ASM_OPERAND_COPY(asm_operand, INDIRECT_REG(reg, type_base_sizeof(v->infer_size_type)));
            return insts;
        } else {
            ASM_OPERAND_COPY(asm_operand, amd64_lower_operand_var_transform(v, 0))
            return insts;
        }
    }

    if (operand->type == LIR_OPERAND_TYPE_SYMBOL_VAR) {
        lir_operand_symbol_var *v = operand->value;
        ASM_OPERAND_COPY(asm_operand, LABEL(v->ident));
        return insts;
    }

    if (operand->type == LIR_OPERAND_TYPE_SYMBOL_LABEL) {
        lir_operand_symbol_label *v = operand->value;
        ASM_OPERAND_COPY(asm_operand, SYMBOL(v->ident, false));
        return insts;
    }

    return insts;
}

list *amd64_lower_op(closure *c, lir_op *op) {
    amd64_lower_fn fn = amd64_lower_table[op->type];
    if (fn == NULL) {
        error_exit("[amd64_lower_op] amd64_lower_table not found fn: %d", op->type);
    }
    return fn(c, op);
}

void amd64_lower_init() {
    amd64_decl_list = list_new();
}

reg_t *amd64_lower_next_reg(uint8_t used[2], uint8_t size) {
    uint8_t used_index = 0; // 8bit ~ 64bit
    if (size > 8) {
        used_index = 1;
    }
    uint8_t count = used[used_index]++;

    reg_t *r = (reg_t *) amd64_register_find(count, size);
    if (r == NULL) {
        error_exit("[amd64_register_find] not found, count: %d, size: %d", count, size);
    }
    return r;
}


/**
 * amd64 下统一使用 8byte 寄存器或者 16byte xmm 寄存器
 * 返回下一个可用的寄存器或者内存地址
 * 但是这样就需要占用 rbp 偏移，怎么做？
 * 每个函数定义的最开始已经使用 sub n => rsp, 已经申请了栈空间 [0 ~ n]
 * 后续函数中调用其他函数所使用的栈空间都是在 [n ~ 无限] 中, 直接通过 push 操作即可
 * 但是需要注意 push 的顺序，最后的参数先 push (也就是指令反向 merge)
 * 如果返回了 NULL 就说明没有可用的寄存器啦，加一条 push 就行了
 * @param count
 * @param size
 * @return
 */
reg_t *amd64_lower_fn_next_reg_target(uint8_t used[2], uint8_t size) {
    uint8_t used_index = 0; // 8bit ~ 64bit
    if (size > 8) {
        used_index = 1;
    }
    uint8_t count = used[used_index]++;
    uint8_t reg_index_list[] = {7, 6, 2, 1, 8, 9};
    // 通用寄存器 (0~5 = 6 个)
    if (size <= QWORD && count <= 5) {
        uint8_t reg_index = reg_index_list[count];
        return (reg_t *) amd64_register_find(reg_index, QWORD);
    }

    // 浮点寄存器(0~7 = 8 个)
    if (size > QWORD && count <= 7) {
        return (reg_t *) amd64_register_find(count, OWORD);
    }

    return NULL;
}

list *amd64_lower_fn_begin(closure *c, lir_op *op) {
    list *insts = list_new();
    // 计算堆栈信息
    list_node *current = list_last(c->local_var_decls); // rear 为 empty 占位
    while (current != NULL) {
        lir_local_var_decl *var = current->value;
        // 局部变量不需要考虑什么最小值的问题，直接网上涨就好了
        uint8_t size = type_base_sizeof(var->ast_type.base);
        c->stack_length += size;
        *var->stack_offset = -(c->stack_length); // rbp-n, 所以这里取负数

        current = current->prev;
    }

    // 部分局部变量需要占用一部分栈空间(amd64 架构下统一使用 8byte)， 按顺序使用堆栈即可
    // 还有一部分 push
    current = c->formal_params->front;
    while (current->value != NULL) {
        lir_local_var_decl *var = current->value;
        // TODO 为什么不能直接接着堆栈走？

        current = current->next;
    }


    // 16 对齐
    c->stack_length = memory_align(c->stack_length, 16);

    list_push(insts, ASM_INST("push", { REG(rbp) }));
    list_push(insts, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    list_push(insts, ASM_INST("sub", { REG(rsp), UINT32(c->stack_length) }));

    // 形参入栈
    list_append(insts, amd64_lower_fn_formal_params(c));

    return insts;
}

/**
 * 拼接在函数尾部，实现结尾块自然退出。
 * @param c
 * @return
 */
list *amd64_lower_fn_end(closure *c, lir_op *op) {
    list *insts = list_new();
    list_push(insts, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    list_push(insts, ASM_INST("pop", { REG(rbp) }));
    list_push(insts, ASM_INST("ret", {}));
    return insts;
}

list *amd64_lower_fn_formal_params(closure *c) {
    list *insts = list_new();
    // 已经在栈里面的就不用管了，只取寄存器中的。存放在 lir_var 中的 stack_offset 中即可
    uint8_t formal_used[2] = {0};
    for (int i = 0; i < c->formal_params.count; i++) {
        lir_operand_var *var = c->formal_params.list[i];

        uint8_t used[2] = {0};
        asm_operand_t *target = NEW(asm_operand_t);
        list_append(insts, amd64_lower_operand_transform(
                LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, var), target, used));

        reg_t *source_reg = amd64_lower_fn_next_reg_target(formal_used, type_base_sizeof(var->infer_size_type));
        if (source_reg != NULL) {
            list_push(insts, ASM_INST("mov", { target, REG(source_reg) }));
        }
    }
    return insts;
}

list *amd64_lower_closure(closure *c) {
    list *insts = list_new();

    // 遍历 block
    for (int i = 0; i < c->blocks.count; ++i) {
        lir_basic_block *block = c->blocks.list[i];
        list_append(insts, amd64_lower_block(c, block));
    }

    return insts;
}


list *amd64_lower_block(closure *c, lir_basic_block *block) {
    list *insts = list_new();
    list_node *current = block->operates->front;
    while (current->value != NULL) {
        lir_op *op = current->value;
        list_append(insts, amd64_lower_op(c, op));
        current = current->next;
    }
    return insts;
}


list *amd64_lower_cmp_goto(closure *c, lir_op *op) {
    list *insts = list_new();
    uint8_t used[2] = {0};

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    asm_operand_t *first = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->first, first, used));

    asm_operand_t *second = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->second, second, used));

    asm_operand_t *result = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->result, result, used));

    // cmp 指令比较
    reg_t *reg = amd64_lower_next_reg(used, first->size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("cmp", { REG(reg), second }));
    list_push(insts, ASM_INST("je", { result }));

    return insts;
}

list *amd64_lower_lea(closure *c, lir_op *op) {
    if (op->first->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[amd64_lower_lead] first operand type not LIR_OPERAND_TYPE_VAR");
    }
    if (op->result->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[amd64_lower_lead] result operand type not LIR_OPERAND_TYPE_VAR");
    }

    list *insts = list_new();
    uint8_t used[2] = {0};

    asm_operand_t *first = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->first, first, used));

    asm_operand_t *result = NEW(asm_operand_t);
    list_append(insts, amd64_lower_operand_transform(op->result, result, used));


    reg_t *reg = amd64_lower_next_reg(used, result->size);
    list_push(insts, ASM_INST("lea", { REG(reg), first }));
    list_push(insts, ASM_INST("mov", { result, REG(reg) }));
    return insts;
}

uint8_t amd64_formal_min_stack(uint8_t size) {
    if (size < 8) {
        size = 8;
    }
    return size;
}

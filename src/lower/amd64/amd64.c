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
        [LIR_OP_TYPE_CMP_GOTO] = amd64_lower_cmp_goto,
        [LIR_OP_TYPE_GOTO] = amd64_lower_goto,
        [LIR_OP_TYPE_GT] = amd64_lower_gt,
        [LIR_OP_TYPE_MOVE] = amd64_lower_mov,
        [LIR_OP_TYPE_FN_BEGIN] = amd64_lower_fn_begin,
        [LIR_OP_TYPE_FN_END] = amd64_lower_fn_end,
};

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

    asm_operand_t *result = NULL;
    if (op->result != NULL) {
        result = amd64_lower_to_asm_operand(op->result); // 可能是寄存器，也可能是内存地址
    }

    // 特殊逻辑，如果响应的参数是一个结构体，就需要做隐藏参数的处理
    // 实参传递(封装一个 static 函数处理),
    asm_operand_t *first = amd64_lower_to_asm_operand(op->first);

    uint8_t used[2] = {0};
    // 1. 大返回值处理(使用 rdi 预处理)
    if (op->result != NULL && lir_type_category(op->result) == TYPE_STRUCT) {
        reg_t *target_reg = amd64_lower_next_actual_reg_target(used, QWORD);
        list_push(insts, ASM_INST("lea", { REG(target_reg), result }));
    }

    // 2. 参数处理  lir_ope type->second;
    lir_operand_actual_param *v = op->second->value;
    list *param_insts = list_new();
    // 计算 push 总长度，进行栈对齐
    int push_length = 0;
    for (int i = 0; i < v->count; ++i) {
        lir_operand *operand = v->list[i];
        // 实参可能是简单参数，也有可能是复杂参数
        regs_t used_regs = {.count = 0};
        asm_operand_t *source = NEW(asm_operand_t);
        list *temp = amd64_lower_complex_to_asm_operand(operand, source, &used_regs);
        reg_t *target_reg = amd64_lower_next_actual_reg_target(used, source->size); // source 和 target 大小要匹配
        if (target_reg == NULL) {
            // push
            list_push(temp, ASM_INST("push", { source })); // push 会导致 rsp 栈不对齐
            push_length += source->size;
        } else {
            list_push(temp, ASM_INST("mov", { REG(target_reg), source }));
        }
        list_append(temp, param_insts);
        param_insts = temp;
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
        if (lir_type_category(op->result) == TYPE_FLOAT) {
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

    asm_operand_t *result = amd64_lower_to_asm_operand(op->result);

    if (lir_type_category(op->result) == TYPE_STRUCT) {
        // 计算长度
        int rep_count = ceil((float) result->size / QWORD);
        asm_operand_t *return_disp_rbp = DISP_REG(rbp, c->return_offset, QWORD);

        list_push(insts, ASM_INST("mov", { REG(rsi), result }));
        list_push(insts, ASM_INST("mov", { REG(rdi), return_disp_rbp }));
        list_push(insts, ASM_INST("mov", { REG(rcx), UINT64(rep_count) }));
        list_push(insts, MOVSQ(0xF3));

        list_push(insts, ASM_INST("mov", { REG(rax), REG(rdi) }));
    } else if (lir_type_category(op->result) == TYPE_FLOAT) {
        list_push(insts, ASM_INST("mov", { REG(xmm0), result }));
    } else {
        list_push(insts, ASM_INST("mov", { REG(rax), result }));
    }

    // lir goto 指令已经做过了，cfg 分析就需要这个操作了，不需要在这里多此一举
//    list_push(insts, ASM_INST("jmp", { SYMBOL(c->end_label, true, true) }));

    return insts;
}


list *amd64_lower_goto(closure *c, lir_op *op) {
    list *insts = list_new();
    asm_operand_t *result = amd64_lower_to_asm_operand(op->result);
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
    regs_t used_regs = {.count = 0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_complex_to_asm_operand(op->first, first, &used_regs);
    list_append(insts, temp);
    asm_operand_t *second = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->second, second, &used_regs);
    list_append(insts, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->result, result, &used_regs);
    list_append(insts, temp);

    // imm64 会造成溢出？所以最大只能是?

    reg_t *reg = amd64_lower_next_reg(&used_regs, result->size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("add", { REG(reg), second }));
    list_push(insts, ASM_INST("mov", { result, REG(reg) }));

    return insts;
//    error_exit("[amd64_lower_add] type->result_type not identify");
//    return NULL;
}

// lir GT foo,bar => result
list *amd64_lower_gt(closure *c, lir_op *op) {
    list *insts = list_new();
    regs_t used_regs = {.count = 0};

    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_complex_to_asm_operand(op->first, first, &used_regs);
    list_append(insts, temp);
    asm_operand_t *second = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->second, second, &used_regs);
    list_append(insts, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->result, result, &used_regs);
    list_append(insts, temp);

    // bool = int64 > int64
    reg_t *reg = amd64_lower_next_reg(&used_regs, first->size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("cmp", { REG(reg), second }));

    // setg r/m8
    list_push(insts, ASM_INST("setg", { result }));

    return insts;

    // float
//    error_exit("[amd64_lower_gt] type->result_type not identify, only support TYPE_INT");
//    return NULL;
}


list *amd64_lower_label(closure *c, lir_op *op) {
    list *insts = list_new();
    lir_operand_label *label_operand = op->result->value;
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

    if (lir_type_category(op->result) == TYPE_STRUCT) {
//    regs_t used_regs = {.count = 0};
        // first => result
        // 如果操作数是内存地址，则直接 lea, 如果操作数是寄存器，则不用操作
        // lea first to rax
        asm_operand_t *first_reg = REG(rax);
        asm_operand_t *first = amd64_lower_to_asm_operand(op->first);
        if (first->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
            list_push(insts, ASM_INST("lea", { REG(rax), first }));
        } else {
            first_reg = first;
        }
        // lea result to rdx
        asm_operand_t *result_reg = REG(rdx);
        asm_operand_t *result = amd64_lower_to_asm_operand(op->result);
        if (result->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
            list_push(insts, ASM_INST("lea", { REG(rdx), result }));
        } else {
            result_reg = result;
        }

        // mov rax,rsi, mov rdx,rdi, mov count,rcx
        int rep_count = ceil((float) result->size / QWORD);
        list_push(insts, ASM_INST("mov", { REG(rsi), first_reg }));
        list_push(insts, ASM_INST("mov", { REG(rdi), result_reg }));
        list_push(insts, ASM_INST("mov", { REG(rcx), UINT64(rep_count) }));
        list_push(insts, MOVSQ(0xF3));
        return insts;
    }

    regs_t used_regs = {.count = 0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_complex_to_asm_operand(op->first, first, &used_regs);
    list_append(insts, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->result, result, &used_regs);
    list_append(insts, temp);

    // TODO 是否需要选择 result 和 first 中的较大的一方？
    // TODO 如果是 int 类型，则不能使用，所以提取 size 需要封装一个方法来实现
    reg_t *reg = amd64_lower_next_reg(&used_regs, result->size);
    list_push(insts, ASM_INST("mov", { REG(reg), first }));
    list_push(insts, ASM_INST("mov", { result, REG(reg) }));

    return insts;
}

asm_operand_t *amd64_lower_to_asm_operand(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *v = operand->value;
        if (v->local->stack_frame_offset > 0) {
            return DISP_REG(rbp, -(*v->local->stack_frame_offset), v->size); // amd64 栈空间从大往小递增
        }
        if (v->reg_id > 0) {
            // 如果是 bool 类型
            asm_operand_register_t *reg = amd64_register_find(v->reg_id, v->size);
            return REG(reg);
        }

        return SYMBOL(v->ident, false);
    }

    // 简单立即数
    if (operand->type == LIR_OPERAND_TYPE_IMMEDIATE) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_INT) {
            return UINT32(v->int_value); // 默认使用 UINT32,v->int_value 真的大于 32 位时使用 64
        }
        if (v->type == TYPE_INT8) {
            return UINT8(v->int_value);
        }
        if (v->type == TYPE_INT16) {
            return UINT16(v->int_value);
        }
        if (v->type == TYPE_INT32) {
            return UINT32(v->int_value);
        }
        if (v->type == TYPE_INT64) {
            return UINT64(v->int_value);
        }
        if (v->type == TYPE_FLOAT) {
            return FLOAT32(v->float_value);
        }
        if (v->type == TYPE_BOOL) {
            return UINT8(v->bool_value);
        }
        error_exit("[amd64_lower_to_asm_operand] type immediate not expected");
    }

    if (operand->type == LIR_OPERAND_TYPE_LABEL) {
        lir_operand_label *v = operand->value;
        return LABEL(v->ident);
    }

    error_exit("[amd64_lower_to_asm_operand] operand type not ident");
    return NULL;
}

/**
 * lir_operand 中不能直接转换为 asm_operand 的参数
 * type_string/lir_operand_memory
 * @param operand
 * @param asm_operand
 * @param used_regs
 * @return
 */
list *amd64_lower_complex_to_asm_operand(lir_operand *operand,
                                         asm_operand_t *asm_operand,
                                         regs_t *used_regs) {
    list *insts = list_new();

    if (operand->type == LIR_OPERAND_TYPE_IMMEDIATE) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_STRING) {
            // 生成符号表(TODO 使用字符串 md5 代替)
            char *unique_name = AMD64_DECL_UNIQUE_NAME();
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = unique_name;
            decl->size = strlen(v->string_value) + 1; // + 1 表示 \0
            decl->value = (uint8_t *) v->string_value;
            decl->type = ASM_VAR_DECL_TYPE_STRING;
            list_push(amd64_decl_list, decl);

            // 使用临时寄存器保存结果(会增加一条 lea 指令)
            reg_t *reg = amd64_lower_next_reg(used_regs, QWORD);

            list_push(insts, ASM_INST("lea", { REG(reg), SYMBOL(unique_name, false) }));

            // asm_copy
            ASM_OPERAND_COPY(asm_operand, REG(reg));

            return insts;
        }

        if (v->type == TYPE_FLOAT) {
            char *unique_name = AMD64_DECL_UNIQUE_NAME();
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = unique_name;
            decl->size = QWORD;
            decl->value = (uint8_t *) &v->float_value; // float to uint8
            decl->type = ASM_VAR_DECL_TYPE_FLOAT;
            list_push(amd64_decl_list, decl);

            // 使用临时寄存器保存结果
            reg_t *reg = amd64_lower_next_reg(used_regs, OWORD);

            // movq xmm1,rm64
            list_push(insts, ASM_INST("mov", { REG(reg), SYMBOL(unique_name, false) }));

            ASM_OPERAND_COPY(asm_operand, REG(reg));
            return insts;
        }

        // 匹配失败继续往下走,简单转换里面还有一层 imm 匹配
    }

    if (operand->type == LIR_OPERAND_TYPE_MEMORY) {
        lir_operand_memory *v = operand->value;
        // base 类型必须为 var
        if (v->base->type != LIR_OPERAND_TYPE_VAR) {
            error_exit("[amd64_lir_to_asm_operand] operand type memory, but that base not type var");
        }

        lir_operand_var *var = v->base->value;
        // 如果是寄存器类型就直接返回 disp reg operand
        if (var->reg_id > 0) {
            asm_operand_register_t *reg = amd64_register_find(var->reg_id, var->size);
            asm_operand_t *temp = DISP_REG(reg, v->offset, QWORD);
            ASM_OPERAND_COPY(asm_operand, temp);
            free(temp);
            return insts;
        }

        if (var->local->stack_frame_offset > 0) {
            // 需要占用一个临时寄存器
            reg_t *reg = amd64_lower_next_reg(used_regs, QWORD);

            // 生成 mov 指令（asm_mov）
            list_push(insts, ASM_INST("mov", { REG(reg), DISP_REG(rbp, -(*var->local->stack_frame_offset), QWORD) }));
            asm_operand_t *temp = DISP_REG(reg, v->offset, QWORD);
            ASM_OPERAND_COPY(asm_operand, temp);
            return insts;
        }

        error_exit("[amd64_lir_to_asm_operand]  var cannot reg_id or stack_frame_offset");
    }

    // 按简单参数处理
    asm_operand_t *temp = amd64_lower_to_asm_operand(operand);
    ASM_OPERAND_COPY(asm_operand, temp);
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

reg_t *amd64_lower_next_reg(regs_t *used, uint8_t size) {
    reg_t *r = (reg_t *) amd64_register_find(used->count, size);
    if (r == NULL) {
        error_exit("[amd64_register_find] result null, count: %d, size: %d", used->count, size);
    }
    used->list[used->count++] = r;
    return r;
}


/**
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
reg_t *amd64_lower_next_actual_reg_target(uint8_t used[2], uint8_t size) {
    uint8_t used_index = 0; // 8bit ~ 64bit
    if (size > 8) {
        used_index = 1;
    }
    uint8_t count = used[used_index]++;
    uint8_t reg_index_list[] = {7, 6, 2, 1, 8, 9};
    // 通用寄存器 (0~5 = 6 个)
    if (size <= QWORD && count <= 5) {
        uint8_t reg_index = reg_index_list[count];
        return (reg_t *) amd64_register_find(reg_index, size);
    }

    // 浮点寄存器(0~7 = 8 个)
    if (size > QWORD && count <= 7) {
        return (reg_t *) amd64_register_find(count, size);
    }

    return NULL;
}

list *amd64_lower_fn_begin(closure *c, lir_op *op) {
    list *insts = list_new();
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
    uint8_t used[2] = {0};
    for (int i = 0; i < c->formal_params.count; i++) {
        lir_operand_var *var = c->formal_params.list[i];
        asm_operand_t *target = amd64_lower_to_asm_operand(LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, var));
        reg_t *source_reg = amd64_lower_next_actual_reg_target(used, var->size);
        list_push(insts, ASM_INST("mov", { target, REG(source_reg) }));
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
    // 比较 first 是否等于 second，如果相等就跳转到 result label
    asm_operand_t *first = amd64_lower_to_asm_operand(op->first); // imm uint8
    asm_operand_t *second = amd64_lower_to_asm_operand(op->second); // disp
    asm_operand_t *result = amd64_lower_to_asm_operand(op->result);

    // cmp 指令比较
    list *insts = list_new();
    list_push(insts, ASM_INST("cmp", { second, first }));
    list_push(insts, ASM_INST("je", { result }));

    return insts;
}


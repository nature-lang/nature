#include "amd64.h"
#include "src/type.h"
#include "src/register/register.h"
#include "src/register/amd64.h"
#include "utils/error.h"
#include "utils/helper.h"
#include "src/symbol.h"
#include "src/native/native.h"
#include <assert.h>

amd64_native_fn amd64_native_table[] = {
        [LIR_OPCODE_ADD] = amd64_native_add,
        [LIR_OPCODE_CALL] = amd64_native_call,
        [LIR_OPCODE_BUILTIN_CALL] = amd64_native_call,
        [LIR_OPCODE_RUNTIME_CALL] = amd64_native_call,
        [LIR_OPCODE_LABEL] = amd64_native_label,
        [LIR_OPCODE_RETURN] = amd64_native_return,
        [LIR_OPCODE_BEQ] = amd64_native_cmp_goto,
        [LIR_OPCODE_BAL] = amd64_native_bal,
        [LIR_OPCODE_SGT] = amd64_native_sgt,
        [LIR_OPCODE_MOVE] = amd64_native_mov,
        [LIR_OPCODE_LEA] = amd64_native_lea,
        [LIR_OPCODE_FN_BEGIN] = amd64_native_fn_begin,
        [LIR_OPCODE_FN_END] = amd64_native_fn_end,
};

static amd64_operand_t *amd64_native_operand_var_transform(lir_operand_var *var, uint8_t force_size) {
    uint8_t size = type_base_sizeof(var->type_base);
    if (force_size > 0) {
        size = force_size;
    }
    if (var->reg_index > 0) {
        // 如果是 bool 类型
        reg_t *reg = register_find(var->reg_index, size);
        return REG(reg);
    }

    if (var->decl->stack_offset != 0) {
        return DISP_REG(rbp, *var->decl->stack_offset, size);
    }

    error_exit("[amd64_native_var_operand] var %d not reg_index or stack offset", var->ident);
}

amd64_operation_t *amd64_native_empty_reg(reg_t *reg) {
    // TODO ah/bh/ch/dh 不能这么清理

    reg_t *r = (reg_t *) register_find(reg->index, QWORD);
    if (r == NULL) {
        error_exit("[amd64_native_empty_reg] reg not found, index: %d, size: %d", reg->index, QWORD);
    }
    return ASM_INST("xor", { REG(r), REG(r) });
}

/**
 * LIR_OPCODE_CALL base, param => result
 * base 有哪些形态？
 * http.get() // 外部符号引用,无非就是是否在符号表中
 * sum.n() // 内部符号引用, sum.n 是否属于 var? 所有的符号都被记录为 var
 * test[0]() // 经过计算后的左值，其值最终存储在了一个临时变量中 => temp var
 * 所以 base 最终只有一个形态，那就是 var, 如果 var 在当前 closure_t 有定义，那其至少会被
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
slice_t *amd64_native_call(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();

    uint8_t used[2] = {0};
    amd64_operand_t *result = NULL;
    if (op->result != NULL) {
        result = NEW(amd64_operand_t);
        slice_append(insts, amd64_native_operand_transform(op->result, result, used));
    }

    // 实参传递(封装一个 static 函数处理),
    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->first, first, used));

    uint8_t actual_used[2] = {0};
    // 2. 参数处理  lir_ope code->second;
    lir_operand_actual_param *v = op->second->value;
    slice_t *param_insts = slice_new();
    // 计算 push 总长度，进行栈对齐
    int push_length = 0;
    for (int i = 0; i < v->count; ++i) {
        lir_operand *operand = v->list[i];

        slice_t *temp_insts = slice_new();

        // 如果是 bool, source 存在 1bit, 但是不影响寄存器或者堆栈，寄存器和堆栈在 amd64 位下都是统一 8bit
        uint8_t actual_transform_used[2] = {0};
        amd64_operand_t *source = NEW(amd64_operand_t);
        slice_append(temp_insts, amd64_native_operand_transform(operand, source, actual_transform_used));

        // 根据实际大小选择寄存器
        reg_t *target_reg = amd64_native_fn_next_reg_target(actual_used,
                                                            lir_operand_type_base(operand)); // source 和 target 大小要匹配

        if (target_reg == NULL) {
            // push
            slice_push(temp_insts, ASM_INST("push", { source })); // push 会导致 rsp 栈不对齐
            push_length += source->size;
        } else {
            if (target_reg->size < 8) {
                slice_push(temp_insts, amd64_native_empty_reg(target_reg));
            }

            slice_push(temp_insts, ASM_INST("mov", { REG(target_reg), source }));
        }
        slice_append(temp_insts, param_insts);
        param_insts = temp_insts;
    }
    // 栈对齐
    uint64_t diff_length = memory_align(push_length, 16) - push_length;
    if (diff_length > 0) {
        slice_push(insts, ASM_INST("sub", { REG(rsp), UINT8(diff_length) }));
    }

    slice_append(insts, param_insts);

    if (first->type == ASM_OPERAND_TYPE_SYMBOL &&
        is_print_symbol(((amd64_operand_symbol_t *) first->value)->name)) {
        // x. TODO 仅调用变成函数之前，需要将 rax 置为 0, 如何判断调用目标是否为变长函数？
        slice_push(insts, ASM_INST("mov", { REG(rax), UINT32(0) }));
    }


    // 3. 调用 call 指令(处理地址), 响应的结果在 rax 中
    slice_push(insts, ASM_INST("call", { first }));

    // 4. 响应处理(取出响应值传递给 result)
    if (op->result != NULL) {
        if (lir_operand_type_base(op->result) == TYPE_FLOAT) {
            slice_push(insts, ASM_INST("mov", { result, REG(xmm0) }));
        } else {
            slice_push(insts, ASM_INST("mov", { result, REG(rax) }));
        }
    }

    return insts;
}

/**
 * 核心问题是: 在结构体作为返回值时，当外部调用将函数的返回地址作为参数 rdi 传递给函数时，
 * 根据 ABI 规定，函数操作的第一步就是对 rdi 入栈，但是当 native return 时,我并不知道 rdi 被存储在了栈的什么位置？
 * 但是实际上是能够知道的，包括初始化时 sub rbp,n 的 n 也是可以在寄存器分配阶段就确定下来的。
 * n 的信息作为 closure_t 的属性存储在 closure_t 中，如何将相关信息传递给 native ?, 参数 1 改成 closure_t?
 * 在结构体中 temp_var 存储的是结构体的起始地址。不能直接 return 起始地址，大数据会随着函数栈帧消亡。 而是将结构体作为整个值传递。
 * 既然已经知道了结构体的起始位置，以及隐藏参数的所在栈帧。 就可以直接进行结构体返回值的构建。
 * @param c
 * @param ast
 * @return
 */
slice_t *amd64_native_return(closure_t *c, lir_op *op) {
    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    amd64_operand_t *result = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->result, result, used));

    if (lir_operand_type_base(op->result) == TYPE_FLOAT) {
        slice_push(insts, ASM_INST("mov", { REG(xmm0), result }));
    } else {
        slice_push(insts, ASM_INST("mov", { REG(rax), result }));
    }

    return insts;
}


slice_t *amd64_native_bal(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    amd64_operand_t *result = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->result, result, used));
    slice_push(insts, ASM_INST("jmp", { result }));
    return insts;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
slice_t *amd64_native_add(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    // 参数转换
    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_t *temp = amd64_native_operand_transform(op->first, first, used);
    slice_append(insts, temp);
    amd64_operand_t *second = NEW(amd64_operand_t);
    temp = amd64_native_operand_transform(op->second, second, used);
    slice_append(insts, temp);

    amd64_operand_t *result = NEW(amd64_operand_t);
    temp = amd64_native_operand_transform(op->result, result, used);
    slice_append(insts, temp);

    // 并没有强制要求寄存器？但是也没有做冗余的 mov 的强制消除？
    // 任何一个值都不是必定能分配到寄存器的！除非配置了 use kind?
    reg_t *reg = amd64_native_next_reg(used, lir_operand_sizeof(op->result));
    slice_push(insts, ASM_INST("mov", { REG(reg), first }));
    slice_push(insts, ASM_INST("add", { REG(reg), second }));
    slice_push(insts, ASM_INST("mov", { result, REG(reg) }));

    return insts;
//    error_exit("[amd64_native_add] code->result_type not identify");
//    return NULL;
}

// lir GT foo,bar => result
slice_t *amd64_native_sgt(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_t *temp = amd64_native_operand_transform(op->first, first, used);
    slice_append(insts, temp);

    amd64_operand_t *second = NEW(amd64_operand_t);
    temp = amd64_native_operand_transform(op->second, second, used);
    slice_append(insts, temp);

    // result 用于 setg, 必须强制 byte 大小
    amd64_operand_t *result = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->result, result, used));

    // bool = int64 > int64
    reg_t *reg = amd64_native_next_reg(used, first->size);
    slice_push(insts, ASM_INST("mov", { REG(reg), first }));
    slice_push(insts, ASM_INST("cmp", { REG(reg), second }));

    // setg r/m8(TODO 强制要求 1bit 用于 setg)
    slice_push(insts, ASM_INST("setg", { result }));

    return insts;

    // float
//    error_exit("[amd64_native_gt] code->result_type not identify, only support TYPE_INT");
//    return NULL;
}


slice_t *amd64_native_label(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    lir_operand_symbol_label *label_operand = op->result->value;
    slice_push(insts, ASM_INST("label", { SYMBOL(label_operand->ident, label_operand->is_local) }));
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
slice_t *amd64_native_mov(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();

    uint8_t used[2] = {0};

    // 参数转换
    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_t *temp = amd64_native_operand_transform(op->first, first, used);
    slice_append(insts, temp);

    amd64_operand_t *result = NEW(amd64_operand_t);
    temp = amd64_native_operand_transform(op->result, result, used);
    slice_append(insts, temp);

    uint8_t size = result->size;

    reg_t *reg = amd64_native_next_reg(used, size);
    slice_push(insts, ASM_INST("mov", { REG(reg), first }));
    slice_push(insts, ASM_INST("mov", { result, REG(reg) }));

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
slice_t *amd64_native_operand_transform(lir_operand *operand,
                                        amd64_operand_t *asm_operand,
                                        uint8_t used[2]) {
    slice_t *insts = slice_new();

    if (operand->type == LIR_OPERAND_TYPE_IMM) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_STRING_RAW) {
            // 生成符号表(TODO 使用字符串 md5 代替)
            char *unique_name = NATIVE_VAR_DECL_UNIQUE_NAME();
            native_var_decl_t *decl = NEW(native_var_decl_t);
            decl->name = unique_name;
            decl->size = strlen(v->string_value) + 1; // + 1 表示 \0
            decl->value = (uint8_t *) v->string_value;
//            decl->code = ASM_VAR_DECL_TYPE_STRING;
            slice_push(native_var_decls, decl);

            // 使用临时寄存器保存结果(会增加一条 lea 指令)
            reg_t *reg = amd64_native_next_reg(used, QWORD);

            slice_push(insts, ASM_INST("lea", { REG(reg), SYMBOL(unique_name, false) }));

            // asm_copy
            ASM_OPERAND_COPY(asm_operand, REG(reg));
        } else if (v->type == TYPE_FLOAT) {
            char *unique_name = NATIVE_VAR_DECL_UNIQUE_NAME();
            native_var_decl_t *decl = NEW(native_var_decl_t);
            decl->name = unique_name;
            decl->size = QWORD;
            decl->value = (uint8_t *) &v->float_value; // float to uint8
//            decl->code = ASM_VAR_DECL_TYPE_FLOAT;
            slice_push(native_var_decls, decl);

            // 使用临时寄存器保存结果
            reg_t *reg = amd64_native_next_reg(used, OWORD);

            // movq xmm1,rm64
            slice_push(insts, ASM_INST("mov", { REG(reg), SYMBOL(unique_name, false) }));

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
            error_exit("[amd64_native_to_asm_operand] code immediate not expected");
        }
        return insts;
    }

    if (operand->type == LIR_OPERAND_TYPE_ADDR) {
        lir_operand_addr *v = operand->value;
        // base 类型必须为 var
        if (v->base->type != LIR_OPERAND_TYPE_VAR) {
            error_exit("[amd64_lir_to_asm_operand] operand code memory, but that base not code var");
        }

        lir_operand_var *base_var = v->base->value;

        // rbp 存储了 base,
        if (base_var->decl->stack_offset == 0) {
            error_exit("[amd64_lir_to_asm_operand]  var cannot stack_frame_offset in var %s", base_var->ident);
        }
        // 需要占用一个临时寄存器
        reg_t *reg = amd64_native_next_reg(used, QWORD);

        // 如果设置了 indirect_addr, 则编译成 [rxx+offset]
        // 否则应该编译成 ADD  rxx -> offset, asm_operand 配置成 rxx
        if (v->indirect_addr) {
            amd64_operand_t *base_addr_operand = amd64_native_operand_var_transform(base_var, 0);
            // 生成 mov 指令（asm_mov）
            slice_push(insts, ASM_INST("mov", { REG(reg), base_addr_operand }));

            amd64_operand_t *temp = DISP_REG(reg, v->offset, type_base_sizeof(v->type_base));
            ASM_OPERAND_COPY(asm_operand, temp);
        } else {
            slice_push(insts, ASM_INST("add", { REG(reg), UINT32(v->offset) }));
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
            assert(v->decl->type.point > 0 && "indirect addr var must point type");

            reg_t *reg = amd64_native_next_reg(used, QWORD);
            amd64_operand_t *var_operand = amd64_native_operand_var_transform(v, 0);
            slice_push(insts, ASM_INST("mov", { REG(reg), var_operand }));

            // 解引用
            ASM_OPERAND_COPY(asm_operand, INDIRECT_REG(reg, type_base_sizeof(v->type_base)));
            return insts;
        } else {
            ASM_OPERAND_COPY(asm_operand, amd64_native_operand_var_transform(v, 0))
            return insts;
        }
    }

    if (operand->type == LIR_OPERAND_TYPE_SYMBOL_VAR) {
        lir_operand_symbol_var *v = operand->value;
        ASM_OPERAND_COPY(asm_operand, SYMBOL(v->ident, false));
        // symbol 也要有 size, 不然无法选择合适的寄存器进行 mov
        asm_operand->size = type_base_sizeof(v->type);
        return insts;
    }

    if (operand->type == LIR_OPERAND_TYPE_SYMBOL_LABEL) {
        lir_operand_symbol_label *v = operand->value;
        ASM_OPERAND_COPY(asm_operand, SYMBOL(v->ident, false));
        return insts;
    }

    return insts;
}

slice_t *amd64_native_op(closure_t *c, lir_op *op) {
    amd64_native_fn fn = amd64_native_table[op->code];
    if (fn == NULL) {
        error_exit("[amd64_native_op] amd64_native_table not found fn: %d", op->code);
    }
    return fn(c, op);
}

reg_t *amd64_native_next_reg(uint8_t used[2], uint8_t size) {
    uint8_t used_index = 0; // 8bit ~ 64bit
    if (size > 8) {
        used_index = 1;
    }
    uint8_t count = used[used_index]++;

    reg_t *r = (reg_t *) register_find(count, size);
    if (r == NULL) {
        error_exit("[register_find] not found, count: %d, size: %d", count, size);
    }
    return r;
}





/**
 *
 * @param c
 * @param op
 * @return
 */
slice_t *amd64_native_fn_begin(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    // 计算堆栈信息(倒序 )
    list_node *current = list_last(c->var_decls); // rear 为 empty 占位
    while (current != NULL) {
        lir_var_decl *var = current->value;
        // 局部变量不需要考虑什么最小值的问题，直接网上涨就好了
        uint8_t size = type_base_sizeof(var->type.base);
        c->stack_slot += size;
        *var->stack_offset = -(c->stack_slot); // rbp-n, 所以这里取负数

        current = current->prev;
    }

    // 一次堆栈对齐,对齐后再继续分配栈空间，分配完成后需要进行二次栈对齐。
    c->stack_slot = memory_align(c->stack_slot, 16);

    // 部分形参需要占用一部分栈空间(amd64 架构下统一使用 8byte)， 按顺序使用堆栈即可
    // 还有一部分 push
    uint8_t used[2] = {0};
    int16_t stack_param_offset = 16; // 16byte 起点

    current = c->formal_params->front;
    while (current->value != NULL) {
        lir_var_decl *var = current->value;

        // 第一步就是将参数从寄存器分配到 stack 中，所以必须为形参分配一个 stack slot 才能做 mov
        reg_t *reg = amd64_native_fn_next_reg_target(used, var->type.base);
        if (reg != NULL) {
            // param 分配了寄存器
            // rbp-x
            c->stack_slot += QWORD;
            *var->stack_offset = -(c->stack_slot); // 栈底，负数
        } else {
            // rbp+x
            *var->stack_offset = stack_param_offset; // 正数
            stack_param_offset += QWORD;
        }

        current = current->succ;
    }

    // 二次对齐
    c->stack_slot = memory_align(c->stack_slot, 16);

    slice_push(insts, ASM_INST("push", { REG(rbp) }));
    slice_push(insts, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    slice_push(insts, ASM_INST("sub", { REG(rsp), UINT32(c->stack_slot) }));

    // 形参入栈
    slice_append(insts, amd64_native_fn_formal_params(c));

    return insts;
}

/**
 * 拼接在函数尾部，实现结尾块自然退出。
 * @param c
 * @return
 */
slice_t *amd64_native_fn_end(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    slice_push(insts, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    slice_push(insts, ASM_INST("pop", { REG(rbp) }));
    slice_push(insts, ASM_INST("ret", {}));
    return insts;
}

slice_t *amd64_native_fn_formal_params(closure_t *c) {
    slice_t *insts = slice_new();
    // 已经在栈里面的就不用管了，只取寄存器中的。存放在 lir_var 中的 stack_offset 中即可
    uint8_t formal_used[2] = {0};
    list_node *current = c->formal_params->front;
    while (current->value != NULL) {
        lir_var_decl *var_decl = current->value;
        reg_t *source_reg = amd64_native_fn_next_reg_target(formal_used, var_decl->type.base);
        if (source_reg == NULL) {
            continue;
        }

        // 直接使用 var 转换
        amd64_operand_t *target = DISP_REG(rbp, *var_decl->stack_offset, type_base_sizeof(var_decl->type.base));
        slice_push(insts, ASM_INST("mov", { target, REG(source_reg) }));

        current = current->succ;
    }
    return insts;
}

slice_t *amd64_native_closure(closure_t *c) {
    slice_t *insts = slice_new();

    // 遍历 block
    for (int i = 0; i < c->blocks->count; ++i) {
        lir_basic_block *block = c->blocks->take[i];
        slice_append(insts, amd64_native_block(c, block));
    }

    return insts;
}


slice_t *amd64_native_block(closure_t *c, lir_basic_block *block) {
    slice_t *insts = slice_new();
    list_node *current = block->operations->front;
    while (current->value != NULL) {
        lir_op *op = current->value;
        slice_append(insts, amd64_native_op(c, op));
        current = current->succ;
    }
    return insts;
}


slice_t *amd64_native_cmp_goto(closure_t *c, lir_op *op) {
    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->first, first, used));

    amd64_operand_t *second = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->second, second, used));

    amd64_operand_t *result = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->result, result, used));

    // cmp 指令比较
    reg_t *reg = amd64_native_next_reg(used, first->size);
    slice_push(insts, ASM_INST("mov", { REG(reg), first }));
    slice_push(insts, ASM_INST("cmp", { REG(reg), second }));
    slice_push(insts, ASM_INST("je", { result }));

    return insts;
}

slice_t *amd64_native_lea(closure_t *c, lir_op *op) {
    if (op->first->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[amd64_native_lead] first operand code not LIR_OPERAND_TYPE_VAR");
    }
    if (op->result->type != LIR_OPERAND_TYPE_VAR) {
        error_exit("[amd64_native_lead] result operand code not LIR_OPERAND_TYPE_VAR");
    }

    slice_t *insts = slice_new();
    uint8_t used[2] = {0};

    amd64_operand_t *first = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->first, first, used));

    amd64_operand_t *result = NEW(amd64_operand_t);
    slice_append(insts, amd64_native_operand_transform(op->result, result, used));


    reg_t *reg = amd64_native_next_reg(used, result->size);
    slice_push(insts, ASM_INST("lea", { REG(reg), first }));
    slice_push(insts, ASM_INST("mov", { result, REG(reg) }));
    return insts;
}

uint8_t amd64_formal_min_stack(uint8_t size) {
    if (size < 8) {
        size = 8;
    }
    return size;
}

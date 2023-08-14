#include "amd64_abi.h"
#include "src/register/amd64.h"

linked_t *amd64_struct_mov(type_t *type, lir_operand_t *src, lir_operand_t *dst) {

}

/**
 * call test(arg1, aeg2, ...) -> result
 * @param c
 * @param op
 * @return
 */
linked_t *amd6_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();

    slice_t *args = op->second->value;
    lir_operand_t *res = op->output;

    uint8_t *onstack = mallocz(args->count + 1);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    uint8_t need_reg_count = 0;
    uint size = 0;
    uint align = 0; // 尤其是当参数是结构体时，需要能够知道结构体的对齐规则
    // 用于记录 onstack 的类型，类型 1 按照 8 字节对齐，类型 2 需要按照 16 字节对齐
    uint stack_adjust = 0;

    // - 确定返回值的类型，如果其是 memory, 则需要将其添加到 args 的头部(通过内存地址)
    for (int i = 0; i < args->count; ++i) {
        amd64_mode_t mode = amd64_arg_classify(c, args->take[i], &size, &align, &need_reg_count);
        if (mode == AMD64_MODE_SSE && (sse_reg_count + need_reg_count) <= 8) {
            onstack[i] = 0;
            sse_reg_count += need_reg_count;
        } else if (mode == AMD64_MODE_INTEGER && (int_reg_count + need_reg_count) <= 6) {
            onstack[i] = 0;
            int_reg_count += need_reg_count;
        } else {
            // 参数 size 过大，或者是参数数量过多，此时需要将 push 到栈里面
            if (align == 16 && (stack_adjust &= 16)) {
                onstack[i] = 2;
                stack_adjust = 0;
            } else {
                onstack[i] = 1;
            }

            stack_adjust += size;
        }
    }

    stack_adjust %= 16;

    // 本次参数传递的虚拟栈的大小(最终的栈需要按照 16/8 byte 对齐)
    // 0 表示第一个栈的参数的位置, stack_arg_size 总是按照 8byte 的倍数增长
    uint stack_arg_size = 0;
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越考右的参数越优先入栈)
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];

        amd64_mode_t mode = amd64_arg_classify(c, arg, &size, &align, &need_reg_count);
        assert(size > 0);
        if (onstack[i] == 0) {
            continue;
        }

        // TODO  暂时忽略 adjust 这个东西， 不知道是用来干嘛的
        // 此处 stack_adjust 用来保障 sse 寄存器 16byte 对齐
        // stack_adjust = 0 标识栈已经是 16byte 对齐了
        if (stack_adjust) {
            stack_arg_size += 8; // 栈边界按照 16byte 对齐
            stack_adjust = 0;
        }

        if (onstack[i] == 2) { // stack 已经栈对齐了
            stack_adjust = 1;
        }

        // 提取
        type_t arg_type = lir_operand_type(arg);
        if (arg_type.kind == TYPE_STRUCT) {
            // struct 存不下，所以是绝对不会分配寄存器的，只能溢出。
            assert(arg->assert_type == LIR_OPERAND_VAR);
            lir_op_t *binary_op = lir_op_new(LIR_OPCODE_SUB,
                                             operand_new(LIR_OPERAND_REG, rsp),
                                             int_operand(size),
                                             operand_new(LIR_OPERAND_REG, rsp));
            linked_push(result, binary_op);


            // TODO arg 此时是什么东西？ a 里面存储的是一个什么东西，具体表现形式是？
            // TODO 寄存器分配还没有完成，a 具体的 rbp 偏移。
            // arg 的变现形式如下 [rbp + 0x0]，其就是一个栈帧地址。
            // 所以无法直接基于其做进一步的偏移计算，应该是
            // 将 arg addr 移动到 indirect_addr 中()

            // TODO 读取 arg 的地址。不如基于 lower mov 进行优化实现？
            // 尽量借助虚拟寄存器来实现移动操作,
            // 基于 indirect arg 不停的像 rsp 部分 mov 即可，但是不能直接使用 rsp 指针
//            lir_operand_t *arg_ref = temp_var_operand(c->module, type_ptrof(arg_type));
//            linked_push(result, lir_op_lea(arg_ref, arg));

            // 既然 size 总是 8byte 对齐的？那不妨不停的调用 push?

            // lir_operand_t *dst_ref = // TODO 现在还能调用函数吗，调用函数不就出问题了？？？
            // rdi 寄存器都被用完了呀，这里不能再用了！改用内存传输指令进行传输。
            // TODO 调用 memmove 进行移动

            // TODO 调用 momv

        } else if (is_float(arg_type.kind)) {
            // TODO
        } else {
            // TODO
        }

        // TODO push 相当于是基于 rsp 做一些增高操作了。所以这里可以直接操作 rsp

        // 感觉 arg 不同的类型和 size, 会分配不同的尺寸

        // 基于 arg 的 type 进行内存 push 操作(TODO type 是个啥？)
        // TODO 不止需要 kind 还需要 size? 既然这样，不如统一
        // 已经确定了参数要入栈，关键是怎么入栈。

    }



    // - 处理需要通过 stack 传递的参数，进行入栈处理


    return result;
}

amd64_mode_t amd64_arg_classify(closure_t *c, lir_operand_t *arg, uint *size, uint *align, uint8_t *need_reg_count) {
    return AMD64_MODE_NULL;
}

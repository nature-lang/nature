#include "riscv64_abi.h"
#include "src/register/arch/riscv64.h"

/**
 * 递归分析结构体字段，确定传递方式
 * rc[0]: 寄存器数量，-1 表示不能通过寄存器传递
 * rc[1], rc[2]: 分别表示两个寄存器类型 (RC_INT 或 RC_FLOAT)
 * fieldofs[1], fieldofs[2]: 字段偏移和类型信息
 */
static void reg_pass_rec(type_t *type, int *rc, int *field_offset, int offset) {
    if (type->kind == TYPE_STRUCT) {
        type_struct_t *type_struct = type->struct_;

        // 遍历结构体的每一个属性进行分类
        for (int i = 0; i < type_struct->properties->length; i++) {
            struct_property_t *p = ct_list_value(type_struct->properties, i);
            type_t element_type = p->type;
            uint16_t element_size = type_sizeof(element_type);
            uint16_t element_align = element_size;
            if (p->type.kind == TYPE_STRUCT) {
                element_align = type_struct_alignof(p->type.struct_);
            } else if (p->type.kind == TYPE_ARR) {
                element_align = type_sizeof(p->type.array->element_type);
            }
            offset = align_up(offset, element_align);
            reg_pass_rec(&element_type, rc, field_offset, offset);

            if (rc[0] < 0) {
                break; // 如果已经不能通过寄存器传递，提前退出
            }

            offset += element_size;
        }
    } else if (type->kind == TYPE_ARR) {
        // 数组处理：小数组可能通过寄存器传递
        if (type->array->length < 0 || type->array->length > 2) {
            rc[0] = -1; // 无法通过寄存器进行参数传递
        } else {
            // 小数组处理
            type_t element_type = type->array->element_type;
            uint8_t element_size = type_sizeof(element_type);
            // 对数组元素进行分析
            reg_pass_rec(&element_type, rc, field_offset, offset);

            // 如果 rc[0] > 2 表示单个数组元素需要超过 2 个寄存器， 表示无法通过寄存器传递
            // 如果 rc[0] == 2 && type->array->length > 1 表示数组元素需要超过 2 个寄存器，则无法通过寄存器传递
            // RISC-V最多只能用2个寄存器传递一个参数
            if (rc[0] > 2 || (rc[0] == 2 && type->array->length > 1)) {
                rc[0] = -1; // 标记为无法通过寄存器传递
            } else if (type->array->length == 2 && rc[0] && rc[1] == TYPE_FLOAT) {
                // 元素可以通过浮点寄存器传递, 并且数组中有两个元素
                // rc[0] 记录了当前寄存器的数量，也就是 index, 通过 rc[rc0] 可以访问对应寄存器位置中使用浮点寄存器还是整型寄存器
                rc[++rc[0]] = TYPE_FLOAT; // 增加整体寄存器的数量
                // 记录数组在 struct 中的 offset
                field_offset[rc[0]] = ((offset + element_size) << 8) | (element_type.kind & 0xFF);
            } else if (type->array->length == 2) {
                rc[0] = -1; // 仅仅双浮点数组进行特殊优化
            }
        }
    } else if (rc[0] == 2 || rc[0] < 0) {
        // 寄存器使用超过单次的上限，无需继续分析
        rc[0] = -1;
    } else if (rc[0] == 0 || rc[1] == TYPE_FLOAT || is_float(type->kind)) {
        // !rc[0] 表示寄存器数量为0, 表示当前参数可以通过寄存器传递
        rc[++rc[0]] = is_float(type->kind) ? TYPE_FLOAT : TYPE_INT;
        field_offset[rc[0]] = (offset << 8) | (type->kind & 0xFF);
    } else {
        rc[0] = -1;
    }
}

/**
 * 分析类型是否可以通过寄存器传递
 */
static void reg_pass(type_t *type, int *prc, int *field_offset, int *areg) {
    prc[0] = 0;
    reg_pass_rec(type, prc, field_offset, 0);

    // 浮点寄存器数量不足时也进行退化
    bool back_int = prc[0] <= 0 ||
                    (prc[0] == 2 && prc[1] == TYPE_FLOAT && prc[2] == TYPE_FLOAT && areg[1] >= 7);


    // 二次计算
    if (back_int) {
        uint16_t align = type_alignof(*type);
        uint16_t size = type_sizeof(*type);
        prc[0] = (size + 7) >> 3; //  计算需要的寄存器数量(寄存器大小按照 8byte 计算)
        prc[1] = prc[2] = TYPE_INT; // 使用通用 int 寄存器 进行传递
        field_offset[1] = (0 << 8) | (size <= 1 ? TYPE_INT8 : size <= 2 ? TYPE_INT16
                                                      : size <= 4       ? TYPE_INT32
                                                                        : TYPE_INT64);

        field_offset[2] = (8 << 8) | (size <= 9 ? TYPE_INT8 : size <= 10 ? TYPE_INT16
                                                      : size <= 12       ? TYPE_INT32
                                                                         : TYPE_INT64);
    }
}


typedef struct {
    int64_t main; // 原始 pos
    int64_t main_filed_offset; // 扁平化处理 offset
    int64_t attach; // 单个参数需要多个寄存器位置时附加
    int64_t attach_filed_offset;
} pos_item_t;

/**
 * 根据RISC-V 64 ABI计算参数位置
 */
static int64_t riscv64_pcs_aux(int64_t n, type_t *args_types, pos_item_t *args_pos) {
    int areg[2] = {0, 0}; // areg[0]: 整数寄存器索引, areg[1]: 浮点寄存器索引
    int64_t ns = 32; // 下一个栈偏移

    for (int i = 0; i < n; i++) {
        uint16_t size = type_sizeof(args_types[i]);
        uint16_t align = type_alignof(args_types[i]);

        // 初始化 args_pos
        args_pos[i].main = -1;
        args_pos[i].attach = -1;

        // 数组参数总是作为指针进行处理
        if (args_types[i].kind == TYPE_ARR) {
            size = align = POINTER_SIZE;
        }

        // 处理大型结构体(超过 16byte)或者数组, 直接基于指针进行处理
        if (size > 16 || args_types[i].kind == TYPE_ARR) {
            if (areg[0] < 8) {
                // 整数寄存器足够
                args_pos[i].main = (areg[0]++ << 1) | 1; // 添加指针标记
                args_pos[i].main_filed_offset = 0 << 8 | TYPE_ANYPTR; // 通过指针传递
            } else {
                args_pos[i].main = ns | 1; // 1 表示指针标记
                args_pos[i].main_filed_offset = 0 << 8 | TYPE_ANYPTR;
                ns += POINTER_SIZE;
            }
            continue;
        }

        // 使用更精确的扁平化寄存器分配
        int prc[3] = {0};
        int field_offset[3] = {0};
        reg_pass(&args_types[i], prc, field_offset, areg);
        args_pos[i].main_filed_offset = field_offset[1];
        int nregs = prc[0];

        if (nregs > 1) {
            args_pos[i].attach_filed_offset = field_offset[2];
        }

        if (size == 0) {
            args_pos[i].main = 0;
        } else if ((prc[1] == TYPE_INT && areg[0] >= 8) ||
                   (prc[1] == TYPE_FLOAT && areg[1] >= 8) ||
                   (nregs == 2 && prc[1] == TYPE_FLOAT && prc[2] == TYPE_FLOAT && areg[1] >= 7) || // 可用 float 寄存器不足
                   (nregs == 2 && prc[1] != prc[2] && (areg[1] >= 8 || areg[0] >= 8))) {
            // 栈传递, 不考虑 offset
            if (align < 8) {
                align = 8;
            }

            type_kind main_kind = field_offset[1] & 0xFF;

            args_pos[i].main = ns;
            ns += align_up(type_kind_sizeof(main_kind), align);

            if (nregs == 2) {
                type_kind attach_kind = field_offset[2] & 0xFF;
                args_pos[i].attach = ns;
                ns += align_up(type_kind_sizeof(attach_kind), align);
            }
        } else {
            // 寄存器传递
            if (prc[1] == TYPE_FLOAT) {
                args_pos[i].main = 16 + (areg[1]++ << 1); // 浮点寄存器从 16 开始计算(16 ~ 31)
            } else {
                args_pos[i].main = areg[0]++ << 1; // 使用整数寄存器
            }

            // 双寄存器使用
            if (nregs == 2) {
                if (prc[2] == TYPE_FLOAT && areg[1] < 8) {
                    // 第二个寄存器也是浮点
                    args_pos[i].attach = (16 + (areg[1]++ << 1));
                } else if (prc[2] == TYPE_INT && areg[0] < 8) {
                    // 第二个寄存器是整数
                    args_pos[i].attach = areg[0]++ << 1;
                } else {
                    // 第二个参数部分通过栈传递
                    args_pos[i].attach = ns; // 标记栈传递
                    ns += POINTER_SIZE; // 直接对齐即可
                }
            }
        }
    }

    // 确保栈大小16字节对齐
    return align_up(ns, 16);
}

/**
 * 根据RISC-V 64 ABI将参数从寄存器传递到局部变量
 */
static linked_t *riscv64_lower_params(closure_t *c, slice_t *param_vars) {
    assert(c != NULL);
    assert(param_vars != NULL);

    linked_t *result = linked_new();

    if (param_vars->count == 0) {
        return result;
    }

    pos_item_t *args_pos = mallocz((param_vars->count + 1) * sizeof(pos_item_t));
    type_t *args_type = mallocz((param_vars->count + 1) * sizeof(type_t));
    assert(args_pos != NULL);
    assert(args_type != NULL);


    type_t return_type = type_kind_new(TYPE_VOID);
    if (c->return_operand) {
        return_type = lir_operand_type(c->return_operand);
    }

    args_type[0] = return_type;
    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        assert(var != NULL);
        args_type[i + 1] = var->type;
    }

    int64_t stack_offset;
    if (return_type.kind == TYPE_STRUCT && type_sizeof(return_type) > 16) {
        stack_offset = riscv64_pcs_aux(param_vars->count + 1, args_type, args_pos);

        args_pos = args_pos + 1;
        args_type = args_type + 1;
    } else {
        args_pos = args_pos + 1;
        args_type = args_type + 1;
        stack_offset = riscv64_pcs_aux(param_vars->count, args_type, args_pos);
    }

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;
        lir_operand_t *dst_param = operand_new(LIR_OPERAND_VAR, var);

        int64_t main_pos = args_pos[i].main;
        // extract filed kind and offset
        type_kind main_kind = args_pos[i].main_filed_offset & 0xFF;
        int64_t main_offset = args_pos[i].main_filed_offset >> 8;
        assert(main_offset == 0);

        int64_t attach_pos = args_pos[i].attach; // 结构体，并且 size < 16 > 8 时需要使用 attached 记录
        type_kind attach_kind = args_pos[i].attach_filed_offset & 0xFF;
        int64_t attach_offset = args_pos[i].attach_filed_offset >> 8;
        assert(main_pos >= 0);

        if (main_pos < 16) {
            uint8_t reg_index = A0->index + (main_pos >> 1);
            reg_t *reg = reg_select(reg_index, param_type.kind);
            assert(reg);

            // 结构体特殊处理，如果结构体通过寄存器进行传递，则 callee params 需要为结构体申请足够的栈空间并将 dst var 指向该栈空间
            // 然后需要从寄存器中获取数据并移动到结构体中, 结构体特殊处理，需要申请栈空间
            if (param_type.kind == TYPE_STRUCT && !(main_pos & 1)) {
                linked_push(result, lir_stack_alloc(c, param_type, dst_param));
                // 将数据从寄存器移动到结构体栈中
                lir_operand_t *lo_src_reg = operand_new(LIR_OPERAND_REG, reg_select(A0->index + (main_pos >> 1), main_kind));
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(main_kind), dst_param, 0);

                linked_push(result, lir_op_move(dst, lo_src_reg));
            } else {
                lir_operand_t *src = operand_new(LIR_OPERAND_REG, reg);
                linked_push(result, lir_op_move(dst_param, src));
            }
        } else if (main_pos < 32) { // 通过浮点寄存器传递
            int64_t reg_index = FA0->index + ((main_pos - 16) >> 1);

            if (param_type.kind == TYPE_STRUCT && !(main_pos & 1)) {
                linked_push(result, lir_stack_alloc(c, param_type, dst_param));
                // 将数据从寄存器移动到结构体栈中
                lir_operand_t *lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(main_kind), dst_param, 0);
                linked_push(result, lir_op_move(dst, lo_reg_operand));
            } else {
                // 通过浮点寄存器传递
                lir_operand_t *src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.kind));
                linked_push(result, lir_op_move(dst_param, src));
            }
        } else {
            int64_t sp_offset = ((main_pos & ~1) - 32) + 16; // src sp offset
            lir_operand_t *src = lir_stack_operand(c->module, sp_offset, type_kind_sizeof(main_kind), main_kind);

            // 参数通过栈传递。
            if (param_type.kind == TYPE_STRUCT && !(main_pos & 1)) {
                // 触发扁平化, 此时栈空间依旧由 callee 分配
                linked_push(result, lir_stack_alloc(c, param_type, dst_param)); // 分配栈空间
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(main_kind), dst_param, 0);

                linked_push(result, lir_op_move(dst, src));
            } else {
                // src 存储在栈中，dst 存储在 var 中
                linked_push(result, lir_op_move(dst_param, src));
            }
        }

        // 特殊结构体(<16byte) 时 attach_pos 存在
        if (attach_pos >= 0) {
            assert(param_type.kind == TYPE_STRUCT);

            if (attach_pos < 16) {
                lir_operand_t *hi_src = operand_new(LIR_OPERAND_REG,
                                                    reg_select(A0->index + (attach_pos >> 1), attach_kind));
                // 默认 8 byte 为最小单位
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(attach_kind), dst_param, attach_offset);
                linked_push(result, lir_op_move(dst, hi_src));
            } else if (attach_pos < 32) {
                // 将数据从寄存器移动到结构体栈中
                lir_operand_t *hi_src = operand_new(LIR_OPERAND_REG, reg_select(FA0->index + ((attach_pos - 16) >> 1), attach_kind));

                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(attach_kind), dst_param, attach_offset);
                linked_push(result, lir_op_move(dst, hi_src));
            } else {
                // 超过 32 通过栈进行传递, 也就是寄存器与栈组合
                int64_t sp_offset = (attach_pos & ~1) - 32 + 16;
                lir_operand_t *hi_src = lir_stack_operand(c->module, sp_offset, type_kind_sizeof(attach_kind), attach_kind);
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(attach_kind), dst_param, attach_offset);
                linked_push(result, lir_op_move(dst, hi_src));
            }
        }
    }

    return result;
}

linked_t *riscv64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    linked_push(result, op); // 将 FN_BEGIN 放在最前端

    slice_t *params = op->output->value;

    if (c->return_operand) {
        // 移除最后一个参数，其指向了 return_operand
        slice_remove(params, params->count - 1);

        type_t return_type = lir_operand_type(c->return_operand);

        if (return_type.kind == TYPE_ARR || type_sizeof(return_type) > 16) {
            assert(return_type.kind == TYPE_STRUCT || return_type.kind == TYPE_ARR);

            // 大于 16 字节的结构体通过内存返回，A0 中存储返回地址
            lir_operand_t *return_operand = c->return_operand;
            assert(return_operand->assert_type == LIR_OPERAND_VAR);

            // 从 A0 寄存器获取返回地址引用
            lir_operand_t *A0_reg = lir_reg_operand(A0->index, TYPE_ANYPTR);
            linked_push(result, lir_op_move(return_operand, A0_reg));
        } else {
            // 将会触发扁平化处理，不需要做什么特殊处理, 但是需要创建 output, 保证 reg 可用
            if (return_type.kind == TYPE_STRUCT) {
                linked_push(result, lir_stack_alloc(c, return_type, c->return_operand));
            } else {
                linked_push(result, lir_op_output(LIR_OPCODE_NOP, c->return_operand));
            }
        }
    }

    linked_concat(result, riscv64_lower_params(c, params));
    op->output->value = slice_new();
    return result;
}

linked_t *riscv64_lower_call(closure_t *c, lir_op_t *op) {
    assert(c != NULL);
    assert(op != NULL);
    assert(op->second != NULL);

    linked_t *result = linked_new();
    slice_t *args = op->second->value; // exprs
    assert(args);

    int64_t args_count = args->count;
    assert(args_count >= 0);
    lir_operand_t *sp_operand = operand_new(LIR_OPERAND_REG, r_sp);

    lir_operand_t *call_result = op->output;
    type_t call_result_type = type_kind_new(TYPE_VOID);
    if (call_result) {
        call_result_type = lir_operand_type(call_result);
    }

    pos_item_t *args_pos = mallocz((args_count + 1) * sizeof(pos_item_t));
    assert(args_pos);
    type_t *args_type = mallocz(sizeof(type_t) * (args_count + 1));

    args_type[0] = call_result_type;
    for (int i = 0; i < args_count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        args_type[i + 1] = arg_type;
    }

    // args_pos[0] 存储返回值的存储类型
    if (args_type[0].kind == TYPE_VOID) {
        args_pos[0].main = -1;
        args_pos[0].attach = -1;
    } else {
        // 计算返回值数据寄存器类型
        riscv64_pcs_aux(1, args_type, args_pos);

        // RISC-V 返回值：整数在A0(A1)，浮点在FA0(FA1)
        assert(args_pos[0].main == 0 || args_pos[0].main == 1 || args_pos[0].main == 16); // v0 或者 x0 寄存器
        if (args_type[0].kind == TYPE_ARR) {
            assert(args_pos[0].main == 1);
        }
    }

    // 计算参数传递需要的栈空间, +1 表示跳过 result 处理， result 在上面已经计算处理完成
    int64_t stack_offset;
    if (call_result_type.kind == TYPE_STRUCT && type_sizeof(call_result_type) > 16) {
        // 返回值作为大型结构体占用 a0 寄存器, 即占用一个参数位置
        stack_offset = riscv64_pcs_aux(args_count + 1, args_type, args_pos);
    } else {
        stack_offset = riscv64_pcs_aux(args_count, args_type + 1, args_pos + 1);
    }

    // 栈空间16字节对齐, 并声明需要的栈空间
    stack_offset = align_up(stack_offset, 16);
    if (stack_offset > c->call_stack_max_offset) {
        c->call_stack_max_offset = stack_offset;
    }

    // 特殊大型结构体处理，进行参数替换, 并且 caller 负责申请栈空间
    for (int i = 0; i < args_count; ++i) {
        lir_operand_t *arg_operand = args->take[i];
        pos_item_t arg_pos = args_pos[i + 1]; // 跳过 result
        type_t arg_type = lir_operand_type(arg_operand);

        if (arg_pos.main & 1) {
            // 申请临时空间存储结构体数据, callee 则不需要进行相关申请
            lir_operand_t *stack_ptr_operand = lower_temp_var_operand(c, result, arg_type);
            linked_t *temps = lir_memory_mov(c->module, type_sizeof(arg_type), stack_ptr_operand, arg_operand);
            linked_concat(result, temps);
            args->take[i] = stack_ptr_operand; // 参数替换
        }
    }

    // 优先处理通过栈进行传递的参数, 除了普通参数通过栈处理时，存在一种特殊情况
    // 即 struct 触发了扁平化处理(struct && !(main & 1)) 时
    for (int64_t i = 0; i < args_count; i++) {
        lir_operand_t *arg_operand = args->take[i];
        type_t arg_type = lir_operand_type(arg_operand);
        pos_item_t arg_pos = args_pos[i + 1];
        uint16_t size = type_sizeof(arg_type);
        bool is_struct_flatten = arg_type.kind == TYPE_STRUCT && !(arg_pos.main & 1);

        type_kind main_kind = arg_pos.main_filed_offset & 0xFF;
        int64_t main_offset = arg_pos.main_filed_offset >> 8;

        type_kind attach_kind = arg_pos.attach_filed_offset & 0xFF;
        int64_t attach_offset = arg_pos.attach_filed_offset >> 8;

        if (arg_pos.main >= 32) {
            int64_t sp_offset = (arg_pos.main & ~1) - 32;
            if (is_struct_flatten) {
                // 通过 arg_operand 获取结构体中的数据作为 src
                lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(main_kind), arg_operand, main_offset);

                // 将 src 数据以值传递的方式传输到 sp_offset 中
                lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(main_kind), sp_operand, sp_offset);
                linked_push(result, lir_op_move(dst, src));
            } else {
                // 普通类型参数通过栈传递
                lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, sp_operand, sp_offset);
                linked_push(result, lir_op_move(dst, arg_operand));
            }
        }

        if (arg_pos.attach >= 32) {
            int64_t sp_offset = (arg_pos.attach & ~1) - 32;
            assert(is_struct_flatten);
            // 通过 arg_operand 获取结构体中的数据作为 src
            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(attach_kind), arg_operand, attach_offset);

            // 将 src 数据以值传递的方式传输到 sp_offset 中
            lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(attach_kind), sp_operand, sp_offset);
            linked_push(result, lir_op_move(dst, src));
        }
    }

    // 记录使用的寄存器列表,用于寄存器分配
    slice_t *use_regs = slice_new();

    // 处理寄存器参数(特殊结构体参数可能会使用两个寄存器)
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg_operand = args->take[i];
        type_t arg_type = lir_operand_type(arg_operand);
        uint16_t size = type_sizeof(arg_type);
        pos_item_t arg_pos = args_pos[i + 1];

        // extract filed kind and offset
        type_kind main_kind = arg_pos.main_filed_offset & 0xFF;
        int64_t main_offset = arg_pos.main_filed_offset >> 8;
        assert(main_offset == 0);

        type_kind attach_kind = arg_pos.attach_filed_offset & 0xFF;
        int64_t attach_offset = arg_pos.attach_filed_offset >> 8;

        // 1. src 有两种来源，一种是普通数据，一种是特殊结构体数据, 都是通过 arg_operand 获取, 特殊结构体需要通过 indirect 获取
        lir_operand_t *lo_src_operand = arg_operand;
        lir_operand_t *hi_src_operand = NULL;

        if (arg_pos.main < 16) { // lo 部分通过整型寄存器传递
            if (arg_type.kind == TYPE_STRUCT && !(arg_pos.main & 1)) {
                lo_src_operand = indirect_addr_operand(c->module, type_kind_new(main_kind), arg_operand, 0);
            }

            assert(main_kind > 0);
            int64_t reg_index = A0->index + (arg_pos.main >> 1);
            lir_operand_t *lo_dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
            linked_push(result, lir_op_move(lo_dst_operand, lo_src_operand));
            slice_push(use_regs, lo_dst_operand->value);
        } else if (arg_pos.main < 32) { // 超过 32 则通过栈进行传递
            if (arg_type.kind == TYPE_STRUCT && !(arg_pos.main & 1)) {
                lo_src_operand = indirect_addr_operand(c->module, type_kind_new(main_kind), arg_operand, 0);
            }
            assert(main_kind > 0);
            int64_t reg_index = FA0->index + ((arg_pos.main - 16) >> 1);
            lir_operand_t *lo_dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
            linked_push(result, lir_op_move(lo_dst_operand, lo_src_operand));
            slice_push(use_regs, lo_dst_operand->value);
        }

        // attach 部分处理
        if (arg_pos.attach >= 0) {
            if (arg_pos.attach < 16) {
                if (arg_type.kind == TYPE_STRUCT && !(arg_pos.attach & 1)) {
                    hi_src_operand = indirect_addr_operand(c->module, type_kind_new(attach_kind), arg_operand, attach_offset);
                }

                int64_t reg_index = A0->index + (arg_pos.attach >> 1);
                lir_operand_t *hi_dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
                linked_push(result, lir_op_move(hi_dst_operand, hi_src_operand));
                slice_push(use_regs, hi_dst_operand->value);
            } else if (arg_pos.attach < 32) { // attach 可能通过栈传递，所以需要明确判断
                assert(arg_pos.attach < 32 && arg_pos.attach >= 16);
                if (arg_type.kind == TYPE_STRUCT && !(arg_pos.attach & 1)) {
                    hi_src_operand = indirect_addr_operand(c->module, type_kind_new(attach_kind), arg_operand, attach_offset);
                }

                int64_t reg_index = FA0->index + ((arg_pos.attach - 16) >> 1);
                lir_operand_t *hi_dst_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
                linked_push(result, lir_op_move(hi_dst_operand, hi_src_operand));
                slice_push(use_regs, hi_dst_operand->value);
            }
        }
    }

    // 记录使用的寄存器
    op->second = operand_new(LIR_OPERAND_REGS, use_regs);
    set_operand_flag(op->second);

    // 处理返回值
    if (!call_result) {
        linked_push(result, op); // 无返回值直接调用
        return result;
    }

    // 大型结构体通过指针处理时 == 1
    if (args_pos[0].main == 1) {
        assertf(call_result->assert_type == LIR_OPERAND_VAR, "call result must a var");
        assert(is_stack_ref_big_type(call_result_type));

        lir_operand_t *result_reg_operand = operand_new(LIR_OPERAND_REG, A0);

        // linear call 的时候没有为 result 申请空间，此处进行空间申请, 为 call 申请了空间，并将空间地址放在 x8 寄存器, callee 会使用该寄存器
        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
        linked_push(result, lir_op_move(result_reg_operand, call_result));

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, NULL, op->line, op->column));
        return result;
    }

    // 结构体分配栈空间
    if (is_stack_ref_big_type(call_result_type)) {
        assert(call_result->assert_type == LIR_OPERAND_VAR);
        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
    }

    uint8_t call_result_size = type_sizeof(call_result_type);
    if (call_result_type.kind == TYPE_STRUCT && call_result_size <= 16) {
        slice_t *output_regs = slice_new();

        // 特殊结构体处理, 由于存在扁平化处理，所以需要根据 args_pos 进行寄存器处理，寄存器选择直接通过 pos 即可
        // extract filed kind and offset
        int64_t main_pos = args_pos[0].main;
        type_kind main_kind = args_pos[0].main_filed_offset & 0xFF;
        int64_t main_offset = args_pos[0].main_filed_offset >> 8;
        assert(main_offset == 0);

        int64_t attach_pos = args_pos[0].attach;
        type_kind attach_kind = args_pos[0].attach_filed_offset & 0xFF;
        int64_t attach_offset = args_pos[0].attach_filed_offset >> 8;

        lir_operand_t *lo_src = NULL;
        lir_operand_t *hi_src = NULL;
        if (main_pos < 16) {
            int64_t reg_index = A0->index + (main_pos >> 1);
            lo_src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
        } else {
            assert(main_pos < 32);
            int64_t reg_index = FA0->index + ((main_pos - 16) >> 1);
            lo_src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
        }
        slice_push(output_regs, lo_src->value);

        if (attach_pos >= 0) { // 存在 hi 寄存器
            if (attach_pos < 16) {
                int64_t reg_index = A0->index + (attach_pos >> 1);
                hi_src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
            } else {
                assert(attach_pos < 32);
                int64_t reg_index = FA0->index + ((attach_pos - 16) >> 1);
                hi_src = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
            }
            slice_push(output_regs, hi_src->value);
        }

        lir_operand_t *new_output = operand_new(LIR_OPERAND_REGS, output_regs);
        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, new_output, op->line, op->column));

        // 进行 mov
        lir_operand_t *lo_dst = indirect_addr_operand(c->module, type_kind_new(main_kind), call_result, 0);
        linked_push(result, lir_op_move(lo_dst, lo_src));

        if (hi_src) {
            lir_operand_t *hi_dst = indirect_addr_operand(c->module, type_kind_new(attach_kind), call_result, attach_offset);
            linked_push(result, lir_op_move(hi_dst, hi_src));
        }
    } else {
        // 基本类型寄存器返回
        lir_operand_t *src;

        // int or float
        if (args_pos[0].main < 16) {
            // int
            src = operand_new(LIR_OPERAND_REG, reg_select(A0->index, call_result_type.kind));
        } else {
            assert(args_pos[0].main < 32);
            src = operand_new(LIR_OPERAND_REG, reg_select(FA0->index, call_result_type.kind));
        }

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, src, op->line, op->column));
        linked_push(result, lir_op_move(call_result, src));
    }

    return result;
}

linked_t *riscv64_lower_fn_end(closure_t *c, lir_op_t *op) {
    assert(c != NULL);
    assert(op != NULL);

    linked_t *result = linked_new();
    lir_operand_t *return_operand = op->first;
    if (!return_operand) {
        linked_push(result, op);
        return result;
    }

    type_t return_type = lir_operand_type(return_operand);

    pos_item_t args_pos[1] = {0};
    type_t args_type[1] = {0};
    args_type[0] = return_type;
    riscv64_pcs_aux(1, args_type, args_pos);

    pos_item_t result_pos = args_pos[0];
    int64_t main_pos = result_pos.main;
    type_kind main_kind = result_pos.main_filed_offset & 0xFF;
    int64_t main_offset = result_pos.main_filed_offset >> 8;
    assert(main_offset == 0);

    int64_t attach_pos = result_pos.attach;
    type_kind attach_kind = result_pos.attach_filed_offset & 0xFF;
    int64_t attach_offset = result_pos.attach_filed_offset >> 8;

    uint8_t return_size = type_sizeof(return_type);
    if (return_type.kind == TYPE_STRUCT && return_size <= 16) { // 触发扁平化处理
        lir_operand_t *lo_dst = NULL;
        lir_operand_t *hi_dst = NULL;

        if (main_pos < 16) {
            int64_t reg_index = A0->index + (main_pos >> 1);
            lo_dst = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
        } else {
            assert(main_pos < 32);
            int64_t reg_index = FA0->index + ((main_pos - 16) >> 1);
            lo_dst = operand_new(LIR_OPERAND_REG, reg_select(reg_index, main_kind));
        }

        if (attach_pos > 0) {
            if (attach_pos < 16) {
                int64_t reg_index = A0->index + (attach_pos >> 1);
                hi_dst = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
            } else {
                assert(attach_pos < 32);
                int64_t reg_index = FA0->index + ((attach_pos - 16) >> 1);
                hi_dst = operand_new(LIR_OPERAND_REG, reg_select(reg_index, attach_kind));
            }
        }
        // 进行 mov
        lir_operand_t *lo_src = indirect_addr_operand(c->module, type_kind_new(main_kind), return_operand, 0);
        linked_push(result, lir_op_move(lo_dst, lo_src));

        if (hi_dst) {
            lir_operand_t *hi_src = indirect_addr_operand(c->module, type_kind_new(attach_kind), return_operand, attach_offset);
            linked_push(result, lir_op_move(hi_dst, hi_src));
        }

    } else {
        // 非结构体基础类型，直接通过 a0/f0 寄存器即可
        lir_operand_t *dst;
        if (is_float(return_type.kind)) {
            dst = operand_new(LIR_OPERAND_REG, reg_select(FA0->index, return_type.kind));
        } else {
            dst = operand_new(LIR_OPERAND_REG, reg_select(A0->index, return_type.kind));
        }
        linked_push(result, lir_op_move(dst, return_operand));
    }

    op->first = NULL;
    linked_push(result, op);
    return result;
}

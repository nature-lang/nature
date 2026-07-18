#include "amd64_abi.h"

#include "src/register/arch/amd64.h"

#define WINDOWS_AMD64_REGISTER_SLOTS 4
#define WINDOWS_AMD64_SHADOW_SPACE 32

static bool amd64_windows_abi(void) {
    return BUILD_OS == OS_WINDOWS;
}

static type_kind amd64_windows_uint_kind(uint64_t size) {
    switch (size) {
        case BYTE:
            return TYPE_UINT8;
        case WORD:
            return TYPE_UINT16;
        case DWORD:
            return TYPE_UINT32;
        case QWORD:
            return TYPE_UINT64;
        default:
            assertf(false, "windows x64 cannot pass a %lu-byte aggregate in a register", size);
            return TYPE_UINT64;
    }
}

static reg_t *amd64_windows_int_arg_reg(uint8_t slot, type_kind kind) {
    static const uint8_t indices[WINDOWS_AMD64_REGISTER_SLOTS] = {1, 2, 8, 9}; // rcx, rdx, r8, r9
    assert(slot < WINDOWS_AMD64_REGISTER_SLOTS);
    return reg_select(indices[slot], kind);
}

static reg_t *amd64_windows_sse_arg_reg(uint8_t slot, type_kind kind) {
    static const uint8_t indices[WINDOWS_AMD64_REGISTER_SLOTS] = {0, 1, 2, 3}; // xmm0 .. xmm3
    assert(slot < WINDOWS_AMD64_REGISTER_SLOTS);
    return reg_select(indices[slot], kind);
}

/**
 * Microsoft x64 uses four ordinal argument slots.  Integer and floating-point
 * arguments do not have independent register cursors: slot N selects either
 * RCX/RDX/R8/R9 or XMM0..XMM3.  Aggregates of exactly 1, 2, 4, or 8 bytes are
 * represented as an integer of the same width; every other aggregate is passed
 * by reference to caller-owned storage.
 *
 * Nature does not expose native SIMD vector types yet.  When those are added,
 * __m128/HVA classification must be handled before the generic aggregate rule.
 */
static int64_t amd64_windows_type_classify(type_t t, amd64_class_t *lo, amd64_class_t *hi) {
    *lo = AMD64_CLASS_NO;
    *hi = AMD64_CLASS_NO;

    if (t.kind == TYPE_ARR) {
        return 0;
    }

    if (is_abi_struct_like(t)) {
        if (t.storage_size == BYTE || t.storage_size == WORD || t.storage_size == DWORD ||
            t.storage_size == QWORD) {
            *lo = AMD64_CLASS_INTEGER;
            return 1;
        }
        return 0;
    }

    *lo = is_float(t.map_imm_kind) ? AMD64_CLASS_SSE : AMD64_CLASS_INTEGER;
    return 1;
}

int64_t amd64_abi_type_classify(type_t t, amd64_class_t *lo, amd64_class_t *hi,
                                uint64_t offset) {
    if (amd64_windows_abi()) {
        return amd64_windows_type_classify(t, lo, hi);
    }

    return amd64_type_classify(t, lo, hi, offset);
}

linked_t *amd64_lower_return(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();

    lir_operand_t *return_operand = op->first;
    if (!return_operand) {
        linked_push(result, op);
        return result;
    }

    type_t return_type = c->fndef->return_type;
    uint64_t return_size = return_type.storage_size;

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_abi_type_classify(return_type, &lo, &hi, 0);

    if (count == 0) {
        // 通过寄存器进行传递，此时 c->return_operand 中存储了目标空间(可能是 caller 申请的内存空间，在 fn_begin 中将该空间的指针传递给了 c->return_operand)
        assert(c->return_big_operand);
        linked_t *temps = lir_memory_mov(c->module, return_size, c->return_big_operand, return_operand);
        linked_concat(result, temps);

        // 将 return big_operand 中的值存储在 rax 中
        lir_operand_t *rax_operand = operand_new(LIR_OPERAND_REG, rax);
        linked_push(result, lir_op_move(rax_operand, c->return_big_operand));

        goto END;
    }

    lir_operand_t *lo_dst_reg = NULL;
    type_kind lo_kind;
    if (lo == AMD64_CLASS_INTEGER) {
        lo_kind = amd64_windows_abi() && is_abi_struct_like(return_type)
                          ? amd64_windows_uint_kind(return_size)
                          : TYPE_UINT64;
        lo_dst_reg = operand_new(LIR_OPERAND_REG, reg_select(rax->index, lo_kind));
    } else {
        lo_dst_reg = operand_new(LIR_OPERAND_REG, xmm0s64);
        lo_kind = TYPE_FLOAT64;
    }

    if (is_abi_struct_like(return_type)) {
        // return_operand 保存的是一个指针，需要 indirect 这个值，将响应的值 mov 到寄存器中
        lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(lo_kind), return_operand, 0);
        linked_push(result, lir_op_move(lo_dst_reg, src));

        if (count == 2) {
            lir_operand_t *hi_dst_reg = NULL;
            type_kind hi_kind;
            if (hi == AMD64_CLASS_INTEGER) {
                hi_dst_reg = operand_new(LIR_OPERAND_REG, rdx);
                hi_kind = TYPE_UINT64;
            } else {
                hi_dst_reg = operand_new(LIR_OPERAND_REG, xmm1s64);
                hi_kind = TYPE_FLOAT64;
            }
            src = indirect_addr_operand(c->module, type_kind_new(hi_kind), return_operand, QWORD);
            linked_push(result, lir_op_move(hi_dst_reg, src));
        }
    } else {
        assert(count == 1);
        assertf(return_type.kind != TYPE_ARR, "array type must be pointer type");

        // 由于需要直接 mov， 所以还是需要选择合适的大小
        reg_t *lo_reg = lo_dst_reg->value;
        lo_dst_reg = operand_new(LIR_OPERAND_REG, reg_select(lo_reg->index, return_type.map_imm_kind));

        linked_push(result, lir_op_move(lo_dst_reg, return_operand));
    }

END:
    op->first = NULL;
    linked_push(result, op);

    return result;
}

/**
 * 参照 linux abi 规范 将参数从寄存器或者栈中取出来，存放到 formal vars 中
 * @param c
 * @param formals
 * @return
 */
static linked_t *amd64_windows_lower_params(closure_t *c, slice_t *param_vars) {
    linked_t *result = linked_new();

    // After CALL and PUSH RBP, [rbp+8] is the return address, [rbp+16]
    // starts the caller-provided home area, and the fifth argument is at
    // [rbp+48].  The first four home slots are not incoming stack arguments.
    int64_t stack_param_slot = 16 + WINDOWS_AMD64_SHADOW_SPACE;

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;
        lir_operand_t *dst_param = operand_new(LIR_OPERAND_VAR, var);

        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_windows_type_classify(param_type, &lo, &hi);
        bool in_register = i < WINDOWS_AMD64_REGISTER_SLOTS;

        if (count == 0) {
            // Large aggregates and arrays arrive as a pointer to the caller's
            // by-value copy.  Keeping that pointer in the formal matches
            // Nature's STORAGE_KIND_IND representation.
            lir_operand_t *src;
            if (in_register) {
                src = operand_new(LIR_OPERAND_REG,
                                  amd64_windows_int_arg_reg((uint8_t) i, TYPE_ANYPTR));
            } else {
                src = lir_stack_operand(c->module, stack_param_slot, QWORD, TYPE_ANYPTR);
                stack_param_slot += QWORD;
            }
            linked_push(result, lir_op_move(dst_param, src));
            continue;
        }

        if (is_abi_struct_like(param_type)) {
            // A 1/2/4/8-byte aggregate is carried in a GPR (or an 8-byte
            // stack slot) but remains addressable in Nature, so materialize
            // it into local storage on entry.
            type_kind word_kind = amd64_windows_uint_kind(param_type.storage_size);
            linked_push(result, lir_stack_alloc(c, param_type, dst_param));

            lir_operand_t *src;
            if (in_register) {
                src = operand_new(LIR_OPERAND_REG,
                                  amd64_windows_int_arg_reg((uint8_t) i, word_kind));
            } else {
                src = lir_stack_operand(c->module, stack_param_slot, param_type.storage_size, word_kind);
                stack_param_slot += QWORD;
            }

            lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(word_kind), dst_param, 0);
            linked_push(result, lir_op_move(dst, src));
            continue;
        }

        assert(count == 1);
        assertf(param_type.kind != TYPE_ARR, "array type must be pointer type");

        lir_operand_t *src;
        if (in_register) {
            reg_t *reg = lo == AMD64_CLASS_SSE
                                 ? amd64_windows_sse_arg_reg((uint8_t) i, param_type.map_imm_kind)
                                 : amd64_windows_int_arg_reg((uint8_t) i, param_type.map_imm_kind);
            src = operand_new(LIR_OPERAND_REG, reg);
        } else {
            src = lir_stack_operand(c->module, stack_param_slot, param_type.storage_size,
                                    param_type.map_imm_kind);
            stack_param_slot += QWORD;
        }
        linked_push(result, lir_op_move(dst_param, src));
    }

    return result;
}

static linked_t *amd64_lower_params(closure_t *c, slice_t *param_vars) {
    if (amd64_windows_abi()) {
        return amd64_windows_lower_params(c, param_vars);
    }

    linked_t *result = linked_new();

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    uint8_t *onstack = mallocz(param_vars->count + 1);

    int64_t stack_param_slot = 16; // by rbp, is_rest addr 和 push rsp 占用了 16 个字节

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(param_type, &lo, &hi, 0);
        if (count == 0) {
            onstack[i] = 1;
            continue;
        } else if (count == 1) {
            if (lo == AMD64_CLASS_SSE && sse_reg_count + 1 <= 8) {
                onstack[i] = 0;
                sse_reg_count++;
            } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 1 <= 6) {
                onstack[i] = 0;
                int_reg_count++;
            } else {
                onstack[i] = 1;
                continue;
            }
        } else if (count == 2) {
            if (lo == hi) {
                if (lo == AMD64_CLASS_SSE && sse_reg_count + 2 <= 8) {
                    onstack[i] = 0;
                    sse_reg_count += 2;
                } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 2 <= 6) {
                    onstack[i] = 0;
                    int_reg_count += 2;
                } else {
                    onstack[i] = 1;
                }
            } else {
                if (sse_reg_count + 1 <= 8 && int_reg_count + 1 <= 6) {
                    onstack[i] = 0;
                    sse_reg_count++;
                    int_reg_count++;
                } else {
                    onstack[i] = 1;
                }
            }
        }
    }

    uint8_t int_reg_indices[6] = {7, 6, 2, 1, 8, 9};
    uint8_t sse_reg_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t int_reg_index = 0;
    uint8_t sse_reg_index = 0;

    // - stack 临时占用的 rsp 空间，在函数调用完成后需要归还回去
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越考右的参数越优先入栈)
    for (int i = 0; i < param_vars->count; ++i) {
        lir_var_t *var = param_vars->take[i];
        type_t param_type = var->type;

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(param_type, &lo, &hi, 0);
        lir_operand_t *dst_param = operand_new(LIR_OPERAND_VAR, var);

        if (onstack[i]) {
            // 最右侧参数先入栈，所以最右侧参数在栈顶，合理的方式是从左往右遍历读取 source 了。
            // 结构体由于 caller 已经申请了 stack 空间，所以参数中直接使用该空间的地址即可
            // 而不需要进行重复的 copy 和 stack 空间申请, 这也导致 gc_malloc 功能无法正常使用。
            lir_operand_t *src = lir_stack_operand(c->module, stack_param_slot, param_type.storage_size,
                                                   param_type.kind);

            // stack 会被 native 成 indirect_addr [rbp+slot], 如果其中保存的是结构体，这里需要的是地址。
            if (param_type.storage_kind == STORAGE_KIND_IND) {
                lir_operand_t *src_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
                linked_push(result, lir_op_lea(src_ref, src));
                linked_push(result, lir_op_move(dst_param, src_ref));
            } else {
                linked_push(result, lir_op_move(dst_param, src));
            }

            stack_param_slot += align_up(param_type.storage_size, QWORD); // 参数按照 8byte 对齐
        } else {
            if (is_abi_struct_like(param_type)) {
                linked_push(result, lir_stack_alloc(c, param_type, dst_param));

                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                type_kind lo_kind;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_UINT64));
                    lo_kind = TYPE_UINT64;
                } else if (lo == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_FLOAT64));
                    lo_kind = TYPE_FLOAT64;
                } else {
                    assert(false);
                }

                lir_operand_t *lo_dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), dst_param, 0);

                linked_push(result, lir_op_move(lo_dst, lo_reg_operand));

                if (count == 2) {
                    // 从寄存器中将数据移入到低保空间
                    lir_operand_t *hi_reg_operand;
                    type_kind hi_kind;
                    if (hi == AMD64_CLASS_INTEGER) {
                        // 查找合适大小的 reg 进行 mov
                        uint8_t reg_index = int_reg_indices[int_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_UINT64));
                        hi_kind = TYPE_UINT64;
                    } else if (hi == AMD64_CLASS_SSE) {
                        uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                        hi_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, TYPE_FLOAT64));
                        hi_kind = TYPE_FLOAT64;
                    } else {
                        assert(false);
                    }

                    lir_operand_t *hi_dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), dst_param, QWORD);

                    linked_push(result, lir_op_move(hi_dst, hi_reg_operand));
                }
            } else {
                assertf(param_type.kind != TYPE_ARR, "array type must be pointer type");
                assertf(count == 1, "the normal type uses only one register");
                // 从寄存器中将数据移入到低保空间
                lir_operand_t *lo_reg_operand;
                uint8_t reg_index;
                if (lo == AMD64_CLASS_INTEGER) {
                    // 查找合适大小的 reg 进行 mov
                    reg_index = int_reg_indices[int_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.map_imm_kind));
                } else if (lo == AMD64_CLASS_SSE) {
                    reg_index = sse_reg_indices[sse_reg_index++];
                    lo_reg_operand = operand_new(LIR_OPERAND_REG, reg_select(reg_index, param_type.map_imm_kind));
                } else {
                    assert(false);
                }

                linked_push(result, lir_op_move(dst_param, lo_reg_operand));
            }
        }
    }

    return result;
}

linked_t *amd64_lower_fn_begin(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    linked_push(result, op); // 将 FN_BEGIN 放在最前端，后续 native 还需要使用

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    slice_t *params = op->output->value;

    type_t return_type = c->fndef->return_type;

    if (return_type.kind != TYPE_VOID) {
        // 为了 ssa 将 return var 添加到了 params 中，现在 从 params 中移除, 使用 nop 或者 其他方式保证 def 完整
        int64_t count = amd64_abi_type_classify(return_type, &lo, &hi, 0);

        if (count == 0) { // count == 0 则无法通过寄存器传递返回值
            assert(return_type.storage_kind == STORAGE_KIND_IND);

            // return operand 总是通过寄存器 rax 或特殊寄存器返回。
            c->return_big_operand = temp_var_operand(c->module, type_kind_new(TYPE_ANYPTR));

            lir_operand_t *return_operand = c->return_big_operand;

            // 占用 rdi 寄存器, 在 lower_params 中会进行 move rdi -> return_operand_var
            slice_insert(params, 0, return_operand->value);
        } else {
            assertf(return_type.kind != TYPE_ARR, "array type must be pointer type");
        }
    }

    linked_concat(result, amd64_lower_params(c, params));
    op->output->value = slice_new();
    return result;
}

/**
 * call test(arg1, aeg2, ...) -> result
 * 使用到的寄存器需要收集起来，放到 call->second 保持 reg 的声明返回健康
 * @param c
 * @param op
 * @return
 */
static linked_t *amd64_windows_lower_args(closure_t *c, lir_op_t *op) {
    slice_t *args = op->second->value;
    linked_t *result = linked_new();
    slice_t *use_regs = slice_new();
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    lir_operand_t **passed_args = mallocz(sizeof(lir_operand_t *) * (args->count + 1));
    uint64_t *copy_offsets = mallocz(sizeof(uint64_t) * (args->count + 1));

    uint64_t stack_arg_count = args->count > WINDOWS_AMD64_REGISTER_SLOTS
                                       ? (uint64_t) args->count - WINDOWS_AMD64_REGISTER_SLOTS
                                       : 0;
    uint64_t copy_cursor = WINDOWS_AMD64_SHADOW_SPACE + stack_arg_count * QWORD;

    // Reserve caller-owned copies after the home area and stack arguments.
    // Indirect aggregate arguments are 16-byte aligned so this layout is also
    // suitable for a future native SIMD aggregate implementation.
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_windows_type_classify(arg_type, &lo, &hi);
        if (count != 0) {
            continue;
        }

        copy_cursor = align_up(copy_cursor, 16);
        copy_offsets[i] = copy_cursor;
        copy_cursor += align_up(max(arg_type.storage_size, (uint64_t) BYTE), 16);
    }

    uint64_t outgoing_size = align_up(copy_cursor, AMD64_STACK_ALIGN_SIZE);
    if (outgoing_size < WINDOWS_AMD64_SHADOW_SPACE) {
        outgoing_size = WINDOWS_AMD64_SHADOW_SPACE;
    }
    if (outgoing_size > (uint64_t) c->call_stack_max_offset) {
        c->call_stack_max_offset = (int64_t) outgoing_size;
    }

    // Materialize by-value aggregate copies before assigning any fixed
    // argument register.  This prevents the copy loop from clobbering an
    // already prepared RCX/RDX/R8/R9 or XMM0..XMM3 value.
    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_windows_type_classify(arg_type, &lo, &hi);

        if (count != 0) {
            passed_args[i] = arg;
            continue;
        }

        assertf(arg_type.storage_kind == STORAGE_KIND_IND,
                "windows x64 indirect ABI argument must use indirect storage");
        lir_operand_t *copy = indirect_addr_operand(c->module, arg_type, rsp_operand, copy_offsets[i]);
        lir_operand_t *copy_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
        linked_push(result, lir_op_lea(copy_ref, copy));
        linked_concat(result, lir_memory_mov(c->module, arg_type.storage_size, copy_ref, arg));
        passed_args[i] = copy_ref;
    }

    // Stack arguments occupy one eight-byte slot each and begin immediately
    // after the mandatory 32-byte home area.
    for (int i = WINDOWS_AMD64_REGISTER_SLOTS; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_windows_type_classify(arg_type, &lo, &hi);
        uint64_t rsp_offset = WINDOWS_AMD64_SHADOW_SPACE +
                              ((uint64_t) i - WINDOWS_AMD64_REGISTER_SLOTS) * QWORD;

        if (count == 0) {
            lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(TYPE_ANYPTR), rsp_operand,
                                                       rsp_offset);
            linked_push(result, lir_op_move(dst, passed_args[i]));
            continue;
        }

        if (is_abi_struct_like(arg_type)) {
            type_kind word_kind = amd64_windows_uint_kind(arg_type.storage_size);
            lir_operand_t *word = lower_temp_var_operand(c, result, type_kind_new(word_kind));
            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(word_kind), arg, 0);
            lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(word_kind), rsp_operand,
                                                       rsp_offset);
            linked_push(result, lir_op_move(word, src));
            linked_push(result, lir_op_move(dst, word));
            continue;
        }

        lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, rsp_operand, rsp_offset);
        linked_push(result, lir_op_move(dst, arg));
    }

    // The first four argument positions share an ordinal slot. A floating
    // argument in slot 2 uses XMM2; it does not consume an independent SSE
    // cursor. Mirror every register floating argument into the matching GPR as
    // required for unprototyped/C-vararg calls. The duplicate is harmless for
    // prototyped calls and lets the current LIR represent both cases without
    // losing call-signature metadata before ABI lowering.
    for (int i = 0; i < args->count && i < WINDOWS_AMD64_REGISTER_SLOTS; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_windows_type_classify(arg_type, &lo, &hi);

        lir_operand_t *temp;
        if (count == 0) {
            reg_t *hint = amd64_windows_int_arg_reg((uint8_t) i, TYPE_ANYPTR);
            temp = lower_temp_var_with_hint(c, result, type_kind_new(TYPE_ANYPTR), hint);
            linked_push(result, lir_op_call_arg_move(temp, passed_args[i]));
        } else if (is_abi_struct_like(arg_type)) {
            type_kind word_kind = amd64_windows_uint_kind(arg_type.storage_size);
            reg_t *hint = amd64_windows_int_arg_reg((uint8_t) i, word_kind);
            temp = lower_temp_var_with_hint(c, result, type_kind_new(word_kind), hint);
            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(word_kind), arg, 0);
            linked_push(result, lir_op_call_arg_move(temp, src));
        } else {
            reg_t *hint = lo == AMD64_CLASS_SSE
                                  ? amd64_windows_sse_arg_reg((uint8_t) i, arg_type.map_imm_kind)
                                  : amd64_windows_int_arg_reg((uint8_t) i, arg_type.map_imm_kind);
            temp = lower_temp_var_with_hint(c, result, arg_type, hint);
            linked_push(result, lir_op_call_arg_move(temp, arg));

            if (lo == AMD64_CLASS_SSE) {
                type_kind mirror_kind = arg_type.storage_size == DWORD ? TYPE_UINT32 : TYPE_UINT64;
                lir_operand_t *home = indirect_addr_operand(c->module, arg_type, rsp_operand,
                                                            (uint64_t) i * QWORD);
                linked_push(result, lir_op_move(home, arg));

                lir_operand_t *mirror_src = indirect_addr_operand(c->module, type_kind_new(mirror_kind),
                                                                  rsp_operand, (uint64_t) i * QWORD);
                reg_t *mirror_hint = amd64_windows_int_arg_reg((uint8_t) i, mirror_kind);
                lir_operand_t *mirror = lower_temp_var_with_hint(c, result, type_kind_new(mirror_kind),
                                                                 mirror_hint);
                linked_push(result, lir_op_call_arg_move(mirror, mirror_src));
                slice_push(use_regs, mirror->value);
            }
        }
        slice_push(use_regs, temp->value);
    }

    op->second = lir_reset_operand(operand_new(LIR_OPERAND_VARS, use_regs), LIR_FLAG_SECOND);
    return result;
}

static linked_t *amd64_lower_args(closure_t *c, lir_op_t *op) {
    if (amd64_windows_abi()) {
        return amd64_windows_lower_args(c, op);
    }

    slice_t *args = op->second->value; // exprs

    // 进行 op 替换(重新set flag 即可)
    slice_t *use_regs = slice_new(); // reg_t*

    linked_t *result = linked_new();

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    uint8_t *onstack = mallocz(args->count + 1);
    lir_operand_t *sp_operand = operand_new(LIR_OPERAND_REG, rsp);

    uint8_t sse_reg_count = 0;
    uint8_t int_reg_count = 0;

    int64_t stack_offset = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (count == 0) {
            // 返回 0 就是需要内存传递, 此时才需要几率栈内存！？
            stack_offset += align_up(arg_type.storage_size, QWORD); // 参数按照 8byte 对齐
            onstack[i] = 1;
            continue;
        } else if (count == 1) {
            if (lo == AMD64_CLASS_SSE && sse_reg_count + 1 <= 8) {
                onstack[i] = 0;
                sse_reg_count++;
            } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 1 <= 6) {
                onstack[i] = 0;
                int_reg_count++;
            } else {
                stack_offset += align_up(arg_type.storage_size, QWORD); // 参数按照 8byte 对齐
                onstack[i] = 1;
                continue;
            }
        } else if (count == 2) {
            if (lo == hi) {
                if (lo == AMD64_CLASS_SSE && sse_reg_count + 2 <= 8) {
                    onstack[i] = 0;
                    sse_reg_count += 2;
                } else if (lo == AMD64_CLASS_INTEGER && int_reg_count + 2 <= 6) {
                    onstack[i] = 0;
                    int_reg_count += 2;
                } else {
                    onstack[i] = 1;
                    stack_offset += align_up(arg_type.storage_size, QWORD);
                }
            } else {
                if (sse_reg_count + 1 <= 8 && int_reg_count + 1 <= 6) {
                    onstack[i] = 0;
                    sse_reg_count++;
                    int_reg_count++;
                } else {
                    onstack[i] = 1;
                    stack_offset += align_up(arg_type.storage_size, QWORD);
                }
            }
        }
    }

    // native 时会进行 16byte 对齐
    if (stack_offset > c->call_stack_max_offset) {
        c->call_stack_max_offset = stack_offset;
    }

    // 最右侧参数最先入栈， sub 相当于入栈标识
    // 如果从最左侧的第一个需要入栈的参数开始处理，则其对应的就是 rsp+0
    // 优先处理需要通过 stack 传递的参数,将他们都 push 或者 mov 到栈里面(越靠右的参数越优先入栈), 使用 mov 的话就不需要担心先后问题
    int64_t rsp_offset = 0;
    for (int i = 0; i < args->count; i++) {
        lir_operand_t *arg_operand = args->take[i];

        if (onstack[i] == 0) {
            continue;
        }

        type_t arg_type = lir_operand_type(arg_operand);

        lo = hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);

        lir_operand_t *dst = indirect_addr_operand(c->module, arg_type, sp_operand, rsp_offset);

        if (arg_type.storage_kind == STORAGE_KIND_IND) {
            assert(arg_operand->assert_type == LIR_OPERAND_VAR);
            // lea addr to temp
            lir_operand_t *dst_ref = lower_temp_var_operand(c, result, type_kind_new(TYPE_ANYPTR));
            linked_push(result, lir_op_lea(dst_ref, dst));

            linked_t *temps = lir_memory_mov(c->module, arg_type.storage_size, dst_ref, arg_operand);
            linked_concat(result, temps);
        } else {
            linked_push(result, lir_op_move(dst, arg_operand));
        }

        // 已经在栈上为大结构体预留预留了足够的空间
        uint64_t align_size = align_up(arg_type.storage_size, POINTER_SIZE);
        rsp_offset += align_size;
    }

    // - 处理通过寄存器传递的参数(选择合适的寄存器。多余的部分进行 xor 清空)
    uint8_t int_reg_indices[6] = {7, 6, 2, 1, 8, 9};
    uint8_t sse_reg_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t int_reg_index = 0;
    uint8_t sse_reg_index = 0;

    for (int i = 0; i < args->count; ++i) {
        lir_operand_t *arg = args->take[i];
        type_t arg_type = lir_operand_type(arg);

        lo = AMD64_CLASS_NO;
        hi = AMD64_CLASS_NO;
        int64_t count = amd64_type_classify(arg_type, &lo, &hi, 0);
        if (onstack[i] == 1) {
            continue;
        }

        assert(count != 0);

        if (is_abi_struct_like(arg_type)) {
            lir_operand_t *lo_temp_var;
            type_kind lo_kind;
            if (lo == AMD64_CLASS_INTEGER) {
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, TYPE_UINT64);
                lo_kind = TYPE_UINT64;
                lo_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(lo_kind), hint_reg);
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, TYPE_FLOAT64);
                lo_kind = TYPE_FLOAT64;
                lo_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(lo_kind), hint_reg);
            } else {
                assert(false);
                exit(EXIT_FAILURE);
            }

            lir_operand_t *src_operand = indirect_addr_operand(c->module, type_kind_new(lo_kind), arg, 0);
            linked_push(result, lir_op_call_arg_move(lo_temp_var, src_operand));
            slice_push(use_regs, lo_temp_var->value);

            if (count == 2) {
                lir_operand_t *hi_temp_var;
                type_kind hi_kind;
                if (hi == AMD64_CLASS_INTEGER) {
                    uint8_t reg_index = int_reg_indices[int_reg_index++];
                    reg_t *hint_reg = reg_select(reg_index, TYPE_UINT64);
                    hi_kind = TYPE_UINT64;
                    hi_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(hi_kind), hint_reg);
                } else if (hi == AMD64_CLASS_SSE) {
                    uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                    reg_t *hint_reg = reg_select(reg_index, TYPE_FLOAT64);
                    hi_kind = TYPE_FLOAT64;
                    hi_temp_var = lower_temp_var_with_hint(c, result, type_kind_new(hi_kind), hint_reg);
                } else {
                    assert(false);
                    exit(EXIT_FAILURE);
                }

                src_operand = indirect_addr_operand(c->module, type_kind_new(hi_kind), arg, QWORD);
                linked_push(result, lir_op_call_arg_move(hi_temp_var, src_operand));
                slice_push(use_regs, hi_temp_var->value);
            }
        } else {
            assertf(count == 1, "the normal type uses only one register");
            lir_operand_t *lo_temp_var;
            if (lo == AMD64_CLASS_INTEGER) {
                uint8_t reg_index = int_reg_indices[int_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, arg_type.map_imm_kind);
                lo_temp_var = lower_temp_var_with_hint(c, result, arg_type, hint_reg);
            } else if (lo == AMD64_CLASS_SSE) {
                uint8_t reg_index = sse_reg_indices[sse_reg_index++];
                reg_t *hint_reg = reg_select(reg_index, arg_type.map_imm_kind);
                lo_temp_var = lower_temp_var_with_hint(c, result, arg_type, hint_reg);
            } else {
                assert(false);
            }
            linked_push(result, lir_op_call_arg_move(lo_temp_var, arg));
            slice_push(use_regs, lo_temp_var->value);
        }
    }

    op->second = lir_reset_operand(operand_new(LIR_OPERAND_VARS, use_regs), LIR_FLAG_SECOND);

    return result;
}

linked_t *amd64_lower_call(closure_t *c, lir_op_t *op) {
    linked_t *result = linked_new();
    lir_operand_t *call_result = op->output;
    lir_operand_t *rsp_operand = operand_new(LIR_OPERAND_REG, rsp);

    if (!call_result) {
        linked_concat(result, amd64_lower_args(c, op));

        // 处理 call first operand (函数指针) 的 must_hint
        // 当 first 是 VAR 时，需要为其分配一个不参与 args_abi 的固定寄存器，避免与参数冲突
        if (op->first && op->first->assert_type == LIR_OPERAND_VAR) {
            lir_var_t *first_var = op->first->value;
            type_t first_type = first_var->type;
            lir_operand_t *first_temp_var = lower_temp_var_with_hint(c, result, first_type, r11);
            linked_push(result, lir_op_call_arg_move(first_temp_var, op->first));
            op->first = lir_reset_operand(first_temp_var, LIR_FLAG_FIRST);
        }

        linked_push(result, op); // call op

        return result;
    }

    type_t call_result_type = lir_operand_type(call_result);

    // amd64 中，大型返回值由 caller 申请空间，并将空间指针通过 rdi 寄存器传递给 calle
    if (call_result_type.storage_kind == STORAGE_KIND_IND) {
        assert(call_result->assert_type == LIR_OPERAND_VAR);

        linked_push(result, lir_stack_alloc(c, call_result_type, call_result));
    }

    amd64_class_t lo = AMD64_CLASS_NO;
    amd64_class_t hi = AMD64_CLASS_NO;
    int64_t count = amd64_abi_type_classify(call_result_type, &lo, &hi, 0);

    // call result to args
    // count == 0 标识参数通过栈进行传递，但是需要注意的是此时传递并的是一个指针，而不是整个 struct
    // 如果把整个 struct 丢进去会造成识别异常
    if (count == 0) {
        assertf(call_result->assert_type == LIR_OPERAND_VAR, "call result must a var");
        assert(lir_operand_type(call_result).storage_kind == STORAGE_KIND_IND);

        lir_operand_t *temp_arg = lir_operand_copy(call_result);
        lir_var_t *temp_var = temp_arg->value;
        temp_var->type = type_refof(temp_var->type);
        slice_insert(op->second->value, 0, temp_arg); // 将 call result 也填入到 args 中进行统一处理
    }

    // -------------------- lower args ------------------------
    linked_concat(result, amd64_lower_args(c, op));

    // 处理 call first operand (函数指针) 的 must_hint
    // 当 first 是 VAR 时，需要为其分配一个不参与 args_abi 的固定寄存器，避免与参数冲突
    if (op->first && op->first->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *first_var = op->first->value;
        type_t first_type = first_var->type;
        lir_operand_t *first_temp_var = lower_temp_var_with_hint(c, result, first_type, r11);
        linked_push(result, lir_op_call_arg_move(first_temp_var, op->first));
        op->first = lir_reset_operand(first_temp_var, LIR_FLAG_FIRST);
    }

    // 参数通过栈传递, 返回值处理
    if (count == 0) {
        lir_operand_t *output_reg = operand_new(LIR_OPERAND_REG, reg_select(rax->index, TYPE_UINT64));
        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, output_reg, op->line, op->column));
        linked_push(result, lir_op_move(call_result, output_reg));
        return result;
    }

    // -- 返回值通过 reg 传递

    // call 指令 存在返回值， 现在需要处理相关的返回值,首先返回值必须通过 rax/rdi 返回
    lir_operand_t *lo_src_reg;
    type_kind lo_kind;
    if (lo == AMD64_CLASS_INTEGER) {
        lo_kind = amd64_windows_abi() && is_abi_struct_like(call_result_type)
                          ? amd64_windows_uint_kind(call_result_type.storage_size)
                          : TYPE_UINT64;
        lo_src_reg = operand_new(LIR_OPERAND_REG, reg_select(rax->index, lo_kind));
    } else {
        lo_src_reg = operand_new(LIR_OPERAND_REG, xmm0s64);
        lo_kind = TYPE_FLOAT64;
    }

    // ~~ call_result 需要做替换，为了后续 interval 阶段能够正确收集 var def, 所以手动添加一下 def ~~ 上面的 lea 做了
    // linked_push(result, lir_op_nop(call_result));

    if (is_abi_struct_like(call_result_type)) {
        // 无论如何，先生成 call 指令
        slice_t *output_regs = slice_new();
        slice_push(output_regs, lo_src_reg->value); // 不能延后处理，会导致 flag 设置异常！
        lir_operand_t *new_output = lir_reset_operand(operand_new(LIR_OPERAND_REGS, output_regs), LIR_FLAG_OUTPUT);

        lir_operand_t *hi_src_reg;
        type_kind hi_kind;
        if (count == 2) {
            if (hi == AMD64_CLASS_INTEGER) {
                hi_src_reg = operand_new(LIR_OPERAND_REG, rdx);
                hi_kind = TYPE_UINT64;
            } else {
                hi_src_reg = operand_new(LIR_OPERAND_REG, xmm1s64);
                hi_kind = TYPE_FLOAT64;
            }
            slice_push(output_regs, hi_src_reg->value);
        }

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, new_output, op->line, op->column));

        // 从 reg 中将返回值 mov 到 call_result 上
        lir_operand_t *dst = indirect_addr_operand(c->module, type_kind_new(lo_kind), call_result, 0);
        linked_push(result, lir_op_move(dst, lo_src_reg));
        if (count == 2) {
            dst = indirect_addr_operand(c->module, type_kind_new(hi_kind), call_result, QWORD);
            linked_push(result, lir_op_move(dst, hi_src_reg));
        }
    } else {
        assert(count == 1);
        reg_t *lo_reg = lo_src_reg->value;
        lo_src_reg = operand_new(LIR_OPERAND_REG, reg_select(lo_reg->index, call_result_type.map_imm_kind));

        linked_push(result, lir_op_with_pos(LIR_OPCODE_CALL, op->first, op->second, lo_src_reg, op->line, op->column));

        linked_push(result, lir_op_move(call_result, lo_src_reg));
    }

    return result;
}

static amd64_class_t amd64_classify_merge(amd64_class_t a, amd64_class_t b) {
    if (a == b) {
        return a;
    }

    if (a == AMD64_CLASS_NO) {
        return b;
    }

    if (b == AMD64_CLASS_NO) {
        return a;
    }

    if (a == AMD64_CLASS_MEMORY || b == AMD64_CLASS_MEMORY) {
        return AMD64_CLASS_MEMORY;
    }

    if (a == AMD64_CLASS_INTEGER || b == AMD64_CLASS_INTEGER) {
        return AMD64_CLASS_INTEGER;
    }

    return AMD64_CLASS_SSE;
}

int64_t amd64_type_classify(type_t t, amd64_class_t *lo, amd64_class_t *hi, uint64_t offset) {
    uint64_t size = t.storage_size;

    if (size > 16) {
        return 0;
    }

    // 在 nature 实现中，栈总是通过栈传递并进行完整的值传递
    // 而 c 语言中，arr 本身是在 abi 中未定义的数据类型，所以 arr 需要作为 ptr 进行传递
    if (t.kind == TYPE_ARR) {
        return 0;
    }

    if (is_float(t.map_imm_kind)) {
        if (offset < 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_SSE);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_SSE);
        }
    } else if (is_abi_struct_like(t)) {
        // 遍历 struct-like 类型的元素进行分类
        list_t *elements = t.abi_struct;
        assert(elements != NULL);
        offset = align_up(offset, t.align); // 开始地址对齐

        for (int i = 0; i < elements->length; i++) {
            type_t *elem = (type_t *) ct_list_value(elements, i);
            type_t element_type = *elem;
            uint64_t element_size = element_type.storage_size;
            uint64_t element_align = element_type.align;

            // 每个元素地址对齐
            offset = align_up(offset, element_align);
            int64_t count = amd64_type_classify(element_type, lo, hi, offset);
            if (count == 0) {
                return 0;
            }

            offset += element_size;
        }

        // 最终 size 对齐
        offset = align_up(offset, t.align);
    } else {
        if (offset < 8) {
            *lo = amd64_classify_merge(*lo, AMD64_CLASS_INTEGER);
        } else {
            *hi = amd64_classify_merge(*hi, AMD64_CLASS_INTEGER);
        }
    }

    return *hi == AMD64_CLASS_NO ? 1 : 2;
}

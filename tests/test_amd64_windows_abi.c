#include "src/lower/amd64_abi.h"

#include "src/register/register.h"
#include "src/symbol/symbol.h"

#include <stdio.h>
#include <string.h>

#define WINDOWS_ABI_CHECK(condition)                                             \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "windows ABI check failed at %s:%d: %s\n", __FILE__, \
                    __LINE__, #condition);                                       \
            return false;                                                        \
        }                                                                        \
    } while (0)

static type_t test_aggregate_type(uint64_t size) {
    type_t type;
    memset(&type, 0, sizeof(type));
    type.kind = TYPE_STRUCT;
    type.storage_kind = STORAGE_KIND_IND;
    type.map_imm_kind = TYPE_ANYPTR;
    type.storage_size = (int64_t) size;
    type.align = (int64_t) min(size, (uint64_t) QWORD);
    return type;
}

static lir_var_t *test_var(char *ident, type_t type) {
    lir_var_t *var = NEW(lir_var_t);
    var->ident = ident;
    var->old = ident;
    var->type = type;
    return var;
}

static module_t *test_module(void) {
    module_t *module = NEW(module_t);
    module->ident = "windows_abi_test";
    module->global_symbol_table = table_new();
    module->global_symbols = slice_new();
    module->asm_global_symbols = slice_new();
    return module;
}

static bool test_windows_aggregate_classification(void) {
    static const uint64_t register_sizes[] = {BYTE, WORD, DWORD, QWORD};

    for (size_t i = 0; i < sizeof(register_sizes) / sizeof(register_sizes[0]); ++i) {
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_abi_type_classify(test_aggregate_type(register_sizes[i]),
                                                &lo, &hi, 0);
        WINDOWS_ABI_CHECK(count == 1);
        WINDOWS_ABI_CHECK(lo == AMD64_CLASS_INTEGER);
        WINDOWS_ABI_CHECK(hi == AMD64_CLASS_NO);
    }

    static const uint64_t indirect_sizes[] = {16U, 24U, 40U};
    for (size_t i = 0; i < sizeof(indirect_sizes) / sizeof(indirect_sizes[0]); ++i) {
        amd64_class_t lo = AMD64_CLASS_NO;
        amd64_class_t hi = AMD64_CLASS_NO;
        int64_t count = amd64_abi_type_classify(test_aggregate_type(indirect_sizes[i]),
                                                &lo, &hi, 0);
        WINDOWS_ABI_CHECK(count == 0);
        WINDOWS_ABI_CHECK(lo == AMD64_CLASS_NO);
        WINDOWS_ABI_CHECK(hi == AMD64_CLASS_NO);
    }

    return true;
}

static bool test_small_aggregate_return_registers(void) {
    static const uint64_t register_sizes[] = {BYTE, WORD, DWORD, QWORD};

    for (size_t i = 0; i < sizeof(register_sizes) / sizeof(register_sizes[0]); ++i) {
        type_t return_type = test_aggregate_type(register_sizes[i]);
        ast_fndef_t fndef = {.return_type = return_type};
        module_t *module = test_module();
        closure_t *closure = lir_closure_new(&fndef);
        closure->module = module;
        module->current_closure = closure;

        lir_var_t *return_var = test_var("return_value", return_type);
        lir_op_t *return_op = lir_op_new(
                LIR_OPCODE_RETURN, operand_new(LIR_OPERAND_VAR, return_var), NULL, NULL);
        linked_t *lowered = amd64_lower_return(closure, return_op);

        WINDOWS_ABI_CHECK(linked_count(lowered) == 2U);
        lir_op_t *move = linked_first(lowered)->value;
        WINDOWS_ABI_CHECK(move->code == LIR_OPCODE_MOVE);
        WINDOWS_ABI_CHECK(move->output->assert_type == LIR_OPERAND_REG);
        reg_t *return_reg = move->output->value;
        WINDOWS_ABI_CHECK(return_reg->index == rax->index);
        WINDOWS_ABI_CHECK(return_reg->size == register_sizes[i]);
    }

    return true;
}

static bool test_hidden_sret_callee_slot(uint64_t aggregate_size) {
    type_t return_type = test_aggregate_type(aggregate_size);
    ast_fndef_t fndef = {.return_type = return_type};
    module_t *module = test_module();
    closure_t *closure = lir_closure_new(&fndef);
    closure->module = module;
    module->current_closure = closure;

    slice_t *params = slice_new();
    slice_push(params, test_var("first_user_arg", type_kind_new(TYPE_UINT64)));
    lir_op_t *fn_begin = lir_op_output(
            LIR_OPCODE_FN_BEGIN, operand_new(LIR_OPERAND_PARAMS, params));
    linked_t *lowered = amd64_lower_fn_begin(closure, fn_begin);

    WINDOWS_ABI_CHECK(closure->return_big_operand != NULL);
    WINDOWS_ABI_CHECK(linked_count(lowered) == 3U);

    linked_node *node = linked_first(lowered)->succ;
    lir_op_t *sret_move = node->value;
    WINDOWS_ABI_CHECK(sret_move->code == LIR_OPCODE_MOVE);
    WINDOWS_ABI_CHECK(sret_move->first->assert_type == LIR_OPERAND_REG);
    reg_t *sret_reg = sret_move->first->value;
    WINDOWS_ABI_CHECK(sret_reg->index == rcx->index);
    WINDOWS_ABI_CHECK(sret_reg->size == QWORD);

    lir_op_t *user_arg_move = node->succ->value;
    WINDOWS_ABI_CHECK(user_arg_move->code == LIR_OPCODE_MOVE);
    WINDOWS_ABI_CHECK(user_arg_move->first->assert_type == LIR_OPERAND_REG);
    reg_t *user_arg_reg = user_arg_move->first->value;
    WINDOWS_ABI_CHECK(user_arg_reg->index == rdx->index);
    WINDOWS_ABI_CHECK(user_arg_reg->size == QWORD);

    return true;
}

static bool test_hidden_sret_caller_rcx(void) {
    ast_fndef_t fndef = {.return_type = type_kind_new(TYPE_VOID)};
    module_t *module = test_module();
    closure_t *closure = lir_closure_new(&fndef);
    closure->module = module;
    module->current_closure = closure;

    lir_var_t *result_var = test_var("call_result", type_kind_new(TYPE_STRING));
    lir_operand_t *result = operand_new(LIR_OPERAND_VAR, result_var);
    lir_op_t *call = lir_call("callee", result, 0);
    linked_t *lowered = amd64_lower_call(closure, call);

    bool found_call_arg = false;
    bool found_call = false;
    LINKED_FOR(lowered) {
        lir_op_t *op = node->value;
        if (op->code == LIR_OPCODE_MOVE && op->resolve_char == '~') {
            WINDOWS_ABI_CHECK(!found_call_arg);
            WINDOWS_ABI_CHECK(op->output->assert_type == LIR_OPERAND_VAR);
            lir_var_t *arg = op->output->value;
            WINDOWS_ABI_CHECK(arg->must_hint != NULL);
            WINDOWS_ABI_CHECK(arg->must_hint->index == rcx->index);
            WINDOWS_ABI_CHECK(arg->must_hint->size == QWORD);
            found_call_arg = true;
        }
        if (op->code == LIR_OPCODE_CALL) {
            WINDOWS_ABI_CHECK(op->output->assert_type == LIR_OPERAND_REG);
            reg_t *output = op->output->value;
            WINDOWS_ABI_CHECK(output->index == rax->index);
            found_call = true;
        }
    }

    WINDOWS_ABI_CHECK(found_call_arg);
    WINDOWS_ABI_CHECK(found_call);
    return true;
}

int main(void) {
    BUILD_OS = OS_WINDOWS;
    BUILD_ARCH = ARCH_AMD64;
    symbol_init();
    reg_init();

    if (!test_windows_aggregate_classification()) return 1;
    if (!test_small_aggregate_return_registers()) return 1;
    if (!test_hidden_sret_callee_slot(16U)) return 1;
    if (!test_hidden_sret_callee_slot(24U)) return 1;
    if (!test_hidden_sret_caller_rcx()) return 1;
    return 0;
}

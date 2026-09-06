#include "src/lower/amd64_abi.h"
#include "src/lower/arm64_abi.h"
#include "src/lower/riscv64_abi.h"
#include "src/register/register.h"
#include "src/symbol/symbol.h"

#include <stdio.h>

#define ABI_CHECK(condition)                                                    \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "aggregate return ABI check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

static type_t pair_type(void) {
    type_t element = type_kind_new(TYPE_INT64);
    type_tuple_t *tuple = NEW(type_tuple_t);
    tuple->elements = ct_list_new(sizeof(type_t));
    ct_list_push(tuple->elements, &element);
    ct_list_push(tuple->elements, &element);

    type_t result = type_new(TYPE_TUPLE, tuple);
    result.storage_kind = STORAGE_KIND_IND;
    result.map_imm_kind = TYPE_ANYPTR;
    result.storage_size = 16;
    result.align = 8;
    result.abi_struct = type_abi_struct(result);
    return result;
}

static module_t *test_module(void) {
    module_t *module = NEW(module_t);
    module->ident = "aggregate_return_abi_test";
    module->global_symbol_table = table_new();
    module->global_symbols = slice_new();
    module->asm_global_symbols = slice_new();
    return module;
}

static linked_t *lower_pair_return(int arch) {
    BUILD_ARCH = arch;
    BUILD_OS = arch == ARCH_ARM64 ? OS_DARWIN : OS_LINUX;
    reg_init();

    type_t return_type = pair_type();
    ast_fndef_t *fndef = NEW(ast_fndef_t);
    fndef->return_type = return_type;

    module_t *module = test_module();
    closure_t *closure = lir_closure_new(fndef);
    closure->module = module;
    module->current_closure = closure;

    lir_var_t *return_var = NEW(lir_var_t);
    return_var->ident = "return_value";
    return_var->old = return_var->ident;
    return_var->type = return_type;

    lir_op_t *return_op = lir_op_new(
            LIR_OPCODE_RETURN, operand_new(LIR_OPERAND_VAR, return_var), NULL, NULL);
    if (arch == ARCH_AMD64) return amd64_lower_return(closure, return_op);
    if (arch == ARCH_ARM64) return arm64_lower_return(closure, return_op);
    return riscv64_lower_return(closure, return_op);
}

static bool check_arch(int arch) {
    linked_t *lowered = lower_pair_return(arch);
    ABI_CHECK(linked_count(lowered) == 3U);

    lir_op_t *high_move = linked_first(lowered)->value;
    lir_op_t *low_move = linked_first(lowered)->succ->value;
    ABI_CHECK(high_move->code == LIR_OPCODE_MOVE);
    ABI_CHECK(low_move->code == LIR_OPCODE_MOVE);
    ABI_CHECK(high_move->first->assert_type == LIR_OPERAND_INDIRECT_ADDR);
    ABI_CHECK(low_move->first->assert_type == LIR_OPERAND_INDIRECT_ADDR);

    lir_indirect_addr_t *high = high_move->first->value;
    lir_indirect_addr_t *low = low_move->first->value;
    ABI_CHECK(high->offset == 8);
    ABI_CHECK(low->offset == 0);
    return true;
}

int main(void) {
    symbol_init();
    if (!check_arch(ARCH_AMD64)) return 1;
    if (!check_arch(ARCH_ARM64)) return 1;
    if (!check_arch(ARCH_RISCV64)) return 1;
    return 0;
}

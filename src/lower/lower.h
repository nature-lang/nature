#ifndef NATURE_SRC_LOWER_LOWER_H_
#define NATURE_SRC_LOWER_LOWER_H_

static inline void lower_imm_symbol(closure_t *c, lir_operand_t *imm_operand, linked_t *list, linked_t *symbol_operations) {
    lir_imm_t *imm = imm_operand->value;
    assert(imm);

    // check value exists, if exists assign unique var ident, and add lea global to var ident
    // and change op use imm to var ident
    char *key;
    if (imm->kind == TYPE_RAW_STRING) {
        key = str_connect("s:", imm->string_value);
    } else {
        key = str_connect("f:", itoa(imm->int_value));
    }

    asm_global_symbol_t *global_symbol = table_get(c->module->global_symbol_table, key);
    if (!global_symbol) {
        char *unique_name = var_unique_ident(c->module, "g");
        global_symbol = NEW(asm_global_symbol_t);
        assert(global_symbol);
        global_symbol->name = unique_name;

        if (imm->kind == TYPE_RAW_STRING) {
            assert(imm->string_value);
            global_symbol->size = imm->strlen + 1;
            global_symbol->value = (uint8_t *) imm->string_value;
        } else if (imm->kind == TYPE_FLOAT64) {
            global_symbol->size = type_kind_sizeof(imm->kind);
            global_symbol->value = (uint8_t *) &imm->f64_value;
        } else if (imm->kind == TYPE_FLOAT32) {
            global_symbol->size = type_kind_sizeof(imm->kind);
            global_symbol->value = (uint8_t *) &imm->f32_value;
        } else {
            assertf(false, "not support type %s", type_kind_str[imm->kind]);
        }

        symbol_table_set_raw_string(c->module, global_symbol->name, type_kind_new(TYPE_RAW_STRING), imm->strlen);
        slice_push(c->asm_symbols, global_symbol);
        table_set(c->module->global_symbol_table, key, global_symbol);
    }

    lir_symbol_var_t *symbol_var = NEW(lir_symbol_var_t);
    symbol_var->kind = imm->kind;
    symbol_var->ident = global_symbol->name;

    lir_operand_t *local_var_operand = NULL;
    // float 采用共享插入方案，然后尝试通过 interval split 进行优化
    if (imm->kind == TYPE_RAW_STRING) {
        local_var_operand = temp_var_operand(c->module, type_kind_new(imm->kind));
        linked_push(list, lir_op_lea(local_var_operand, operand_new(LIR_OPERAND_SYMBOL_VAR, symbol_var)));
    } else {
        // 插入到函数开头，后续在尝试优化
        local_var_operand = table_get(c->local_imm_table, key);
        if (local_var_operand == NULL) {
            local_var_operand = temp_var_operand(c->module, type_kind_new(imm->kind));

            lir_operand_t *symbol_ptr = temp_var_operand(c->module, type_kind_new(TYPE_ANYPTR));
            linked_push(symbol_operations, lir_op_lea(symbol_ptr, operand_new(LIR_OPERAND_SYMBOL_VAR, symbol_var)));
            lir_operand_t *src = indirect_addr_operand(c->module, type_kind_new(imm->kind), symbol_ptr, 0);
            linked_push(symbol_operations, lir_op_move(local_var_operand, src));

            table_set(c->local_imm_table, key, local_var_operand);
        }
    }

    // imm replace to local var
    lir_operand_t *temp_operand = lir_reset_operand(local_var_operand, imm_operand->pos);
    imm_operand->assert_type = temp_operand->assert_type;
    imm_operand->value = temp_operand->value;
}

#endif //NATURE_SRC_LOWER_LOWER_H_

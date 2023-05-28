#include "generic.h"
#include "utils/table.h"

/**
 * @param t
 */
static void generic_params_collect(ast_fndef_t *fndef, type_t t) {
    if (t.kind == TYPE_ALIAS) {
        if (t.alias->actual_params) {
            for (int i = 0; i < t.alias->actual_params->length; ++i) {
                type_t *temp = ct_list_value(t.alias->actual_params, i);
                generic_params_collect(fndef, *temp);
            }

            return;
        }

        symbol_t *symbol = symbol_table_get(t.alias->literal);
        assertf(symbol->type == SYMBOL_TYPEDEF, "'%s' is not a type", symbol->ident);
        ast_type_alias_stmt_t *type_alias_stmt = symbol->ast_value;

        if (type_alias_stmt->type.kind != TYPE_GENERIC) {
            return;
        }

        if (!table_exist(fndef->exists_generic_params, symbol->ident)) {
            slice_push(fndef->generic_params, type_alias_stmt);
            table_set(fndef->exists_generic_params, symbol->ident, type_alias_stmt);
        }
        return;
    }

    // 复合类型进行递归处理
    if (t.kind == TYPE_POINTER) {
        generic_params_collect(fndef, t.pointer->value_type);
        return;
    }

    if (t.kind == TYPE_LIST) {
        generic_params_collect(fndef, t.list->element_type);
        return;
    }

    if (t.kind == TYPE_MAP) {
        generic_params_collect(fndef, t.map->key_type);
        generic_params_collect(fndef, t.map->value_type);
        return;
    }

    if (t.kind == TYPE_SET) {
        generic_params_collect(fndef, t.set->element_type);
        return;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *temp = ct_list_value(tuple->elements, i);
            generic_params_collect(fndef, *temp);
        }
        return;
    }

    if (t.kind == TYPE_FN) {
        type_fn_t *fn = t.fn;
        // 可选的返回类型
        generic_params_collect(fndef, fn->return_type);

        for (int i = 0; i < fn->formal_types->length; ++i) {
            type_t *formal_type = ct_list_value(fn->formal_types, i);
            generic_params_collect(fndef, *formal_type);
        }
        return;
    }

    if (t.kind == TYPE_STRUCT) {
        type_struct_t *s = t.struct_;

        for (int i = 0; i < s->properties->length; ++i) {
            struct_property_t *p = ct_list_value(s->properties, i);
            generic_params_collect(fndef, p->type);
        }

        return;
    }
}

/**
 * 使用递归基于数组的索引的笛卡尔积组合
 */
static void generic_cartesian_product(list_t *products, slice_t *generic_params, type_t **element, int depth) {
    ast_type_alias_stmt_t *stmt = generic_params->take[depth];
    assert(stmt->type.kind == TYPE_GENERIC);
    if (depth == generic_params->count) {
        // element 已经收集完毕，现在可以写入到 products 中并退出递归
        ct_list_push(products, element);
        return;
    }

    type_generic_t *generic = stmt->type.generic;
    for (int i = 0; i < generic->constraints->length; ++i) {
        // type*
        element[depth] = ct_list_value(generic->constraints, i);
        generic_cartesian_product(products, generic_params, element, depth + 1);
    }
}


/**
 * @param fndef
 * @return slice_t of ast_fndef_t
 */
static slice_t *generic_global_fndef(ast_fndef_t *fndef) {
    slice_t *result = slice_new();

    fndef->exists_generic_params = table_new();
    fndef->generic_params = slice_new();

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl_t *var = ct_list_value(fndef->formals, i);
        generic_params_collect(fndef, var->type);
    }

    if (fndef->generic_params->count == 0) {
        slice_push(result, fndef);
        for (int i = 0; i < fndef->child_fndefs->count; ++i) {
            slice_push(result, fndef->child_fndefs->take[i]);
        }
        return result;
    }


    slice_t *temps = fndef->child_fndefs;
    slice_push(temps, fndef);

    // - 根据 generic_types 中的约束信息生成笛卡尔积列表，列表中的每个元素都是
    uint64_t element_size = sizeof(type_t *) * fndef->generic_params->count;
    list_t *products = ct_list_new(element_size);  // 收集的全量排列组合结果集
    type_t **element = mallocz(element_size);
    generic_cartesian_product(products, fndef->generic_params, element, 0);
    // 写入到 result 中 table_t *generic_assign; key is generic->ident, value is *type_t
    for (int i = 0; i < products->length; ++i) {
        // element 的长度等于 fndef->generic_params->count
        element = ct_list_value(products, i);

        slice_t *fndefs = slice_new();
        for (int j = 0; j < temps->count; ++j) {
            ast_fndef_t *new_fndef = ast_fndef_copy(temps->take[j]);

            slice_push(fndefs, new_fndef);
            slice_push(result, new_fndef);
        }

        for (int j = 0; j < fndefs->count; ++j) {
            ast_fndef_t *new_fndef = fndefs->take[j];
            new_fndef->generic_assign = table_new();

            for (int k = 0; k < fndef->generic_params->count; ++k) {
                ast_type_alias_stmt_t *stmt = fndef->generic_params->take[k];
                type_t *assign_type = element[k];
                table_set(new_fndef->generic_assign, stmt->ident, assign_type);
            }
        }
    }

    return result;
}

void generic(module_t *m) {
    slice_t *ast_fndefs = slice_new();
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        slice_concat(ast_fndefs, generic_global_fndef(fndef));
    }
    m->ast_fndefs = ast_fndefs;
}

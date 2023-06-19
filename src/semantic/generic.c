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

        symbol_t *symbol = symbol_table_get(t.alias->ident);
        assertf(symbol, "type '%s' not found", t.alias->ident);
        assertf(symbol->type == SYMBOL_TYPE_ALIAS, "'%s' is not a type", symbol->ident);
        ast_type_alias_stmt_t *type_alias_stmt = symbol->ast_value;

        if (type_alias_stmt->type.kind != TYPE_GEN) {
            return;
        }

        if (type_alias_stmt->type.gen->any) {
            return;
        }

        if (table_exist(fndef->exists_generic_params, symbol->ident)) {
            return;
        }

        slice_push(fndef->generic_params, type_alias_stmt);
        table_set(fndef->exists_generic_params, symbol->ident, type_alias_stmt);
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
    if (depth == generic_params->count) {
        // element 已经收集完毕，现在可以写入到 products 中并退出递归
        ct_list_push(products, element);
        return;
    }


    ast_type_alias_stmt_t *stmt = generic_params->take[depth];
    assert(stmt->type.kind == TYPE_GEN);
    type_gen_t *generic = stmt->type.gen;
    for (int i = 0; i < generic->elements->length; ++i) {
        // type*
        element[depth] = ct_list_value(generic->elements, i);
        generic_cartesian_product(products, generic_params, element, depth + 1);
    }
}


/**
 * global fn 必须放在最前面，才能先解析出其中的必要参数
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

    // 非泛型 global fn, 此时 generic_assign 为 null
    if (fndef->generic_params->count == 0) {
        slice_push(result, fndef);
        if (fndef->local_children) {
            slice_concat(result, fndef->local_children);
            fndef->local_children = NULL;
        }

        return result;
    }

    // 泛型 global 函数处理
    slice_t *temps = slice_new();
    slice_push(temps, fndef);
    if (fndef->local_children) {
        slice_concat(temps, fndef->local_children);
        fndef->local_children = NULL;
    }

    // - 根据 generic_types 中的约束信息生成笛卡尔积列表，列表中的每个元素都是
    uint64_t element_size = sizeof(type_t *) * fndef->generic_params->count;
    list_t *products = ct_list_new(element_size);  // 收集的全量排列组合结果集
    type_t **element = mallocz(element_size);
    generic_cartesian_product(products, fndef->generic_params, element, 0);
    // 写入到 result 中 table_t *generic_assign; key is generic->ident, value is *type_t
    for (int i = 0; i < products->length; ++i) {
        // element 的长度等于 fndef->generic_params->count
        element = ct_list_value(products, i);

        // 包含 global 和 local fn
        slice_t *fndefs = slice_new();
        for (int j = 0; j < temps->count; ++j) {
            // 最后一组 gen types 直接基于 temps 进行生成，避免 symbol 中存在 generic_assign = null 的 temp
            ast_fndef_t *new_fndef;
            if (i == products->length - 1) {
                new_fndef = temps->take[j];
            } else {
                // 这里进行了深度 copy, 所有的表达式之间不会再有关联关系
                new_fndef = ast_fndef_copy(temps->take[j]);
                // 将 new fn 添加到 symbol table 中 (依旧使用原始的名称, 方便使用者可以定位函数信息)
                symbol_table_set(new_fndef->symbol_name, SYMBOL_FN, new_fndef, fndef->is_local);
            }


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

/**
 * generic 中无论是泛型还是非泛型 fn, 都进行了 local fn 进行了展开平铺处理, 所以 m->ast_fndefs 中包含 local 和 global fn
 * @param m
 */
void generic(module_t *m) {
    slice_t *ast_fndefs = slice_new();
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        slice_concat(ast_fndefs, generic_global_fndef(fndef));
    }
    m->ast_fndefs = ast_fndefs;
}

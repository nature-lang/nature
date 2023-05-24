#include "generic.h"
#include "utils/table.h"

/**
 * @param t
 */
static void generic_params_collect(ast_fndef_t *fndef, type_t t) {
    if (t.kind == TYPE_ALIAS) {
        if (t.alias->actual_types) {
            for (int i = 0; i < t.alias->actual_types->length; ++i) {
                type_t *temp = ct_list_value(t.alias->actual_types, i);
                generic_params_collect(fndef, *temp);
            }

            return;
        }

        symbol_t *symbol = symbol_table_get(t.alias->literal);
        assertf(symbol->type == SYMBOL_TYPEDEF, "'%s' is not a type", symbol->ident);
        ast_type_alias_stmt *type_alias_stmt = symbol->ast_value;

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
void generic_cartesian_product(list_t *products, slice_t *generic_params, type_t **element, int depth) {
    ast_type_alias_stmt *stmt = generic_params->take[depth];
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
 *
 * @param fndef
 * @return slice_t of ast_fndef_t
 */
slice_t *generic(ast_fndef_t *fndef) {
    slice_t *fndefs = slice_new();

    fndef->exists_generic_params = table_new();
    fndef->generic_params = slice_new();

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var = ct_list_value(fndef->formals, i);
        generic_params_collect(fndef, var->type);
    }

    if (fndef->generic_params->count == 0) {
        slice_push(fndefs, fndef);
        return fndefs;
    }

    // - 根据 generic_types 中的约束信息生成笛卡尔积列表，列表中的每个元素都是
    uint64_t element_size = sizeof(type_t *) * fndef->generic_params->count;
    list_t *products = ct_list_new(element_size);  // 收集的全量排列组合结果集
    type_t **element = mallocz(element_size);
    generic_cartesian_product(products, fndef->generic_params, element, 0);
    // table_t *generic_assign; // key is generic->ident, value is *type_t
    // 写入到 fndefs 中

}

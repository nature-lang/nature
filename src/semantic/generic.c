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
void params_cartesian_product(list_t *products, slice_t *generic_params, int depth) {

}

/**
 *
 * @param fndef
 * @return slice_t of ast_fndef_t
 */
slice_t *generic(ast_fndef_t *fndef) {
    slice_t *fndefs = slice_new();

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var = ct_list_value(fndef->formals, i);
        generic_params_collect(fndef, var->type);
    }

    if (fndef->generic_params->count == 0) {
        slice_push(fndefs, fndef);
        return fndefs;
    }

    /**
     typedef struct {
        string ident; // my_int (自定义的类型名称)
        type_t type; // int (类型)
    } ast_typedef_stmt;
     */
    // - 根据 generic_types 中的约束信息生成笛卡尔积列表，列表中的每个元素都是
    // table_t *generic_assign; // key is generic->ident, value is *type_t
    // 通过这个列表我将生产一组函数

}

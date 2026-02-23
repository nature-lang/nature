#include "global_eval.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "infer.h"
#include "src/error.h"
#include "src/rtype.h"
#include "src/symbol/symbol.h"
#include "utils/helper.h"

static bool global_type_confirmed(type_t t) {
    if (t.kind == TYPE_UNKNOWN) {
        return false;
    }

    if (t.kind == TYPE_VEC) {
        return t.vec->element_type.kind != TYPE_UNKNOWN;
    }

    if (t.kind == TYPE_MAP) {
        return t.map->key_type.kind != TYPE_UNKNOWN && t.map->value_type.kind != TYPE_UNKNOWN;
    }

    if (t.kind == TYPE_SET) {
        return t.set->element_type.kind != TYPE_UNKNOWN;
    }

    if (t.kind == TYPE_ARR) {
        return t.array->element_type.kind != TYPE_UNKNOWN;
    }

    if (t.kind == TYPE_TUPLE) {
        for (int i = 0; i < t.tuple->elements->length; ++i) {
            type_t *element = ct_list_value(t.tuple->elements, i);
            if (!global_type_confirmed(*element)) {
                return false;
            }
        }
    }

    if (t.kind == TYPE_STRUCT) {
        for (int i = 0; i < t.struct_->properties->length; ++i) {
            struct_property_t *p = ct_list_value(t.struct_->properties, i);
            if (!global_type_confirmed(p->type)) {
                return false;
            }
        }
    }

    return true;
}

static void global_eval_precheck_expr(module_t *m, ast_expr_t *expr) {
    SET_LINE_COLUMN(expr);

    switch (expr->assert_type) {
        case AST_EXPR_LITERAL:
        case AST_EXPR_EMPTY_CURLY_NEW:
        case AST_MACRO_EXPR_DEFAULT:
        case AST_MACRO_EXPR_REFLECT_HASH:
            return;
        case AST_EXPR_UNARY: {
            ast_unary_expr_t *unary = expr->value;
            global_eval_precheck_expr(m, &unary->operand);
            return;
        }
        case AST_EXPR_BINARY: {
            ast_binary_expr_t *binary = expr->value;
            global_eval_precheck_expr(m, &binary->left);
            global_eval_precheck_expr(m, &binary->right);
            return;
        }
        case AST_EXPR_TERNARY: {
            ast_ternary_expr_t *ternary = expr->value;
            global_eval_precheck_expr(m, &ternary->condition);
            global_eval_precheck_expr(m, &ternary->consequent);
            global_eval_precheck_expr(m, &ternary->alternate);
            return;
        }
        case AST_EXPR_AS: {
            ast_as_expr_t *as_expr = expr->value;
            global_eval_precheck_expr(m, &as_expr->src);
            return;
        }
        case AST_EXPR_ARRAY_NEW: {
            ast_array_new_t *array_new = expr->value;
            for (int i = 0; i < array_new->elements->length; ++i) {
                ast_expr_t *item = ct_list_value(array_new->elements, i);
                global_eval_precheck_expr(m, item);
            }
            return;
        }
        case AST_EXPR_ARRAY_REPEAT_NEW: {
            ast_array_repeat_new_t *repeat_new = expr->value;
            global_eval_precheck_expr(m, &repeat_new->default_element);
            global_eval_precheck_expr(m, &repeat_new->length_expr);
            return;
        }
        case AST_EXPR_VEC_REPEAT_NEW: {
            ast_vec_repeat_new_t *repeat_new = expr->value;
            global_eval_precheck_expr(m, &repeat_new->default_element);
            global_eval_precheck_expr(m, &repeat_new->length_expr);
            return;
        }
        case AST_EXPR_VEC_NEW: {
            ast_vec_new_t *vec_new = expr->value;
            for (int i = 0; i < vec_new->elements->length; ++i) {
                ast_expr_t *item = ct_list_value(vec_new->elements, i);
                global_eval_precheck_expr(m, item);
            }
            return;
        }
        case AST_EXPR_MAP_NEW: {
            ast_map_new_t *map_new = expr->value;
            for (int i = 0; i < map_new->elements->length; ++i) {
                ast_map_element_t *item = ct_list_value(map_new->elements, i);
                global_eval_precheck_expr(m, &item->key);
                global_eval_precheck_expr(m, &item->value);
            }
            return;
        }
        case AST_EXPR_SET_NEW: {
            ast_set_new_t *set_new = expr->value;
            for (int i = 0; i < set_new->elements->length; ++i) {
                ast_expr_t *item = ct_list_value(set_new->elements, i);
                global_eval_precheck_expr(m, item);
            }
            return;
        }
        case AST_EXPR_TUPLE_NEW: {
            ast_tuple_new_t *tuple_new = expr->value;
            for (int i = 0; i < tuple_new->elements->length; ++i) {
                ast_expr_t *item = ct_list_value(tuple_new->elements, i);
                global_eval_precheck_expr(m, item);
            }
            return;
        }
        case AST_EXPR_STRUCT_NEW: {
            ast_struct_new_t *struct_new = expr->value;
            for (int i = 0; i < struct_new->properties->length; ++i) {
                struct_property_t *property = ct_list_value(struct_new->properties, i);
                global_eval_precheck_expr(m, property->right);
            }
            return;
        }
        case AST_EXPR_IDENT: {
            ast_ident *ident = expr->value;
            symbol_t *symbol = symbol_table_get_noref(ident->literal);
            INFER_ASSERTF(symbol, "ident '%s' undeclared", ident->literal);
            INFER_ASSERTF(symbol->type != SYMBOL_VAR, "global initializer cannot reference global var '%s'",
                          ident->literal);
            INFER_ASSERTF(false, "global initializer cannot reference ident '%s'", ident->literal);
            return;
        }
        default:
            INFER_ASSERTF(false, "global initializer expression type=%d is not compile-time evaluable",
                          expr->assert_type);
            return;
    }
}

static void global_eval_write_default(type_t t, uint8_t *dst) {
    if (t.storage_size > 0) {
        memset(dst, 0, t.storage_size);
    }

    if (t.kind == TYPE_STRING) {
        n_string_t *str = (n_string_t *) dst;
        str->element_size = type_kind_sizeof(TYPE_UINT8);
        str->hash = type_hash(t);
        return;
    }

    if (t.kind == TYPE_VEC) {
        n_vec_t *vec = (n_vec_t *) dst;
        vec->element_size = t.vec->element_type.storage_size;
        vec->hash = type_hash(t);
        return;
    }

    if (t.kind == TYPE_MAP) {
        n_map_t *map = (n_map_t *) dst;
        map->key_rtype_hash = (uint64_t) type_hash(t.map->key_type);
        map->value_rtype_hash = (uint64_t) type_hash(t.map->value_type);
        return;
    }

    if (t.kind == TYPE_SET) {
        n_set_t *set = (n_set_t *) dst;
        set->key_rtype_hash = (uint64_t) type_hash(t.set->element_type);
        return;
    }

    if (t.kind == TYPE_ARR) {
        int64_t element_size = t.array->element_type.storage_size;
        for (int64_t i = 0; i < t.array->length; ++i) {
            global_eval_write_default(t.array->element_type, dst + i * element_size);
        }
        return;
    }

    if (t.kind == TYPE_TUPLE) {
        for (int i = 0; i < t.tuple->elements->length; ++i) {
            type_t *element_type = ct_list_value(t.tuple->elements, i);
            int64_t offset = type_tuple_offset(t.tuple, i);
            global_eval_write_default(*element_type, dst + offset);
        }
        return;
    }

    if (t.kind == TYPE_STRUCT) {
        for (int i = 0; i < t.struct_->properties->length; ++i) {
            struct_property_t *property = ct_list_value(t.struct_->properties, i);
            uint64_t offset = type_struct_offset(t.struct_, property->name);
            global_eval_write_default(property->type, dst + offset);
        }
    }
}

static bool global_eval_literal_as_bool(module_t *m, ast_literal_t *literal) {
    if (literal->kind == TYPE_BOOL) {
        return str_equal(literal->value, "true");
    }

    if (is_integer_or_anyptr(literal->kind)) {
        char *endptr;
        uint64_t v = strtoull(literal->value, &endptr, 0);
        INFER_ASSERTF(*endptr == '\0', "invalid integer literal '%s'", literal->value);
        return v != 0;
    }

    if (is_float(literal->kind)) {
        char *endptr;
        double v = strtod(literal->value, &endptr);
        INFER_ASSERTF(*endptr == '\0', "invalid float literal '%s'", literal->value);
        return v != 0.0;
    }

    INFER_ASSERTF(false, "cannot cast literal kind '%s' to bool", type_kind_str[literal->kind]);
    return false;
}

static int64_t global_eval_literal_as_i64(module_t *m, ast_literal_t *literal) {
    if (literal->kind == TYPE_BOOL) {
        return str_equal(literal->value, "true") ? 1 : 0;
    }

    if (is_signed(literal->kind)) {
        char *endptr;
        int64_t v = strtoll(literal->value, &endptr, 0);
        INFER_ASSERTF(*endptr == '\0', "invalid integer literal '%s'", literal->value);
        return v;
    }

    if (is_unsigned(literal->kind) || literal->kind == TYPE_ANYPTR) {
        char *endptr;
        uint64_t v = strtoull(literal->value, &endptr, 0);
        INFER_ASSERTF(*endptr == '\0', "invalid integer literal '%s'", literal->value);
        return (int64_t) v;
    }

    if (is_float(literal->kind)) {
        char *endptr;
        double v = strtod(literal->value, &endptr);
        INFER_ASSERTF(*endptr == '\0', "invalid float literal '%s'", literal->value);
        return (int64_t) v;
    }

    INFER_ASSERTF(false, "cannot cast literal kind '%s' to integer", type_kind_str[literal->kind]);
    return 0;
}

static uint64_t global_eval_literal_as_u64(module_t *m, ast_literal_t *literal) {
    if (literal->kind == TYPE_BOOL) {
        return str_equal(literal->value, "true") ? 1 : 0;
    }

    if (is_unsigned(literal->kind) || literal->kind == TYPE_ANYPTR) {
        char *endptr;
        uint64_t v = strtoull(literal->value, &endptr, 0);
        INFER_ASSERTF(*endptr == '\0', "invalid integer literal '%s'", literal->value);
        return v;
    }

    if (is_signed(literal->kind)) {
        char *endptr;
        int64_t v = strtoll(literal->value, &endptr, 0);
        INFER_ASSERTF(*endptr == '\0', "invalid integer literal '%s'", literal->value);
        return (uint64_t) v;
    }

    if (is_float(literal->kind)) {
        char *endptr;
        double v = strtod(literal->value, &endptr);
        INFER_ASSERTF(*endptr == '\0', "invalid float literal '%s'", literal->value);
        return (uint64_t) v;
    }

    INFER_ASSERTF(false, "cannot cast literal kind '%s' to integer", type_kind_str[literal->kind]);
    return 0;
}

static double global_eval_literal_as_f64(module_t *m, ast_literal_t *literal) {
    if (literal->kind == TYPE_BOOL) {
        return str_equal(literal->value, "true") ? 1.0 : 0.0;
    }

    if (is_number(literal->kind) || literal->kind == TYPE_ANYPTR) {
        char *endptr;
        double v = strtod(literal->value, &endptr);
        INFER_ASSERTF(*endptr == '\0', "invalid number literal '%s'", literal->value);
        return v;
    }

    INFER_ASSERTF(false, "cannot cast literal kind '%s' to float", type_kind_str[literal->kind]);
    return 0;
}

static void global_eval_write_literal(module_t *m, type_t target_type, ast_literal_t *literal, uint8_t *dst) {
    INFER_ASSERTF(literal != NULL, "global initializer literal is null");

    if (target_type.kind == TYPE_STRING) {
        INFER_ASSERTF(literal->kind == TYPE_STRING, "global string initializer must be string literal");
        INFER_ASSERTF(literal->len == 0, "global string initializer must be empty");
        global_eval_write_default(target_type, dst);
        return;
    }

    if (target_type.kind == TYPE_BOOL) {
        bool value = global_eval_literal_as_bool(m, literal);
        memmove(dst, &value, sizeof(bool));
        return;
    }

    if (target_type.kind == TYPE_FLOAT32) {
        float v = (float) global_eval_literal_as_f64(m, literal);
        memmove(dst, &v, sizeof(float));
        return;
    }

    if (target_type.kind == TYPE_FLOAT64 || target_type.kind == TYPE_FLOAT) {
        double v = global_eval_literal_as_f64(m, literal);
        memmove(dst, &v, sizeof(double));
        return;
    }

    if (target_type.kind == TYPE_ENUM) {
        type_t element_type = reduction_type(m, target_type.enum_->element_type);
        global_eval_write_literal(m, element_type, literal, dst);
        return;
    }

    if (target_type.kind == TYPE_INT8) {
        int8_t v = (int8_t) global_eval_literal_as_i64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_INT16) {
        int16_t v = (int16_t) global_eval_literal_as_i64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_INT32) {
        int32_t v = (int32_t) global_eval_literal_as_i64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_INT64 || target_type.kind == TYPE_INT) {
        int64_t v = global_eval_literal_as_i64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_UINT8) {
        uint8_t v = (uint8_t) global_eval_literal_as_u64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_UINT16) {
        uint16_t v = (uint16_t) global_eval_literal_as_u64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_UINT32) {
        uint32_t v = (uint32_t) global_eval_literal_as_u64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }
    if (target_type.kind == TYPE_UINT64 || target_type.kind == TYPE_UINT || target_type.kind == TYPE_ANYPTR) {
        uint64_t v = global_eval_literal_as_u64(m, literal);
        memmove(dst, &v, sizeof(v));
        return;
    }

    if (target_type.kind == TYPE_PTR || target_type.kind == TYPE_REF || target_type.kind == TYPE_FN ||
        target_type.kind == TYPE_CHAN || target_type.kind == TYPE_COROUTINE_T || target_type.kind == TYPE_NULL) {
        bool null_literal = literal->kind == TYPE_NULL;
        if (!null_literal && is_integer_or_anyptr(literal->kind)) {
            null_literal = global_eval_literal_as_u64(m, literal) == 0;
        }
        INFER_ASSERTF(null_literal, "pointer-like global initializer must be null");
        memset(dst, 0, target_type.storage_size);
        return;
    }

    if (target_type.kind == TYPE_UNION && target_type.union_->nullable) {
        INFER_ASSERTF(literal->kind == TYPE_NULL, "nullable union global initializer only supports null");
        memset(dst, 0, target_type.storage_size);
        return;
    }

    INFER_ASSERTF(false, "global initializer cannot assign literal to type '%s'", type_format(target_type));
}

static void global_eval_write_expr(module_t *m, type_t target_type, ast_expr_t *expr, uint8_t *dst) {
    SET_LINE_COLUMN(expr);

    switch (expr->assert_type) {
        case AST_EXPR_LITERAL: {
            global_eval_write_literal(m, target_type, expr->value, dst);
            return;
        }
        case AST_EXPR_AS: {
            ast_as_expr_t *as_expr = expr->value;
            INFER_ASSERTF(as_expr->src.assert_type == AST_EXPR_LITERAL, "global cast initializer must cast a literal");
            global_eval_write_literal(m, target_type, as_expr->src.value, dst);
            return;
        }
        case AST_MACRO_EXPR_DEFAULT: {
            global_eval_write_default(target_type, dst);
            return;
        }
        case AST_MACRO_EXPR_REFLECT_HASH: {
            ast_macro_reflect_hash_expr_t *reflect_expr = expr->value;
            ast_literal_t literal = {
                .kind = TYPE_INT,
                .value = itoa(type_hash(reflect_expr->target_type)),
                .len = 0,
            };
            global_eval_write_literal(m, target_type, &literal, dst);
            return;
        }
        case AST_EXPR_ARRAY_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_ARR, "array literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_array_new_t *array_new = expr->value;
            INFER_ASSERTF(array_new->elements->length <= target_type.array->length,
                          "array literal length overflow, expect=%ld, actual=%ld",
                          target_type.array->length, array_new->elements->length);

            global_eval_write_default(target_type, dst);
            int64_t element_size = target_type.array->element_type.storage_size;
            for (int64_t i = 0; i < array_new->elements->length; ++i) {
                ast_expr_t *item = ct_list_value(array_new->elements, i);
                global_eval_write_expr(m, target_type.array->element_type, item, dst + i * element_size);
            }
            return;
        }
        case AST_EXPR_ARRAY_REPEAT_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_ARR, "array repeat target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_array_repeat_new_t *repeat_new = expr->value;
            INFER_ASSERTF(repeat_new->length_expr.assert_type == AST_EXPR_LITERAL,
                          "array repeat length must be compile-time literal");
            ast_literal_t *length_literal = repeat_new->length_expr.value;
            int64_t length = global_eval_literal_as_i64(m, length_literal);
            INFER_ASSERTF(length == target_type.array->length,
                          "array repeat length mismatch, expect=%ld, actual=%ld", target_type.array->length, length);

            global_eval_write_default(target_type, dst);
            int64_t element_size = target_type.array->element_type.storage_size;
            for (int64_t i = 0; i < target_type.array->length; ++i) {
                global_eval_write_expr(m, target_type.array->element_type, &repeat_new->default_element,
                                       dst + i * element_size);
            }
            return;
        }
        case AST_EXPR_TUPLE_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_TUPLE, "tuple literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_tuple_new_t *tuple_new = expr->value;
            INFER_ASSERTF(tuple_new->elements->length == target_type.tuple->elements->length,
                          "tuple element count mismatch, expect=%ld, actual=%ld",
                          target_type.tuple->elements->length, tuple_new->elements->length);
            global_eval_write_default(target_type, dst);
            for (int i = 0; i < target_type.tuple->elements->length; ++i) {
                type_t *element_type = ct_list_value(target_type.tuple->elements, i);
                int64_t offset = type_tuple_offset(target_type.tuple, i);
                ast_expr_t *item = ct_list_value(tuple_new->elements, i);
                global_eval_write_expr(m, *element_type, item, dst + offset);
            }
            return;
        }
        case AST_EXPR_STRUCT_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_STRUCT, "struct literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_struct_new_t *struct_new = expr->value;
            global_eval_write_default(target_type, dst);
            for (int i = 0; i < struct_new->properties->length; ++i) {
                struct_property_t *property = ct_list_value(struct_new->properties, i);
                struct_property_t *expect_property = type_struct_property(target_type.struct_, property->name);
                INFER_ASSERTF(expect_property, "struct field '%s' not found", property->name);
                uint64_t offset = type_struct_offset(target_type.struct_, property->name);
                global_eval_write_expr(m, expect_property->type, property->right, dst + offset);
            }
            return;
        }
        case AST_EXPR_VEC_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_VEC, "vec literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_vec_new_t *vec_new = expr->value;
            INFER_ASSERTF(vec_new->elements->length == 0, "global vec initializer must be empty");
            global_eval_write_default(target_type, dst);
            return;
        }
        case AST_EXPR_MAP_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_MAP, "map literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_map_new_t *map_new = expr->value;
            INFER_ASSERTF(map_new->elements->length == 0, "global map initializer must be empty");
            global_eval_write_default(target_type, dst);
            return;
        }
        case AST_EXPR_SET_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_SET, "set literal target type mismatch, expect '%s'",
                          type_format(target_type));
            ast_set_new_t *set_new = expr->value;
            INFER_ASSERTF(set_new->elements->length == 0, "global set initializer must be empty");
            global_eval_write_default(target_type, dst);
            return;
        }
        case AST_EXPR_EMPTY_CURLY_NEW: {
            INFER_ASSERTF(target_type.kind == TYPE_MAP || target_type.kind == TYPE_SET,
                          "{} only supports map/set global initializer");
            global_eval_write_default(target_type, dst);
            return;
        }
        case AST_EXPR_UNARY:
        case AST_EXPR_BINARY:
        case AST_EXPR_TERNARY:
            INFER_ASSERTF(false, "global initializer must fold to literal before layout");
            return;
        default:
            INFER_ASSERTF(false, "global initializer expression type=%d is unsupported", expr->assert_type);
            return;
    }
}

void global_eval(module_t *m) {
    for (int i = 0; i < m->global_vardef->count; ++i) {
        ast_vardef_stmt_t *vardef = m->global_vardef->take[i];
        ast_var_decl_t *var_decl = &vardef->var_decl;

        INFER_ASSERTF(vardef->right != NULL, "global var '%s' must have initializer", var_decl->ident);

        SET_LINE_COLUMN(vardef->right);
        global_eval_precheck_expr(m, vardef->right);

        var_decl->type = reduction_type(m, var_decl->type);
        INFER_ASSERTF(var_decl->type.kind != TYPE_VOID, "cannot assign to void");

        type_t right_type = infer_global_expr(m, vardef->right, var_decl->type);
        INFER_ASSERTF(right_type.kind != TYPE_VOID, "cannot assign void to global var");

        if (var_decl->type.kind == TYPE_UNKNOWN) {
            INFER_ASSERTF(global_type_confirmed(right_type), "global right type not confirmed");
            var_decl->type = right_type;
        }

        var_decl->type = reduction_type(m, var_decl->type);
        INFER_ASSERTF(global_type_confirmed(var_decl->type), "global type not confirmed");

        vardef->global_data = mallocz(var_decl->type.storage_size);
        global_eval_write_expr(m, var_decl->type, vardef->right, vardef->global_data);
    }
}

#include "type.h"

#include "assertf.h"
#include "bitmap.h"
#include "ct_list.h"
#include "custom_links.h"
#include "helper.h"

int64_t type_kind_sizeof(type_kind t) {
    assert(is_number(t) || t == TYPE_BOOL || t == TYPE_ANYPTR || t == TYPE_VOID);

    switch (t) {
        case TYPE_VOID:
            return 0;
        case TYPE_BOOL:
        case TYPE_INT8:
        case TYPE_UINT8:
            return BYTE;
        case TYPE_INT16:
        case TYPE_UINT16:
            return WORD;
        case TYPE_INT32:
        case TYPE_UINT32:
        case TYPE_FLOAT32:
            return DWORD;
        case TYPE_INT64:
        case TYPE_UINT64:
        case TYPE_FLOAT64:
        case TYPE_ANYPTR:
            return QWORD;
        default:
            assert(false);
    }
}

int64_t type_struct_alignof(type_struct_t *s) {
    int64_t max_align = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);
        uint8_t element_align = type_alignof(p->type);
        if (element_align > max_align) {
            max_align = element_align;
        }
    }
    return max_align;
}


// 只有当 struct 没有元素(可以存在空的 struct 嵌套)，或者有嵌套 sub struct 依旧没有元素时， align 才为 0
int64_t type_struct_sizeof(type_struct_t *s) {
    int64_t size = 0;
    int64_t max_align = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        int64_t element_size = type_sizeof(p->type);
        int64_t element_align = type_alignof(p->type);
        if (element_align > max_align) {
            max_align = element_align;
        }

        size = align_up(size, element_align);
        size += element_size;
    }

    // struct 整体按照 max_align 对齐
    size = align_up(size, max_align);

    return size;
}

static int64_t type_tuple_sizeof(type_tuple_t *t) {
    int64_t size = 0;
    int64_t max_align = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *element = ct_list_value(t->elements, i);
        int64_t element_size = element->storage_size;
        int64_t element_align = element->align;
        if (element_align > max_align) {
            max_align = element_align;
        }
        size = align_up(size, element_align);
        size += element_size;
    }

    // tuple 整体按照 max_align 对齐
    size = align_up(size, max_align);
    return size;
}

static int64_t type_tuple_alignof(type_tuple_t *t) {
    int64_t max_align = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *element = ct_list_value(t->elements, i);
        if (element->align > max_align) {
            max_align = element->align;
        }
    }
    return max_align;
}

/**
 * @param t
 * @return
 */
int64_t type_sizeof(type_t t) {
    if (t.kind == TYPE_IDENT) {
        //        assert(false);
        return POINTER_SIZE;
    }

    if (t.kind == TYPE_STRUCT) {
        int64_t size = type_struct_sizeof(t.struct_);
        return size;
    }

    if (t.kind == TYPE_TUPLE) {
        return type_tuple_sizeof(t.tuple);
    }

    if (t.kind == TYPE_ARR) {
        int64_t element_size = type_sizeof(t.array->element_type);
        return t.array->length * element_size;
    }

    if (t.kind == TYPE_ENUM) {
        return type_sizeof(t.enum_->element_type);
    }

    if (t.kind == TYPE_ANY) {
        return sizeof(n_any_t);
    }

    if (t.kind == TYPE_UNION) {
        int64_t max_size = 0;
        if (t.union_ && t.union_->elements && t.union_->elements->length > 0) {
            for (int i = 0; i < t.union_->elements->length; ++i) {
                type_t *element = ct_list_value(t.union_->elements, i);
                int64_t element_size = element->storage_size;
                if (element_size > max_size) {
                    max_size = element_size;
                }
            }
        } else {
            // any/empty union defaults to pointer-sized payload
            max_size = POINTER_SIZE;
        }

        return POINTER_SIZE + align_up(max_size, POINTER_SIZE);
    }

    if (t.kind == TYPE_TAGGED_UNION) {
        int64_t max_size = 0;
        if (t.tagged_union && t.tagged_union->elements && t.tagged_union->elements->length > 0) {
            for (int i = 0; i < t.tagged_union->elements->length; ++i) {
                tagged_union_element_t *element = ct_list_value(t.tagged_union->elements, i);
                int64_t element_size = element->type.storage_size;
                if (element_size > max_size) {
                    max_size = element_size;
                }
            }
        }

        if (max_size <= 0) {
            max_size = POINTER_SIZE;
        }

        return POINTER_SIZE + align_up(max_size, POINTER_SIZE);
    }

    if (t.kind == TYPE_STRING) {
        return sizeof(n_string_t);
    }

    if (t.kind == TYPE_VEC) {
        return sizeof(n_vec_t);
    }

    if (t.kind == TYPE_MAP) {
        return sizeof(n_map_t);
    }

    if (t.kind == TYPE_SET) {
        return sizeof(n_set_t);
    }

    if (t.storage_kind == STORAGE_KIND_PTR) {
        return POINTER_SIZE;
    }

    return type_kind_sizeof(t.kind);
}

bool type_can_size(type_t t) {
    if (t.kind == TYPE_IDENT) {
        return false;
    }

    if (t.kind == TYPE_TUPLE) {
        for (int i = 0; i < t.tuple->elements->length; ++i) {
            type_t *item = ct_list_value(t.tuple->elements, i);
            if (!type_can_size(*item)) {
                return false;
            }
        }

        return true;
    }

    if (t.kind == TYPE_STRUCT) {
        for (int i = 0; i < t.struct_->properties->length; ++i) {
            struct_property_t *p = ct_list_value(t.struct_->properties, i);
            if (!type_can_size(p->type)) {
                return false;
            }
        }

        return true;
    }

    if (t.kind == TYPE_ARR) {
        return type_can_size(t.array->element_type);
    }

    return true;
}

int64_t type_alignof(type_t t) {
    if (t.kind == TYPE_IDENT) {
        //        assert(false);
        return POINTER_SIZE;
    }

    if (t.kind == TYPE_STRING || t.kind == TYPE_VEC || t.kind == TYPE_MAP || t.kind == TYPE_SET) {
        return POINTER_SIZE;
    }

    if (t.kind == TYPE_STRUCT) {
        return type_struct_alignof(t.struct_);
    }

    if (t.kind == TYPE_TUPLE) {
        return type_tuple_alignof(t.tuple);
    }

    if (t.kind == TYPE_ARR) {
        return type_alignof(t.array->element_type);
    }

    if (t.kind == TYPE_ENUM) {
        assert(t.enum_->element_type.storage_size > 0);
        return t.enum_->element_type.storage_size;
    }

    if (t.kind == TYPE_ANY) {
        return POINTER_SIZE;
    }

    if (t.kind == TYPE_UNION) {
        return POINTER_SIZE;
    }

    if (t.kind == TYPE_TAGGED_UNION) {
        return POINTER_SIZE;
    }

    return t.storage_size;
}

/**
 * ptr 自动机场
 * @param t
 * @return
 */
type_t type_refof(type_t t) {
    type_t result = {0};
    result.status = t.status;

    result.kind = TYPE_REF;
    result.ptr = NEW(type_ptr_t);
    result.ptr->value_type = t;
    //    result.ident = t.ident;
    //    result.ident_kind = t.ident_kind;
    //    result.args = t.args;
    result.line = t.line;
    result.column = t.column;
    result.in_heap = false;
    result.storage_size = POINTER_SIZE;
    result.storage_kind = STORAGE_KIND_PTR;
    result.map_imm_kind = TYPE_ANYPTR;
    result.align = QWORD;
    return result;
}

type_t type_ptrof(type_t t) {
    type_t result = {0};
    result.status = t.status;

    result.kind = TYPE_PTR;
    result.ptr = NEW(type_ptr_t);
    result.ptr->value_type = t;
    result.line = t.line;
    result.column = t.column;
    result.in_heap = false;
    result.storage_size = POINTER_SIZE;
    result.storage_kind = STORAGE_KIND_PTR;
    result.map_imm_kind = TYPE_ANYPTR;
    result.align = QWORD;
    return result;
}


uint64_t calc_gc_bits_size(uint64_t size, uint8_t ptr_size) {
    size = align_up(size, ptr_size);

    uint64_t gc_bits_size = size / ptr_size;

    // 8bit = 1byte, 再次对齐到 1byte
    gc_bits_size = align_up(gc_bits_size, ptr_size);

    return gc_bits_size;
}

uint8_t *malloc_gc_bits(uint64_t size) {
    uint64_t gc_bits_size = calc_gc_bits_size(size, POINTER_SIZE);
    return mallocz(gc_bits_size);
}

type_param_t *type_param_new(char *literal) {
    type_param_t *t = NEW(type_param_t);
    t->ident = literal;
    return t;
}

type_alias_t *type_alias_new(char *literal, char *import_module_ident) {
    type_alias_t *t = NEW(type_alias_t);
    t->ident = literal;
    t->import_as = import_module_ident;
    t->args = NULL;
    return t;
}

/**
 * 当前类型中存储的数据是否指向堆中, 如果是
 * @param t
 * @return
 */
bool type_is_pointer_heap(type_t t) {
    if (t.in_heap) {
        return true;
    }

    if (t.kind == TYPE_PTR || t.kind == TYPE_REF || t.kind == TYPE_ANYPTR) {
        return true;
    }

    return false;
}

rtype_t *rtype_push(rtype_t rtype) {
    uint64_t index = ct_rtype_list->length;
    ct_list_push(ct_rtype_list, &rtype);

    ct_rtype_size += sizeof(rtype_t);
    ct_rtype_count += 1;

    return ct_list_value(ct_rtype_list, index);
}

/**
 * 需要按 key 对齐
 * @param type
 * @param key
 * @return
 */
uint64_t type_struct_offset(type_struct_t *s, char *key) {
    uint64_t offset = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        int64_t field_size = p->type.storage_size;
        int64_t field_align = p->type.align;

        offset = align_up(offset, field_align);
        if (str_equal(p->name, key)) {
            // found
            return offset;
        }

        offset += field_size;
    }

    assertf(false, "key=%s not found in struct", key);
    return 0;
}

struct_property_t *type_struct_property(type_struct_t *s, char *key) {
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);
        if (str_equal(p->name, key)) {
            return p;
        }
    }
    return NULL;
}

int64_t type_tuple_offset(type_tuple_t *t, uint64_t index) {
    uint64_t offset = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *element = ct_list_value(t->elements, i);

        int64_t element_size = element->storage_size;
        int64_t element_align = element->align;

        offset = align_up(offset, element_align);

        if (i == index) {
            // found
            return offset;
        }
        offset += element_size;
    }

    return 0;
}

char *_type_format(type_t t) {
    if (t.kind == TYPE_VEC) {
        // []
        return dsprintf("[%s]", type_format(t.vec->element_type));
    }
    if (t.kind == TYPE_CHAN) {
        return dsprintf("chan<%s>", type_format(t.chan->element_type));
    }
    if (t.kind == TYPE_ARR) {
        return dsprintf("[%s;%d]", type_format(t.array->element_type), t.array->length);
    }
    if (t.kind == TYPE_MAP) {
        return dsprintf("map<%s,%s>", type_format(t.map->key_type), _type_format(t.map->value_type));
    }

    if (t.kind == TYPE_SET) {
        return dsprintf("set<%s>", type_format(t.set->element_type));
    }

    if (t.kind == TYPE_TUPLE) {
        // while
        //        char *str = "tup<";
        //        list_t *elements = t.tuple->elements; // type_t
        //        for (int i = 0; i < elements->length; ++i) {
        //            type_t *item = ct_list_value(elements, i);
        //            char *item_str = _type_format(*item);
        //            str = str_connect_by(str, item_str, ",");
        //        }
        return dsprintf("tup<...>");
    }

    if (t.kind == TYPE_FN) {
        if (t.fn) {
            if (t.fn->is_errable) {
                return dsprintf("fn(...):%s!", type_format(t.fn->return_type));
            } else {
                return dsprintf("fn(...):%s", type_format(t.fn->return_type));
            }
        } else {
            return "fn";
        }
    }

    if (t.kind == TYPE_REF) {
        return dsprintf("ref<%s>", type_format(t.ptr->value_type));
    }

    if (t.kind == TYPE_PTR) {
        return dsprintf("ptr<%s>", type_format(t.ptr->value_type));
    }

    if (t.kind == TYPE_ANY) {
        return "any";
    }


    if (t.kind == TYPE_INTERFACE) {
        return "interface";
    }

    if (t.kind == TYPE_ENUM) {
        return dsprintf("enum:%s", type_format(t.enum_->element_type));
    }

    return type_kind_str[t.kind];
}

/**
 * @param t
 * @return
 */
char *type_format(type_t t) {
    char *ident = NULL;
    if (t.ident_kind != TYPE_IDENT_BUILTIN) {
        ident = t.ident;
    }

    if (t.ident && (str_equal(t.ident, "int") || str_equal(t.ident, "uint") || str_equal(t.ident, "float"))) {
        ident = t.ident;
    }

    if (ident == NULL) {
        return _type_format(t);
    }

    if (ident_is_generics_param(&t)) {
        return ident;
    }

    if (t.args && t.args->length > 0) {
        char *args_str = "";
        for (int i = 0; i < t.args->length; ++i) {
            type_t *arg = ct_list_value(t.args, i);
            char *arg_str = type_origin_format(*arg);
            if (i == 0) {
                args_str = arg_str;
            } else {
                args_str = str_connect3(args_str, ",", arg_str);
            }
        }
        return dsprintf("%s<%s>(%s)", ident, args_str, _type_format(t));
    }

    return dsprintf("%s(%s)", ident, _type_format(t));
}

char *type_origin_format(type_t t) {
    char *ident = t.ident;
    if (ident == NULL) {
        return _type_format(t);
    }

    if (ident_is_generics_param(&t)) {
        return ident;
    }

    return ident;
}

type_t type_kind_new(type_kind kind) {
    type_t result = {
            .status = REDUCTION_STATUS_DONE,
            .kind = kind,
            .value = 0,
            .ident = NULL,
            .ident_kind = 0,
    };

    // 不能直接填充 ident, 比如 vec ident 必须要有 args, 但是此时无法计算 args
    if (is_impl_builtin_type(kind)) {
        result.ident = type_kind_str[kind];
        result.ident_kind = TYPE_IDENT_BUILTIN;
    }

    result.in_heap = kind_in_heap(kind);
    result.storage_kind = type_storage_kind(result);
    result.storage_size = type_storage_size(result);
    result.map_imm_kind = type_map_imm_kind(result);
    result.align = type_alignof(result);

    return result;
}

type_t type_new(type_kind kind, void *value) {
    type_t result = {
            .kind = kind,
            .value = value,
            .in_heap = kind_in_heap(kind),
            .status = REDUCTION_STATUS_DONE,
            .ident = NULL,
            .ident_kind = 0,
    };

    if (is_impl_builtin_type(kind)) {
        result.ident = type_kind_str[kind];
        result.ident_kind = TYPE_IDENT_BUILTIN;
    }

    result.storage_kind = type_storage_kind(result);
    result.storage_size = type_storage_size(result);
    result.map_imm_kind = type_map_imm_kind(result);
    result.align = type_alignof(result);
    return result;
}

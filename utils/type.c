#include "type.h"

#include "assertf.h"
#include "bitmap.h"
#include "ct_list.h"
#include "custom_links.h"
#include "helper.h"

int64_t type_kind_sizeof(type_kind t) {
    assert(t > 0);

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
            return QWORD;
        default:
            return POINTER_SIZE;
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

/**
 * @param t
 * @return
 */
int64_t type_sizeof(type_t t) {
    if (t.kind == TYPE_IDENT) {
        assert(false);
    }

    if (t.kind == TYPE_STRUCT) {
        int64_t size = type_struct_sizeof(t.struct_);
        return size;
    }

    if (t.kind == TYPE_ARR) {
        int64_t element_size = type_sizeof(t.array->element_type);
        return t.array->length * element_size;
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
    assert(t.kind != TYPE_IDENT);

    if (t.kind == TYPE_STRUCT) {
        return type_struct_alignof(t.struct_);
    }

    if (t.kind == TYPE_ARR) {
        return type_alignof(t.array->element_type);
    }

    return type_kind_sizeof(t.kind);
}

/**
 * ptr 自动机场
 * @param t
 * @return
 */
type_t type_ptrof(type_t t) {
    type_t result = {0};
    result.status = t.status;

    result.kind = TYPE_PTR;
    result.ptr = NEW(type_ptr_t);
    result.ptr->value_type = t;
    //    result.ident = t.ident;
    //    result.ident_kind = t.ident_kind;
    //    result.args = t.args;
    result.line = t.line;
    result.column = t.column;
    result.in_heap = false;
    return result;
}

type_t type_rawptrof(type_t t) {
    type_t result = {0};
    result.status = t.status;

    result.kind = TYPE_RAWPTR;
    result.ptr = NEW(type_ptr_t);
    result.ptr->value_type = t;
    //    result.ident = t.ident;
    //    result.ident_kind = t.ident_kind;
    //    result.args = t.args;
    result.line = t.line;
    result.column = t.column;
    result.in_heap = false;
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

    if (t.kind == TYPE_RAWPTR || t.kind == TYPE_PTR || t.kind == TYPE_ANYPTR) {
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
 * rtype 在堆外占用的空间大小,比如 stack,global,list value, struct value 中的占用的 size 的大小
 * 如果类型没有存储在堆中，则其在堆外占用的大小是就是类型本身的大小，如果类型存储在堆中，其在堆外存储的是指向堆的指针
 * 占用 POINTER_SIZE 大小
 * @param rtype
 * @return
 */
int64_t rtype_stack_size(rtype_t *rtype, uint8_t ptr_size) {
    assert(rtype && "rtype is null");

    // 应对 vec/map/chan/string 等存储在堆中的元素，其在 stack 上占用一个指针大小
    if (kind_in_heap(rtype->kind)) {
        return ptr_size;
    }

    return rtype->size;
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

        int64_t field_size = type_sizeof(p->type);
        int64_t field_align = type_alignof(p->type);

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
        type_t *typedecl = ct_list_value(t->elements, i);

        int element_size = type_sizeof(*typedecl);
        int element_align = type_alignof(*typedecl);

        offset = align_up(offset, element_align);

        if (i == index) {
            // found
            return offset;
        }
        offset += element_size;
    }

    return 0;
}

type_kind to_gc_kind(type_kind kind) {
    assert(kind > 0);
    if (kind_in_heap(kind)) {
        return TYPE_GC_SCAN;
    }

    return TYPE_GC_NOSCAN;
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

    if (t.kind == TYPE_PTR) {
        return dsprintf("ptr<%s>", type_format(t.ptr->value_type));
    }

    if (t.kind == TYPE_RAWPTR) {
        return dsprintf("rawptr<%s>", type_format(t.ptr->value_type));
    }

    if (t.kind == TYPE_UNION) {
        if (t.union_->any) {
            return "any";
        }
    }

    if (t.kind == TYPE_INTERFACE) {
        return "interface";
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

    // 特殊 t.ident 处理
    if (t.ident && (str_equal(t.ident, "int") || str_equal(t.ident, "uint") || str_equal(t.ident, "float"))) {
        ident = t.ident;
    }

    if (ident == NULL) {
        return _type_format(t);
    }

    if (ident_is_generics_param(&t)) {
        return ident;
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
    return result;
}

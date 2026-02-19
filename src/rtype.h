#ifndef NATURE_SRC_RTYPE_H_
#define NATURE_SRC_RTYPE_H_

#include "src/symbol/symbol.h"
#include "utils/custom_links.h"
#include "utils/type.h"

static inline int64_t rtype_builtin_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_t t);

static inline int64_t rtype_array_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_array_t *t);

static int64_t rtype_struct_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_struct_t *t);

static inline int64_t rtype_tuple_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_tuple_t *t);

static inline int64_t rtype_union_gc_bits(int64_t gc_bits_offset, int64_t *offset, int64_t union_size);

static inline int64_t rtype_tagged_union_gc_bits(int64_t gc_bits_offset, int64_t *offset, int64_t tagged_union_size);

static inline rtype_t reflect_type(type_t t);

static inline int64_t type_hash(type_t t) {
    if (t.ident_kind == TYPE_IDENT_DEF || t.ident_kind == TYPE_IDENT_INTERFACE || t.ident_kind == TYPE_IDENT_UNKNOWN) { // 存在 ident 优先基于 ident 计算 hash, 而不是递归解析
        assert(t.ident);
        // TODO param? hash TODO TODO
        if (t.args == NULL || t.args->length == 0) {
            return hash_string(t.ident);
        }

        // t.ident.param1.param2
        char *str = t.ident;
        for (int i = 0; i < t.args->length; ++i) {
            type_t *arg = ct_list_value(t.args, i);
            str = dsprintf("%s.%ld", str, type_hash(*arg));
        }

        return hash_string(str);
    }

    if (is_origin_type(t)) {
        return hash_string(type_kind_str[t.kind]);
    }

    if (t.kind == TYPE_REF) {
        char *str = dsprintf("ref.%ld", type_hash(t.ptr->value_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_PTR) {
        char *str = dsprintf("ptr.%ld", type_hash(t.ptr->value_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_VEC) {
        char *str = dsprintf("vec.%ld", type_hash(t.vec->element_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_CHAN) {
        char *str = dsprintf("chan.%ld", type_hash(t.chan->element_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_ARR) {
        char *str = dsprintf("arr.%ld_%ld", t.array->length, type_hash(t.array->element_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_MAP) {
        char *str = dsprintf("map.%ld_%ld", type_hash(t.map->key_type), type_hash(t.map->value_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_SET) {
        char *str = dsprintf("set.%ld", type_hash(t.set->element_type));
        return hash_string(str);
    }

    if (t.kind == TYPE_TUPLE) {
        char *str = dsprintf("tuple");
        for (int i = 0; i < t.tuple->elements->length; ++i) {
            type_t *element_type = ct_list_value(t.tuple->elements, i);
            str = str_connect(str, dsprintf(".%ld", type_hash(*element_type)));
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_STRUCT) {
        char *str = dsprintf("struct");
        if (t.ident) {
            str = str_connect(str, t.ident);
        } else {
            for (int i = 0; i < t.struct_->properties->length; ++i) {
                struct_property_t *p = ct_list_value(t.struct_->properties, i);
                str = str_connect(str, dsprintf("%s.%ld", p->name, type_hash(p->type)));
            }
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_FN) {
        char *str = dsprintf("fn.%ld", type_hash(t.fn->return_type));
        for (int i = 0; i < t.fn->param_types->length; ++i) {
            type_t *param_type = ct_list_value(t.fn->param_types, i);
            str = str_connect(str, dsprintf(".%ld", type_hash(*param_type)));
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_UNION) {
        char *str = dsprintf("union");
        for (int i = 0; i < t.union_->elements->length; ++i) {
            type_t *element_type = ct_list_value(t.union_->elements, i);
            str = str_connect(str, dsprintf(".%ld", type_hash(*element_type)));
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_TAGGED_UNION) {
        char *str = dsprintf("tagged_union");
        for (int i = 0; i < t.tagged_union->elements->length; ++i) {
            tagged_union_element_t *element = ct_list_value(t.tagged_union->elements, i);
            str = str_connect(str, dsprintf(".%s.%ld", element->tag, type_hash(element->type)));
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_INTERFACE) { // ident right
        char *str = dsprintf("interface");
        if (t.ident) {
            str = str_connect(str, t.ident);
        }
        return hash_string(str);
    }

    if (t.kind == TYPE_STRING) {
        return hash_string("string");
    }

    if (t.kind == TYPE_ANYPTR) {
        return hash_string("anyptr");
    }

    // 默认情况，返回基于类型种类的哈希
    return hash_string(type_kind_str[t.kind]);
}

static inline rtype_t rtype_origin(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = t.storage_size, // 单位 byte
            .hash = type_hash(t),
            .last_ptr = 0,
            .kind = t.kind,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    if (rtype.gc_heap_size > 0) {
        rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    }

    return rtype;
}

static inline rtype_t rtype_ptr(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_ptr_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_PTR,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.ptr->value_type);

    return rtype;
}

static inline rtype_t rtype_anyptr(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_ptr_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_ANYPTR,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits, 在当前的设计中，anyptr 同样会被 gc 追踪
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}


/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static inline rtype_t rtype_ref(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_ptr_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_REF,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.ptr->value_type);
    return rtype;
}

/**
 * hash = type_kind
 * @param t
 * @return
 */
static inline rtype_t rtype_string(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_string_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_STRING,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}

/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static inline rtype_t rtype_vec(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_vec_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_VEC,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.vec->element_type);

    return rtype;
}

static inline rtype_t rtype_chan(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_chan_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE * 5,
            .kind = TYPE_CHAN,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));

    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 2);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 3);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 4);

    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.chan->element_type);

    return rtype;
}

/**
 * hash = type_kind + count + element_type_hash
 * @param t
 * @return
 */
static inline rtype_t rtype_array(type_t t) {
    int64_t element_size = t.array->element_type.storage_size;

    rtype_t rtype = {
            .gc_heap_size = element_size * t.array->length,
            .hash = type_hash(t),
            .kind = TYPE_ARR,
            .length = t.array->length, // array length 特殊占用
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };

    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));

    int64_t offset = 0;
    rtype.last_ptr = rtype_array_gc_bits(rtype.malloc_gc_bits_offset, &offset, t.array);

    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.array->element_type);

    return rtype;
}

/**
 * hash = type_kind + key_rtype.hash + value_rtype.hash
 * @param t
 * @return
 */
static inline rtype_t rtype_map(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_map_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE * 3, // hash_table + key_data + value_data
            .kind = TYPE_MAP,
            .length = 2,
            .hashes_offset = data_put(NULL, sizeof(int64_t) * 2),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0); // hash_table
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1); // key_data
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 2); // value_data

    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.map->key_type);
    ((int64_t *) CTDATA(rtype.hashes_offset))[1] = type_hash(t.map->value_type);

    return rtype;
}

/**
 * @param t
 * @return
 */
static inline rtype_t rtype_set(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = sizeof(n_set_t),
            .hash = type_hash(t),
            .last_ptr = POINTER_SIZE * 2, // hash_table + key_data
            .kind = TYPE_SET,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(int64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0); // hash_table
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1); // key_data

    ((int64_t *) CTDATA(rtype.hashes_offset))[0] = type_hash(t.set->element_type);

    return rtype;
}

static inline rtype_t rtype_interface(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = POINTER_SIZE * 4, // element_rtype + value(并不知道 value 的类型)
            .hash = type_hash(t),
            .kind = TYPE_INTERFACE,
            .last_ptr = POINTER_SIZE * 2,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(POINTER_SIZE * 2, POINTER_SIZE)),
            .hashes_offset = -1,
    };

    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1);

    return rtype;
}

static inline rtype_t rtype_tagged_union(type_t t) {
    int64_t tagged_union_size = t.storage_size;
    assert(tagged_union_size > 0);
    int64_t slot_count = align_up(tagged_union_size, POINTER_SIZE) / POINTER_SIZE;

    rtype_t rtype = {
            .gc_heap_size = tagged_union_size,
            .hash = type_hash(t),
            .kind = TYPE_TAGGED_UNION,
            .last_ptr = slot_count * POINTER_SIZE,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(tagged_union_size, POINTER_SIZE)),
            .length = t.tagged_union->elements->length,
            .hashes_offset = -1,
    };

    for (int i = 1; i < slot_count; ++i) {
        bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), i);
    }
    if (t.tagged_union->elements->length > 0) {
        int64_t size = sizeof(int64_t) * t.tagged_union->elements->length;
        int64_t *hashes = mallocz(size);
        for (int i = 0; i < t.tagged_union->elements->length; ++i) {
            tagged_union_element_t *element = ct_list_value(t.tagged_union->elements, i);
            hashes[i] = type_hash(element->type);
        }
        rtype.hashes_offset = data_put((uint8_t *) hashes, size);
    }

    return rtype;
}

/**
 * 从类型声明上无法看出 union 第二个值是否需要 GC mark, 所以总是设置需要 GC
 * hash = type_kind
 * @param t
 * @return
 */
static inline rtype_t rtype_union(type_t t) {
    int64_t union_size = t.storage_size;
    assert(union_size > 0);
    int64_t slot_count = align_up(union_size, POINTER_SIZE) / POINTER_SIZE;

    rtype_t rtype = {
            .gc_heap_size = union_size, // rtype + payload
            .hash = type_hash(t),
            .kind = TYPE_UNION,
            .last_ptr = slot_count * POINTER_SIZE,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(union_size, POINTER_SIZE)),
            .length = t.union_->elements->length,
            .hashes_offset = -1,
    };

    for (int i = 0; i < slot_count; ++i) {
        bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), i);
    }
    if (t.union_->elements->length > 0) {
        int64_t size = sizeof(int64_t) * t.union_->elements->length;
        int64_t *hashes = mallocz(size);
        for (int i = 0; i < t.union_->elements->length; ++i) {
            type_t *element_type = ct_list_value(t.union_->elements, i);
            hashes[i] = type_hash(*element_type);
        }
        rtype.hashes_offset = data_put((uint8_t *) hashes, size);
    }

    return rtype;
}

/**
 * any: { rtype_t *rtype; value_casting value; }
 * value runtime type not fixed, conservatively scan all slots
 */
static inline rtype_t rtype_any(type_t t) {
    int64_t any_size = t.storage_size;
    assert(any_size > 0);

    rtype_t rtype = {
            .gc_heap_size = any_size,
            .hash = type_hash(t),
            .kind = TYPE_ANY,
            .last_ptr = POINTER_SIZE * 2,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(POINTER_SIZE * 2, POINTER_SIZE)),
            .hashes_offset = -1,
            .length = 0,
    };

    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}

/**
 * 无法分片 fn 是否是 closure, 所以统一都进行扫描, runtime 可以根据 fn 的地址判断是否需要进一步扫描
 * hash = type_kind + params hash + return_type hash
 * @param t
 * @return
 */
static inline rtype_t rtype_fn(type_t t) {
    rtype_t rtype = {
            .gc_heap_size = POINTER_SIZE * 2,
            .last_ptr = POINTER_SIZE,
            .hash = type_hash(t),
            .kind = TYPE_FN,
            .malloc_gc_bits_offset = -1,
            .hashes_offset = -1,
    };

    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.gc_heap_size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}

static inline int64_t rtype_array_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_array_t *t) {
    // offset 已经按照 align 对齐过了，这里不需要重复对齐
    int64_t last_ptr_offset = 0;

    for (int i = 0; i < t->length; ++i) {
        int64_t last_ptr_temp_offset = 0;
        if (t->element_type.kind == TYPE_STRUCT) {
            last_ptr_temp_offset = rtype_struct_gc_bits(gc_bits_offset, offset, t->element_type.struct_);
        } else if (t->element_type.kind == TYPE_ARR) {
            last_ptr_temp_offset = rtype_array_gc_bits(gc_bits_offset, offset, t->element_type.array);
        } else if (t->element_type.kind == TYPE_TUPLE) {
            last_ptr_temp_offset = rtype_tuple_gc_bits(gc_bits_offset, offset, t->element_type.tuple);
        } else if (t->element_type.kind == TYPE_UNION || t->element_type.kind == TYPE_ANY) {
            last_ptr_temp_offset = rtype_union_gc_bits(gc_bits_offset, offset, t->element_type.storage_size);
        } else if (t->element_type.kind == TYPE_TAGGED_UNION) {
            last_ptr_temp_offset = rtype_tagged_union_gc_bits(gc_bits_offset, offset, t->element_type.storage_size);
        } else if (t->element_type.kind == TYPE_STRING || t->element_type.kind == TYPE_VEC || t->element_type.kind == TYPE_MAP ||
                   t->element_type.kind == TYPE_SET) {
            last_ptr_temp_offset = rtype_builtin_gc_bits(gc_bits_offset, offset, t->element_type);
        } else {
            int64_t bit_index = *offset / POINTER_SIZE;
            if (type_is_pointer_heap(t->element_type)) {
                bitmap_set(CTDATA(gc_bits_offset), bit_index);
            }

            *offset += t->element_type.storage_size;
            last_ptr_temp_offset = *offset;
        }

        if (last_ptr_temp_offset > last_ptr_offset) {
            last_ptr_offset = last_ptr_temp_offset;
        }
    }

    return last_ptr_offset;
}

static inline void *type_recycle_check(module_t *m, type_t *t, struct sc_map_s64 *visited) {
    if (t->ident) {
        // check ident
        if (sc_map_get_s64(visited, t->ident)) { // recycle
            return t->ident;
        }

        sc_map_put_s64(visited, t->ident, 1);
    }

    if (t->kind == TYPE_STRUCT) {
        type_struct_t *s = t->struct_;
        for (int i = 0; i < s->properties->length; ++i) {
            struct_property_t *p = ct_list_value(s->properties, i);

            void *temp = type_recycle_check(m, &p->type, visited);
            if (temp != NULL) {
                return temp;
            }
        }
    } else if (t->kind == TYPE_ARR) {
        return type_recycle_check(m, &t->array->element_type, visited);
    } else if (t->kind == TYPE_IDENT && t->ident_kind != TYPE_IDENT_GENERICS_PARAM) {
        symbol_t *symbol = symbol_table_get(t->ident);
        assert(symbol);
        ast_typedef_stmt_t *typedef_stmt = symbol->ast_value;

        bool stack_pushed = false;
        if (t->args && t->args->length > 0) {
            table_t *args_table = table_new();
            for (int i = 0; i < t->args->length; ++i) {
                type_t *arg = ct_list_value(t->args, i);
                ast_generics_param_t *param = ct_list_value(typedef_stmt->params, i);
                table_set(args_table, param->ident, arg);
            }
            if (m->infer_type_args_stack) {
                stack_push(m->infer_type_args_stack, args_table);
                stack_pushed = true;
            }
        }


        void *temp = type_recycle_check(m, &typedef_stmt->type_expr, visited);
        if (temp != NULL) {
            return temp;
        }

        if (stack_pushed && m->infer_type_args_stack) {
            stack_pop(m->infer_type_args_stack);
        }
    }

    if (t->ident) {
        // remove
        sc_map_del_s64(visited, t->ident);
    }

    return NULL;
}

// union: { rtype_t *rtype; value_casting value; }
// union value 运行时类型不固定，保守地将 payload 区域按指针槽扫描
static inline int64_t rtype_union_gc_bits(int64_t gc_bits_offset, int64_t *offset, int64_t union_size) {
    if (union_size <= 0) {
        union_size = POINTER_SIZE * 2;
    }

    int64_t slot_count = align_up(union_size, POINTER_SIZE) / POINTER_SIZE;
    int64_t bit_index = *offset / POINTER_SIZE;
    for (int i = 0; i < slot_count; ++i) {
        bitmap_set(CTDATA(gc_bits_offset), bit_index + i);
    }

    int64_t last_ptr_offset = *offset + slot_count * POINTER_SIZE;
    *offset += union_size;

    return last_ptr_offset;
}

// tagged_union: { int64_t tag_hash; value_casting value; }
// tag_hash 不需要 gc，value 运行时类型不固定，保守地扫描 payload 所有槽位
static inline int64_t rtype_tagged_union_gc_bits(int64_t gc_bits_offset, int64_t *offset, int64_t tagged_union_size) {
    if (tagged_union_size <= 0) {
        tagged_union_size = POINTER_SIZE * 2;
    }

    int64_t slot_count = align_up(tagged_union_size, POINTER_SIZE) / POINTER_SIZE;
    int64_t bit_index = *offset / POINTER_SIZE;
    for (int i = 1; i < slot_count; ++i) {
        bitmap_set(CTDATA(gc_bits_offset), bit_index + i);
    }

    int64_t last_ptr_offset = 0;
    if (slot_count > 1) {
        last_ptr_offset = *offset + slot_count * POINTER_SIZE;
    }

    *offset += tagged_union_size;

    return last_ptr_offset;
}

static inline int64_t rtype_builtin_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_t t) {
    int64_t ptr_slots = 0;
    if (t.kind == TYPE_STRING || t.kind == TYPE_VEC) {
        ptr_slots = 1;
    } else if (t.kind == TYPE_MAP) {
        ptr_slots = 3;
    } else if (t.kind == TYPE_SET) {
        ptr_slots = 2;
    } else {
        return 0;
    }

    int64_t bit_index = *offset / POINTER_SIZE;
    for (int i = 0; i < ptr_slots; ++i) {
        bitmap_set(CTDATA(gc_bits_offset), bit_index + i);
    }

    int64_t last_ptr_offset = 0;
    if (ptr_slots > 0) {
        last_ptr_offset = *offset + ptr_slots * POINTER_SIZE;
    }

    *offset += t.storage_size;
    return last_ptr_offset;
}


static inline int64_t rtype_struct_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_struct_t *t) {
    // offset 已经按照 align 对齐过了，这里不需要重复对齐
    int64_t last_ptr_offset = 0;
    for (int i = 0; i < t->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t->properties, i);

        // 属性基础地址对齐
        *offset = align_up(*offset, p->type.align);

        int64_t last_ptr_temp_offset = 0;
        if (p->type.kind == TYPE_STRUCT) {
            last_ptr_temp_offset = rtype_struct_gc_bits(gc_bits_offset, offset, p->type.struct_);
        } else if (p->type.kind == TYPE_ARR) {
            last_ptr_temp_offset = rtype_array_gc_bits(gc_bits_offset, offset, p->type.array);
        } else if (p->type.kind == TYPE_TUPLE) {
            last_ptr_temp_offset = rtype_tuple_gc_bits(gc_bits_offset, offset, p->type.tuple);
        } else if (p->type.kind == TYPE_UNION || p->type.kind == TYPE_ANY) {
            last_ptr_temp_offset = rtype_union_gc_bits(gc_bits_offset, offset, p->type.storage_size);
        } else if (p->type.kind == TYPE_TAGGED_UNION) {
            last_ptr_temp_offset = rtype_tagged_union_gc_bits(gc_bits_offset, offset, p->type.storage_size);
        } else if (p->type.kind == TYPE_STRING || p->type.kind == TYPE_VEC || p->type.kind == TYPE_MAP || p->type.kind == TYPE_SET) {
            last_ptr_temp_offset = rtype_builtin_gc_bits(gc_bits_offset, offset, p->type);
        } else {
            int64_t size = p->type.storage_size; // 等待存储的 struct size
            // 这里就是存储位置
            int64_t bit_index = *offset / POINTER_SIZE;

            *offset += size;
            bool is_ptr = type_is_pointer_heap(p->type);
            if (is_ptr) {
                bitmap_set(CTDATA(gc_bits_offset), bit_index);
                last_ptr_temp_offset = *offset;
            }
        }

        if (last_ptr_temp_offset > last_ptr_offset) {
            last_ptr_offset = last_ptr_temp_offset;
        }
    }

    // 结构体需要整体需要对齐到 align
    *offset = align_up(*offset, type_struct_alignof(t));

    return last_ptr_offset;
}

static inline int64_t rtype_tuple_gc_bits(int64_t gc_bits_offset, int64_t *offset, type_tuple_t *t) {
    int64_t last_ptr_offset = 0;
    int64_t max_align = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *element = ct_list_value(t->elements, i);

        int64_t element_align = element->align;
        if (element_align > max_align) {
            max_align = element_align;
        }

        *offset = align_up(*offset, element_align);

        int64_t last_ptr_temp_offset = 0;
        if (element->kind == TYPE_STRUCT) {
            last_ptr_temp_offset = rtype_struct_gc_bits(gc_bits_offset, offset, element->struct_);
        } else if (element->kind == TYPE_ARR) {
            last_ptr_temp_offset = rtype_array_gc_bits(gc_bits_offset, offset, element->array);
        } else if (element->kind == TYPE_TUPLE) {
            last_ptr_temp_offset = rtype_tuple_gc_bits(gc_bits_offset, offset, element->tuple);
        } else if (element->kind == TYPE_UNION || element->kind == TYPE_ANY) {
            last_ptr_temp_offset = rtype_union_gc_bits(gc_bits_offset, offset, element->storage_size);
        } else if (element->kind == TYPE_TAGGED_UNION) {
            last_ptr_temp_offset = rtype_tagged_union_gc_bits(gc_bits_offset, offset, element->storage_size);
        } else if (element->kind == TYPE_STRING || element->kind == TYPE_VEC || element->kind == TYPE_MAP || element->kind == TYPE_SET) {
            last_ptr_temp_offset = rtype_builtin_gc_bits(gc_bits_offset, offset, *element);
        } else {
            int64_t size = element->storage_size;
            int64_t bit_index = *offset / POINTER_SIZE;

            *offset += size;
            bool is_ptr = type_is_pointer_heap(*element);
            if (is_ptr) {
                bitmap_set(CTDATA(gc_bits_offset), bit_index);
                last_ptr_temp_offset = *offset;
            }
        }

        if (last_ptr_temp_offset > last_ptr_offset) {
            last_ptr_offset = last_ptr_temp_offset;
        }
    }

    *offset = align_up(*offset, max_align);
    return last_ptr_offset;
}

/**
 * hash = type_kind + key type hash
 * @param t
 * @return
 */
static inline rtype_t rtype_struct(type_t t) {
    int64_t size = t.storage_size;
    if (size == 0) {
        rtype_t rtype = {
                .gc_heap_size = 1, // 空 struct 默认占用 1 一个字节, 让 gc malloc 可以编译通过
                .hash = type_hash(t),
                .kind = TYPE_STRUCT,
                .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(1, POINTER_SIZE)),
                .length = t.struct_->properties->length,
                .hashes_offset = -1,
                .last_ptr = 0,
        };

        return rtype;
    }

    int64_t gc_bits_offset = data_put(NULL, calc_gc_bits_size(size, POINTER_SIZE));

    int64_t fields_size = sizeof(rtype_field_t) * t.struct_->properties->length;
    rtype_field_t *fields = mallocz(fields_size);

    int64_t offset = 0;
    for (int i = 0; i < t.struct_->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t.struct_->properties, i);
        int64_t field_align = p->type.align;
        int64_t field_size = p->type.storage_size;
        offset = align_up(offset, field_align);

        rtype_field_t field = {
                .name_offset = strtable_put(p->name),
                .offset = offset,
                .hash = type_hash(p->type),
        };


        offset += field_size;

        fields[i] = field;
    }

    // 假设没有 struct， 可以根据所有 property 计算 gc bits
    offset = 0;
    uint16_t last_ptr_offset = rtype_struct_gc_bits(gc_bits_offset, &offset, t.struct_);

    rtype_t rtype = {
            .gc_heap_size = size,
            .hash = type_hash(t),
            .kind = TYPE_STRUCT,
            .malloc_gc_bits_offset = gc_bits_offset,
            .length = t.struct_->properties->length,
            .last_ptr = last_ptr_offset,
            .hashes_offset = data_put((uint8_t *) fields, fields_size),
    };

    return rtype;
}

/**
 * 参考 struct
 * @param t
 * @return
 */
static inline rtype_t rtype_tuple(type_t t) {
    int64_t offset = 0;
    int64_t max_align = 0;

    int64_t hashes_offset = data_put(NULL, sizeof(int64_t) * t.tuple->elements->length);
    for (int64_t i = 0; i < t.tuple->elements->length; ++i) {
        type_t *element_type = ct_list_value(t.tuple->elements, i);

        int64_t element_size = element_type->storage_size;
        int element_align = element_type->align;
        if (element_align > max_align) {
            max_align = element_align;
        }

        offset = align_up(offset, element_align);
        offset += element_size;

        ((int64_t *) CTDATA(hashes_offset))[i] = type_hash(*element_type);
    }
    int64_t size = align_up(offset, max_align);

    rtype_t rtype = {
            .gc_heap_size = size,
            .hash = type_hash(t),
            .kind = TYPE_TUPLE,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(size, POINTER_SIZE)),
            .length = t.tuple->elements->length,
            .hashes_offset = hashes_offset,
    };

    offset = 0;
    rtype.last_ptr = rtype_tuple_gc_bits(rtype.malloc_gc_bits_offset, &offset, t.tuple);

    return rtype;
}

/**
 * 仅做 reflect, 不写入任何 table 中
 * @param t
 * @return
 */
static inline rtype_t reflect_type(type_t t) {
    assert(t.kind != TYPE_IDENT);
    rtype_t rtype = {0};

    switch (t.kind) {
        case TYPE_STRING:
            rtype = rtype_string(t);
            break;
        case TYPE_REF:
            rtype = rtype_ref(t);
            break;
        case TYPE_PTR:
            rtype = rtype_ptr(t);
            break;
        case TYPE_ANYPTR:
            rtype = rtype_anyptr(t);
            break;
        case TYPE_VEC:
            rtype = rtype_vec(t);
            break;
        case TYPE_CHAN:
            rtype = rtype_chan(t);
            break;
        case TYPE_ARR:
            rtype = rtype_array(t);
            break;
        case TYPE_MAP:
            rtype = rtype_map(t);
            break;
        case TYPE_SET:
            rtype = rtype_set(t);
            break;
        case TYPE_TUPLE:
            rtype = rtype_tuple(t);
            break;
        case TYPE_STRUCT:
            rtype = rtype_struct(t);
            break;
        case TYPE_FN:
            rtype = rtype_fn(t);
            break;
        case TYPE_UNION:
            rtype = rtype_union(t);
            break;
        case TYPE_ANY:
            rtype = rtype_any(t);
            break;
        case TYPE_TAGGED_UNION:
            rtype = rtype_tagged_union(t);
            break;
        case TYPE_INTERFACE:
            rtype = rtype_interface(t);
            break;
        default:
            rtype = rtype_origin(t);
    }
    if (t.ident) {
        rtype.ident_offset = strtable_put(t.ident);
    }

    rtype.storage_kind = type_storage_kind(t);
    rtype.storage_size = type_storage_size(t);

    return rtype;
}

static inline void ct_register_rtype(type_t t) {
    if (t.kind == TYPE_IDENT) { // 未递归完成，无法完成注册。
        return;
    }

    int64_t hash = type_hash(t);
    bool exists = table_exist(ct_rtype_table, itoa(hash));
    //    log_debug("module %s, add rtype %ld -> %s, exists %d", m->ident, hash, type_format(t), exists);
    if (!exists) {
        rtype_t rtype = reflect_type(t);
        assert(rtype.gc_heap_size >= 0);
        assert(rtype.hash == hash);
        rtype_t *mem_rtype = rtype_push(rtype);
        table_set(ct_rtype_table, itoa(rtype.hash), mem_rtype);
    }
}

#endif //NATURE_SRC_RTYPE_H_

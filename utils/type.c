#include "type.h"

#include "assertf.h"
#include "bitmap.h"
#include "ct_list.h"
#include "custom_links.h"
#include "helper.h"

rtype_t rtype_base(type_kind kind) {
    uint32_t hash = hash_string(itoa(kind));
    rtype_t rtype = {
            .size = type_kind_sizeof(kind), // 单位 byte
            .hash = hash,
            .last_ptr = 0,
            .kind = kind,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    if (rtype.size > 0) {
        rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    }

    return rtype;
}

static rtype_t rtype_rawptr(type_ptr_t *t) {
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = dsprintf("%d_%lu", TYPE_RAWPTR, value_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_ptr_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_RAWPTR,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    (CTDATA(rtype.hashes_offset))[0] = value_rtype.hash;

    return rtype;
}

static rtype_t rtype_anyptr(type_kind kind) {
    char *str = dsprintf("%d", TYPE_ANYPTR);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_ptr_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_ANYPTR,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}


/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static rtype_t rtype_pointer(type_ptr_t *t) {
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = dsprintf("%d_%lu", TYPE_PTR, value_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_ptr_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_PTR,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    (CTDATA(rtype.hashes_offset))[0] = value_rtype.hash;


    return rtype;
}

/**
 * hash = type_kind
 * @param t
 * @return
 */
static rtype_t rtype_string() {
    uint32_t hash = hash_string(itoa(TYPE_STRING));
    rtype_t rtype = {
            .size = sizeof(n_string_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_STRING,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };

    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}

/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static rtype_t rtype_vec(type_vec_t *t) {
    rtype_t element_rtype = reflect_type(t->element_type);

    char *str = dsprintf("%d_%lu", TYPE_VEC, element_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_vec_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_VEC,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    (CTDATA(rtype.hashes_offset))[0] = element_rtype.hash;

    return rtype;
}

static rtype_t rtype_chan(type_chan_t *t) {
    rtype_t element_rtype = reflect_type(t->element_type);

    char *str = dsprintf("%d_%lu", TYPE_CHAN, element_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_chan_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE * 5,
            .kind = TYPE_CHAN,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));

    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 2);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 3);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 4);

    (CTDATA(rtype.hashes_offset))[0] = element_rtype.hash;

    return rtype;
}

/**
 * hash = type_kind + count + element_type_hash
 * @param t
 * @return
 */
rtype_t rtype_array(type_array_t *t) {
    rtype_t element_rtype = reflect_type(t->element_type);
    uint64_t element_size = type_sizeof(t->element_type);

    char *str = dsprintf("%d_%lu_%lu", TYPE_ARR, t->length, element_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = element_size * t->length,
            .hash = hash,
            .kind = TYPE_ARR,
            .length = t->length, // array length 特殊占用
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };

    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));

    uint64_t offset = 0;
    rtype.last_ptr = rtype_array_gc_bits(rtype.malloc_gc_bits_offset, &offset, t);

    (CTDATA(rtype.hashes_offset))[0] = element_rtype.hash;

    return rtype;
}

/**
 * hash = type_kind + key_rtype.hash + value_rtype.hash
 * @param t
 * @return
 */
static rtype_t rtype_map(type_map_t *t) {
    rtype_t key_rtype = reflect_type(t->key_type);
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = dsprintf("%d_%lu_%lu", TYPE_MAP, key_rtype.hash, value_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_map_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE * 3, // hash_table + key_data + value_data
            .kind = TYPE_MAP,
            .length = 2,
            .hashes_offset = data_put(NULL, sizeof(uint64_t) * 2),
            .malloc_gc_bits_offset = -1,
    };

    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0); // hash_table
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1); // key_data
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 2); // value_data

    (CTDATA(rtype.hashes_offset))[0] = key_rtype.hash;
    (CTDATA(rtype.hashes_offset))[1] = value_rtype.hash;

    return rtype;
}

/**
 * @param t
 * @return
 */
static rtype_t rtype_set(type_set_t *t) {
    rtype_t key_rtype = reflect_type(t->element_type);

    char *str = dsprintf("%d_%lu", TYPE_SET, key_rtype.hash);
    uint32_t hash = hash_string(str);
    free(str);
    rtype_t rtype = {
            .size = sizeof(n_set_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE * 2, // hash_table + key_data
            .kind = TYPE_SET,
            .length = 1,
            .hashes_offset = data_put(NULL, sizeof(uint64_t)),
            .malloc_gc_bits_offset = -1,
    };
    // 计算 gc_bits
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0); // hash_table
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1); // key_data

    (CTDATA(rtype.hashes_offset))[0] = key_rtype.hash;

    return rtype;
}

static rtype_t rtype_interface(type_t t) {
    char *str = itoa(TYPE_INTERFACE);

    // typedef 的 right expr 没有 ident, 当然对应的 rtype 也用不上，所以不需要做额外的处理
    if (t.ident) {
        str = str_connect(str, t.ident); // 通过 interface 的名称进行绝对区分
    }
    uint32_t hash = hash_string(str);

    rtype_t rtype = {
            .size = POINTER_SIZE * 4, // element_rtype + value(并不知道 value 的类型)
            .hash = hash,
            .kind = TYPE_INTERFACE,
            .last_ptr = POINTER_SIZE * 2,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(POINTER_SIZE * 2, POINTER_SIZE)),
            .hashes_offset = -1,
    };

    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 1);

    return rtype;
}

/**
 * 从类型声明上无法看出 any 是否需要 gc,那就默认第二个值总是需要 gc 扫描
 * hash = type_kind
 * @param t
 * @return
 */
static rtype_t rtype_union(type_union_t *t) {
    uint32_t hash = hash_string(itoa(TYPE_UNION));

    rtype_t rtype = {
            .size = POINTER_SIZE * 2, // element_rtype + value(并不知道 value 的类型)
            .hash = hash,
            .kind = TYPE_UNION,
            .last_ptr = POINTER_SIZE,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(POINTER_SIZE * 2, POINTER_SIZE)),
            .hashes_offset = -1,
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
static rtype_t rtype_fn(type_fn_t *t) {
    char *str = itoa(TYPE_FN);
    rtype_t return_rtype = ct_reflect_type(t->return_type);
    str = str_connect(str, itoa(return_rtype.hash));
    for (int i = 0; i < t->param_types->length; ++i) {
        type_t *typeuse = ct_list_value(t->param_types, i);
        rtype_t formal_type = ct_reflect_type(*typeuse);
        str = str_connect(str, itoa(formal_type.hash));
    }
    rtype_t rtype = {
            .size = POINTER_SIZE,
            .hash = hash_string(str),
            .kind = TYPE_FN,
            .last_ptr = 0,
            .hashes_offset = -1,
            .malloc_gc_bits_offset = -1,
    };
    rtype.malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(rtype.size, POINTER_SIZE));

    rtype.last_ptr = 8;
    bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), 0);

    return rtype;
}

static uint64_t rtype_array_gc_bits(uint64_t gc_bits_offset, uint64_t *offset, type_array_t *t) {
    // offset 已经按照 align 对齐过了，这里不需要重复对齐
    uint64_t last_ptr_offset = 0;

    for (int i = 0; i < t->length; ++i) {
        uint64_t last_ptr_temp_offset = 0;
        if (t->element_type.kind == TYPE_STRUCT) {
            last_ptr_temp_offset = rtype_struct_gc_bits(gc_bits_offset, offset, t->element_type.struct_);
        } else if (t->element_type.kind == TYPE_ARR) {
            last_ptr_temp_offset = rtype_array_gc_bits(gc_bits_offset, offset, t->element_type.array);
        } else {
            uint64_t bit_index = *offset / POINTER_SIZE;
            if (type_is_pointer_heap(t->element_type)) {
                bitmap_set(CTDATA(gc_bits_offset), bit_index);
                last_ptr_temp_offset = *offset;
            }

            *offset += type_sizeof(t->element_type);
        }

        if (last_ptr_temp_offset > last_ptr_offset) {
            last_ptr_offset = last_ptr_temp_offset;
        }
    }

    return last_ptr_offset;
}

static uint64_t rtype_struct_gc_bits(uint64_t gc_bits_offset, uint64_t *offset, type_struct_t *t) {
    // offset 已经按照 align 对齐过了，这里不需要重复对齐
    uint64_t last_ptr_offset = 0;
    for (int i = 0; i < t->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t->properties, i);

        // 属性基础地址对齐
        *offset = align_up(*offset, type_alignof(p->type));

        uint64_t last_ptr_temp_offset = 0;
        if (p->type.kind == TYPE_STRUCT) {
            last_ptr_temp_offset = rtype_struct_gc_bits(gc_bits_offset, offset, p->type.struct_);
        } else if (p->type.kind == TYPE_ARR) {
            last_ptr_temp_offset = rtype_array_gc_bits(gc_bits_offset, offset, p->type.array);
        } else {
            uint64_t size = type_sizeof(p->type); // 等待存储的 struct size
            // 这里就是存储位置
            uint64_t bit_index = *offset / POINTER_SIZE;

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
    *offset = align_up(*offset, t->align);

    return last_ptr_offset;
}

/**
 * hash = type_kind + key type hash
 * @param t
 * @return
 */
static rtype_t rtype_struct(type_struct_t *t) {
    char *str = itoa(TYPE_STRUCT);

    uint64_t size = type_struct_sizeof(t);
    if (size == 0) {
        rtype_t rtype = {
                .size = 1, // 空 struct 默认占用 1 一个字节, 让 gc malloc 可以编译通过
                .hash = hash_string(str),
                .kind = TYPE_STRUCT,
                .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(1, POINTER_SIZE)),
                .length = t->properties->length,
                .hashes_offset = -1,
                .last_ptr = 0,
        };

        return rtype;
    }

    uint64_t offset = 0; // 基于 offset 计算 gc bits
    uint64_t gc_bits_offset = data_put(NULL, calc_gc_bits_size(size, POINTER_SIZE));

    // 假设没有 struct， 可以根据所有 property 计算 gc bits
    uint16_t last_ptr_offset = rtype_struct_gc_bits(gc_bits_offset, &offset, t);


    uint64_t hashes_offset = data_put(NULL, sizeof(uint64_t) * t->properties->length);

    // 记录需要 gc 的 key 的
    int hash_index = 0;
    for (int i = 0; i < t->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t->properties, i);
        rtype_t element_rtype = ct_reflect_type(p->type);
        (CTDATA(hashes_offset))[i] = element_rtype.hash;

        str = str_connect(str, itoa(element_rtype.hash));
    }

    rtype_t rtype = {
            .size = size,
            .hash = hash_string(str),
            .kind = TYPE_STRUCT,
            .malloc_gc_bits_offset = gc_bits_offset,
            .length = t->properties->length,
            .hashes_offset = hashes_offset,
            .last_ptr = last_ptr_offset,
    };

    return rtype;
}

/**
 * 参考 struct
 * @param t
 * @return
 */
static rtype_t rtype_tuple(type_tuple_t *t) {
    char *str = itoa(TYPE_TUPLE);
    uint64_t offset = 0;
    uint64_t need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};

    uint64_t hashes_offset = data_put(NULL, sizeof(uint64_t) * t->elements->length);

    // 记录需要 gc 的 key 的
    for (uint64_t i = 0; i < t->elements->length; ++i) {
        type_t *element_type = ct_list_value(t->elements, i);

        uint64_t element_size = type_sizeof(*element_type);
        int element_align = type_alignof(*element_type);

        // 按 offset 对齐
        offset = align_up(offset, element_align);
        // 计算 element_rtype
        rtype_t rtype = ct_reflect_type(*element_type);
        str = str_connect(str, itoa(rtype.hash));

        // 如果存在 heap 中就是需要 gc
        bool need_gc = type_is_pointer_heap(*element_type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }

        (CTDATA(hashes_offset))[i] = rtype.hash;

        offset += element_size;
    }
    uint64_t size = align_up(offset, t->align);

    rtype_t rtype = {
            .size = size,
            .hash = hash_string(str),
            .kind = TYPE_TUPLE,
            .malloc_gc_bits_offset = data_put(NULL, calc_gc_bits_size(size, POINTER_SIZE)),
            .length = t->elements->length,
            .hashes_offset = hashes_offset,
    };

    if (need_gc_count > 0) {
        // 默认 size 8byte 对齐了
        for (int i = 0; i < need_gc_count; ++i) {
            uint16_t gc_offset = need_gc_offsets[i];
            bitmap_set(CTDATA(rtype.malloc_gc_bits_offset), gc_offset / POINTER_SIZE);
        }

        rtype.last_ptr = need_gc_offsets[need_gc_count - 1] + POINTER_SIZE;
    }

    return rtype;
}

uint64_t type_kind_sizeof(type_kind t) {
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

uint64_t type_struct_sizeof(type_struct_t *s) {
    // 只有当 struct 没有元素，或者有嵌套 struct 依旧没有元素时， align 才为 0
    if (s->align == 0) {
        return 0; // 空 struct 占用 1 size, 让 linear/lower 都可以正常工作
    }

    uint64_t size = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        uint64_t element_size = type_sizeof(p->type);
        uint8_t element_align = type_alignof(p->type);

        size = align_up(size, element_align);
        size += element_size;
    }

    // struct 整体按照 max_align 对齐
    size = align_up(size, s->align);

    return size;
}

/**
 * @param t
 * @return
 */
uint64_t type_sizeof(type_t t) {
    if (t.kind == TYPE_STRUCT) {
        uint64_t size = type_struct_sizeof(t.struct_);
        if (size == 0) {
            return 1;
        }
        return size;
    }

    if (t.kind == TYPE_ARR) {
        return t.array->length * type_sizeof(t.array->element_type);
    }

    return type_kind_sizeof(t.kind);
}

uint64_t type_alignof(type_t t) {
    if (t.kind == TYPE_STRUCT) {
        //        assert(t.struct_->align > 0);
        return t.struct_->align;
    }
    if (t.kind == TYPE_ARR) {
        return type_alignof(t.array->element_type);
    }

    return type_sizeof(t);
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

/**
 * 仅做 reflect, 不写入任何 table 中
 * @param t
 * @return
 */
rtype_t reflect_type(type_t t) {
    rtype_t rtype = {0};

    switch (t.kind) {
        case TYPE_STRING:
            rtype = rtype_string();
            break;
        case TYPE_PTR:
            rtype = rtype_pointer(t.ptr);
            break;
        case TYPE_RAWPTR:
            rtype = rtype_rawptr(t.ptr);
            break;
        case TYPE_ANYPTR:
            rtype = rtype_anyptr(t.kind);
            break;
        case TYPE_VEC:
            rtype = rtype_vec(t.vec);
            break;
        case TYPE_CHAN:
            rtype = rtype_chan(t.chan);
            break;
        case TYPE_ARR:
            rtype = rtype_array(t.array);
            break;
        case TYPE_MAP:
            rtype = rtype_map(t.map);
            break;
        case TYPE_SET:
            rtype = rtype_set(t.set);
            break;
        case TYPE_TUPLE:
            rtype = rtype_tuple(t.tuple);
            break;
        case TYPE_STRUCT:
            rtype = rtype_struct(t.struct_);
            break;
        case TYPE_FN:
            rtype = rtype_fn(t.fn);
            break;
        case TYPE_UNION:
            rtype = rtype_union(t.union_);
            break;
        case TYPE_INTERFACE:
            rtype = rtype_interface(t);
            break;
        default:
            if (is_integer(t.kind) || is_float(t.kind) || t.kind == TYPE_NULL || t.kind == TYPE_VOID ||
                t.kind == TYPE_BOOL || t.kind == TYPE_INTEGER_T) {
                rtype = rtype_base(t.kind);
            }
    }
    rtype.in_heap = t.in_heap;
    if (t.ident) {
        rtype.ident_offset = strtable_put(t.ident);
    }

    return rtype;
}

rtype_t ct_reflect_type(type_t t) {
    rtype_t rtype = reflect_type(t);
    if (rtype.kind == 0) {
        return rtype;
    }

    bool exists = table_exist(ct_rtype_table, itoa(rtype.hash));
    if (!exists) {
        // 添加到 ct_rtypes 并得到 index(element_rtype 是值传递并 copy)
        rtype_t *mem = rtype_push(rtype);

        // 将 index 添加到 table
        table_set(ct_rtype_table, itoa(rtype.hash), mem);
    }

    return rtype;
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
    // type_array 和 type_struct 的 var 就是一个 pointer， 所以总是需要 gc
    // stack 中的数据，gc 是无法扫描的
    //    if (is_alloc_stack(t)) {
    //        return true;
    //    }
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
 * compile time
 * @param t
 * @return
 */
uint64_t ct_find_rtype_hash(type_t t) {
    rtype_t rtype = ct_reflect_type(t);
    assertf(rtype.hash, "type reflect failed");
    return rtype.hash;
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

        int element_size = type_sizeof(p->type);
        int element_align = type_alignof(p->type);

        offset = align_up(offset, element_align);
        if (str_equal(p->key, key)) {
            // found
            return offset;
        }

        offset += element_size;
    }

    assertf(false, "key=%s not found in struct", key);
    return 0;
}

struct_property_t *type_struct_property(type_struct_t *s, char *key) {
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);
        if (str_equal(p->key, key)) {
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

    if (ident_is_param(&t)) {
        return ident;
    }

    return dsprintf("%s(%s)", ident, _type_format(t));
}

char *type_origin_format(type_t t) {
    char *ident = t.ident;
    if (ident == NULL) {
        return _type_format(t);
    }

    if (ident_is_param(&t)) {
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

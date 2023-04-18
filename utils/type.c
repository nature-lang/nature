#include "type.h"
#include "assertf.h"
#include "helper.h"
#include "bitmap.h"
#include "ct_list.h"
#include "custom_links.h"
#include "src/cross.h"

static rtype_t rtype_base(type_kind kind) {
    uint32_t hash = hash_string(itoa(kind));
    rtype_t rtype = {
            .size = type_kind_sizeof(kind),  // 单位 byte
            .hash = hash,
            .last_ptr = 0,
            .kind = kind,
    };
    rtype.gc_bits = malloc_gc_bits(rtype.size);

    return rtype;
}

//static rtype_t rtype_byte() {
//    return rtype_base(TYPE_BYTE);
//}

static rtype_t rtype_int(type_kind kind) {
    return rtype_base(kind);
}

static rtype_t rtype_float() {
    return rtype_base(TYPE_FLOAT);
}

static rtype_t rtype_bool() {
    return rtype_base(TYPE_BOOL);
}


/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static rtype_t rtype_pointer(type_pointer_t *t) {
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_POINTER, value_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_pointer_t),
            .hash = hash,
            .last_ptr = cross_ptr_size(),
            .kind = TYPE_POINTER,
    };
    // 计算 gc_bits
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(rtype.gc_bits, 0);

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
            .size = sizeof(memory_string_t),
            .hash = hash,
            .last_ptr = 0,
            .kind = TYPE_STRING,
    };

    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(rtype.gc_bits, 0);

    return rtype;
}

/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static rtype_t rtype_list(type_list_t *t) {
    rtype_t element_rtype = reflect_type(t->element_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_LIST, element_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_list_t),
            .hash = hash,
            .last_ptr = cross_ptr_size(),
            .kind = TYPE_LIST,
    };
    // 计算 gc_bits
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(rtype.gc_bits, 0);

    return rtype;
}

/**
 * hash = type_kind + count + element_type_hash
 * @param t
 * @return
 */
static rtype_t rtype_array(type_array_t *t) {
    rtype_t element_rtype = t->element_rtype;
    uint64_t element_size = element_rtype.size;

    char *str = fixed_sprintf("%d_%lu_%lu", TYPE_ARRAY, t->length, element_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size = element_size * t->length,
            .hash = hash,
            .kind = TYPE_ARRAY,
    };
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bool need_gc = element_rtype.last_ptr > 0; // element 包含指针数据
    if (need_gc) {
        rtype.last_ptr = element_size * t->length;

        // need_gc 暗示了 8byte 对齐了
        for (int i = 0; i < rtype.size / cross_ptr_size(); ++i) {
            bitmap_set(rtype.gc_bits, i);
        }
    }

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

    char *str = fixed_sprintf("%d_%lu_%lu", TYPE_MAP, key_rtype.hash, value_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_map_t),
            .hash = hash,
            .last_ptr = cross_ptr_size() * 3, // hash_table + key_data + value_data
            .kind = TYPE_MAP,
    };
    // 计算 gc_bits
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(rtype.gc_bits, 0); // hash_table
    bitmap_set(rtype.gc_bits, 1); // key_data
    bitmap_set(rtype.gc_bits, 2); // value_data

    return rtype;
}

/**
 * @param t
 * @return
 */
static rtype_t rtype_set(type_set_t *t) {
    rtype_t key_rtype = reflect_type(t->key_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_SET, key_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_set_t),
            .hash = hash,
            .last_ptr = cross_ptr_size() * 2, // hash_table + key_data
            .kind = TYPE_SET,
    };
    // 计算 gc_bits
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(rtype.gc_bits, 0); // hash_table
    bitmap_set(rtype.gc_bits, 1); // key_data

    return rtype;
}


/**
 * hash = type_kind
 * @param t
 * @return
 */
static rtype_t rtype_any(type_any_t *t) {
    uint32_t hash = hash_string(itoa(TYPE_ANY));

    rtype_t rtype = {
            .size = cross_ptr_size() * 2, // element_rtype + value(并不知道 value 的类型)
            .hash = hash,
            .kind = TYPE_ANY,
            .last_ptr = 0,
            .gc_bits = malloc_gc_bits(cross_ptr_size() * 2)
    };
    return rtype;
}

/**
 * TODO golang 的 fn 的 gc 是 1，所以 fn 为啥需要扫描呢？难道函数的代码段还能存储在堆上？
 * hash = type_kind + params hash + return_type hash
 * @param t
 * @return
 */
static rtype_t rtype_fn(type_fn_t *t) {
    char *str = itoa(TYPE_FN);
    rtype_t return_rtype = ct_reflect_type(t->return_type);
    str = str_connect(str, itoa(return_rtype.hash));
    for (int i = 0; i < t->formal_types->length; ++i) {
        type_t *typeuse = ct_list_value(t->formal_types, i);
        rtype_t formal_type = ct_reflect_type(*typeuse);
        str = str_connect(str, itoa(formal_type.hash));
    }
    rtype_t rtype = {
            .size = cross_ptr_size(),
            .hash = hash_string(str),
            .kind = TYPE_FN,
            .last_ptr = 0,
            .gc_bits = malloc_gc_bits(cross_ptr_size())
    };
    return rtype;
}

/**
 * hash = type_kind + key type hash
 * @param t
 * @return
 */
static rtype_t rtype_struct(type_struct_t *t) {
    char *str = itoa(TYPE_STRUCT);
    uint64_t offset = 0;
    uint64_t max = 0;
    uint64_t need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};
    // 记录需要 gc 的 key 的
    for (int i = 0; i < t->properties->length; ++i) {
        struct_property_t *property = ct_list_value(t->properties, i);
        uint16_t item_size = type_sizeof(property->type);
        if (item_size > max) {
            max = item_size;
        }
        // 按 offset 对齐
        offset = align(offset, item_size);
        // 计算 element_rtype
        rtype_t rtype = ct_reflect_type(property->type);
        str = str_connect(str, itoa(rtype.hash));
        bool need_gc = type_need_gc(property->type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }
        offset += item_size;
    }
    uint64_t size = align(offset, max);


    rtype_t rtype = {
            .size = size,
            .hash = hash_string(str),
            .kind = TYPE_STRUCT,
            .gc_bits = malloc_gc_bits(size)
    };
    if (need_gc_count) {
        // 默认 size 8byte 对齐了
        for (int i = 0; i < need_gc_count; ++i) {
            uint16_t gc_offset = need_gc_offsets[i];
            bitmap_set(rtype.gc_bits, gc_offset / cross_ptr_size());
        }
        rtype.last_ptr = need_gc_offsets[need_gc_count - 1] + cross_ptr_size();
    }

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
    uint64_t max = 0;
    uint64_t need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};
    // 记录需要 gc 的 key 的
    for (uint64_t i = 0; i < t->elements->length; ++i) {
        type_t *element_type = ct_list_value(t->elements, i);
        uint16_t item_size = type_sizeof(*element_type);
        if (item_size > max) {
            max = item_size;
        }
        // 按 offset 对齐
        offset = align(offset, item_size);
        // 计算 element_rtype
        rtype_t rtype = ct_reflect_type(*element_type);
        str = str_connect(str, itoa(rtype.hash));

        // 如果存在 heap 中就是需要 gc
        // TODO element_type size > 8 处理
        bool need_gc = type_need_gc(*element_type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }
        offset += item_size;
    }
    uint64_t size = align(offset, max);


    rtype_t rtype = {
            .size = size,
            .hash = hash_string(str),
            .kind = TYPE_TUPLE,
            .gc_bits = malloc_gc_bits(size)
    };

    if (need_gc_count > 0) {
        // 默认 size 8byte 对齐了
        for (int i = 0; i < need_gc_count; ++i) {
            uint16_t gc_offset = need_gc_offsets[i];
            bitmap_set(rtype.gc_bits, gc_offset / cross_ptr_size());
        }

        rtype.last_ptr = need_gc_offsets[need_gc_count - 1] + cross_ptr_size();
    }

    return rtype;
}

// TODO 功能上和 type in_heap 有一定的冲突
uint8_t type_kind_sizeof(type_kind t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_INT8:
        case TYPE_UINT8:
            return 1;
        case TYPE_INT16:
        case TYPE_UINT16:
            return 2;
        case TYPE_INT32:
        case TYPE_UINT32:
            return 4;
        case TYPE_INT64:
        case TYPE_UINT64:
            return 8;
        case TYPE_INT:
        case TYPE_UINT:
        case TYPE_FLOAT:
            return cross_number_size(); // 固定大小
        default:
            return cross_ptr_size();
    }
}

/**
 * TODO 目前阶段最大的数据类型也就是指针了
 * @param t
 * @return
 */
uint16_t type_sizeof(type_t t) {
    return type_kind_sizeof(t.kind);
}


type_t type_ptrof(type_t t) {
    type_t result;
    result.status = t.status;

    result.kind = TYPE_POINTER;
    result.pointer = NEW(type_pointer_t);
    result.pointer->value_type = t;
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
        case TYPE_BOOL:
            rtype = rtype_bool();
            break;
        case TYPE_STRING:
            rtype = rtype_string();
            break;
        case TYPE_POINTER:
            rtype = rtype_pointer(t.pointer);
            break;
        case TYPE_LIST:
            rtype = rtype_list(t.list);
            break;
        case TYPE_ARRAY:
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
        case TYPE_ANY:
            rtype = rtype_any(t.any);
            break;
        default:
            if (is_integer(t.kind) || is_float(t.kind)) {
                rtype = rtype_base(t.kind);
            }
    }
    rtype.in_heap = t.in_heap;
    return rtype;
}

rtype_t ct_reflect_type(type_t t) {
    rtype_t rtype = reflect_type(t);
    if (rtype.kind == 0) {
        return rtype;
    }

    uint64_t rtype_index = (uint64_t) table_get(ct_rtype_table, itoa(rtype.hash));
    if (rtype_index == 0) {
        // 添加到 ct_rtypes 并得到 index(element_rtype 是值传递并 copy)
        uint64_t index = rtypes_push(rtype);
        // 将 index 添加到 table
        table_set(ct_rtype_table, itoa(rtype.hash), (void *) index);
        rtype.index = index;
    } else {
        rtype.index = rtype_index;
    }

    return rtype;
}


rtype_t rt_reflect_type(type_t t) {
    rtype_t rtype = reflect_type(t);

    // TODO 应该在 runtime 中实现，且写入到 rt reflect_type 中
    return rtype;
}

uint64_t calc_gc_bits_size(uint64_t size, uint8_t ptr_size) {
    size = align(size, ptr_size);

    uint64_t gc_bits_size = size / ptr_size;

    // 8bit  = 1byte, 再次对齐
    gc_bits_size = align(gc_bits_size, 8);

    return gc_bits_size;
}

byte *malloc_gc_bits(uint64_t size) {
    uint64_t gc_bits_size = calc_gc_bits_size(size, cross_ptr_size());
    return mallocz(gc_bits_size);
}

type_ident_t *typeuse_ident_new(char *literal) {
    type_ident_t *t = NEW(type_ident_t);
    t->literal = literal;
    return t;
}

bool type_need_gc(type_t t) {
    if (t.in_heap) {
        return true;
    }
    return false;
}

uint64_t rtypes_push(rtype_t rtype) {
    uint64_t index = ct_rtype_list->length;

    rtype.index = index; // 索引反填
    ct_list_push(ct_rtype_list, &rtype);

    ct_rtype_size += sizeof(rtype_t);
    ct_rtype_size += calc_gc_bits_size(rtype.size, cross_ptr_size());
    ct_rtype_count += 1;

    return index;
}

/**
 * compile time
 * @param t
 * @return
 */
uint64_t ct_find_rtype_index(type_t t) {
    rtype_t rtype = ct_reflect_type(t);
    assertf(rtype.hash, "type reflect failed");
    uint64_t index = (uint64_t) table_get(ct_rtype_table, itoa(rtype.hash));
    assertf(index, "notfound index by ct_rtype_table,hash=%d", rtype.hash);
    return index;
}


/**
 * rtype 在堆外占用的空间大小,比如 stack,global,list value, struct value 中的占用的 size 的大小
 * 如果类型没有存储在堆中，则其在堆外占用的大小是就是类型本身的大小，如果类型存储在堆中，其在堆外存储的是指向堆的指针
 * 占用 cross_ptr_size() 大小
 * @param rtype
 * @return
 */
uint64_t rtype_heap_out_size(rtype_t *rtype, uint8_t ptr_size) {
    if (rtype->in_heap) {
        return cross_ptr_size();
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
        uint64_t item_size = type_sizeof(p->type);
        offset = align(offset, item_size);
        if (str_equal(p->key, key)) {
            // found
            return offset;
        }
        offset += item_size;
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


uint64_t type_tuple_offset(type_tuple_t *t, uint64_t index) {
    uint64_t offset = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *typedecl = ct_list_value(t->elements, i);
        uint64_t item_size = type_sizeof(*typedecl);
        offset = align(offset, item_size);

        if (i == index) {
            // found
            return offset;
        }
        offset += item_size;
    }

    return 0;
}

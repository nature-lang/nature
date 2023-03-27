#include "type.h"
#include "assertf.h"
#include "helper.h"
#include "bitmap.h"
#include "ct_list.h"
#include "links.h"

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

static rtype_t rtype_byte() {
    return rtype_base(TYPE_BYTE);
}

static rtype_t rtype_int() {
    return rtype_base(TYPE_INT);
}

static rtype_t rtype_float() {
    return rtype_base(TYPE_FLOAT);
}

static rtype_t rtype_bool() {
    return rtype_base(TYPE_BOOL);
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
static rtype_t rtype_list(typedecl_list_t *t) {
    rtype_t element_rtype = reflect_type(t->element_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_LIST, element_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_list_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
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
static rtype_t rtype_array(typedecl_array_t *t) {
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
        for (int i = 0; i < rtype.size / POINTER_SIZE; ++i) {
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
static rtype_t rtype_map(typedecl_map_t *t) {
    rtype_t key_rtype = reflect_type(t->key_type);
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = fixed_sprintf("%d_%lu_%lu", TYPE_MAP, key_rtype.hash, value_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_map_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE * 3, // hash_table + key_data + value_data
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
static rtype_t rtype_set(typedecl_set_t *t) {
    rtype_t key_rtype = reflect_type(t->key_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_SET, key_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(memory_set_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE * 2, // hash_table + key_data
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
static rtype_t rtype_any(typedecl_any_t *t) {
    uint32_t hash = hash_string(itoa(TYPE_ANY));

    rtype_t rtype = {
            .size = POINTER_SIZE * 2, // element_rtype + value(并不知道 value 的类型)
            .hash = hash,
            .kind = TYPE_ANY,
            .last_ptr = 0,
            .gc_bits = malloc_gc_bits(POINTER_SIZE * 2)
    };
    return rtype;
}

/**
 * TODO golang 的 fn 的 gc 是 1，所以 fn 为啥需要扫描呢？难道函数的代码段还能存储在堆上？
 * hash = type_kind + params hash + return_type hash
 * @param t
 * @return
 */
static rtype_t rtype_fn(typedecl_fn_t *t) {
    char *str = itoa(TYPE_FN);
    rtype_t return_rtype = ct_reflect_type(t->return_type);
    str = str_connect(str, itoa(return_rtype.hash));
    for (int i = 0; i < t->formals_count; ++i) {
        rtype_t formal_type = ct_reflect_type(t->formals_types[i]);
        str = str_connect(str, itoa(formal_type.hash));
    }
    rtype_t rtype = {
            .size = POINTER_SIZE,
            .hash = hash_string(str),
            .kind = TYPE_FN,
            .last_ptr = 0,
            .gc_bits = malloc_gc_bits(POINTER_SIZE)
    };
    return rtype;
}

/**
 * hash = type_kind + key type hash
 * @param t
 * @return
 */
static rtype_t rtype_struct(typedecl_struct_t *t) {
    char *str = itoa(TYPE_STRUCT);
    uint offset = 0;
    uint max = 0;
    uint need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};
    // 记录需要 gc 的 key 的
    for (int i = 0; i < t->count; ++i) {
        typedecl_struct_property_t property = t->properties[i];
        uint16_t item_size = type_sizeof(property.type);
        if (item_size > max) {
            max = item_size;
        }
        // 按 offset 对齐
        offset = align(offset, item_size);
        // 计算 element_rtype
        rtype_t rtype = ct_reflect_type(property.type);
        str = str_connect(str, itoa(rtype.hash));
        // TODO element_type size > 8 处理
        bool need_gc = type_need_gc(property.type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }
        offset += item_size;
    }
    uint size = align(offset, max);


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
            bitmap_set(rtype.gc_bits, gc_offset / POINTER_SIZE);
        }
        rtype.last_ptr = need_gc_offsets[need_gc_count - 1] + POINTER_SIZE;
    }

    return rtype;
}

/**
 * 参考 struct
 * @param t
 * @return
 */
static rtype_t rtype_tuple(typedecl_tuple_t *t) {
    char *str = itoa(TYPE_TUPLE);
    uint offset = 0;
    uint max = 0;
    uint need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};
    // 记录需要 gc 的 key 的
    for (uint64_t i = 0; i < t->elements->length; ++i) {
        typedecl_t *element_type = ct_list_value(t->elements, i);
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
    uint size = align(offset, max);


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
            bitmap_set(rtype.gc_bits, gc_offset / POINTER_SIZE);
        }

        rtype.last_ptr = need_gc_offsets[need_gc_count - 1] + POINTER_SIZE;
    }

    return rtype;
}

// TODO 功能上和 type in_heap 有一定的冲突
uint8_t type_kind_sizeof(type_kind t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_INT8:
        case TYPE_BYTE:
            return 1;
        case TYPE_INT16:
            return 2;
        case TYPE_INT32:
            return 4;
        case TYPE_INT64:
        case TYPE_FLOAT:
            return INT_SIZE; // 固定大小
        default:
            return POINTER_SIZE;
    }
}

/**
 * TODO 目前阶段最大的数据类型也就是指针了
 * @param t
 * @return
 */
uint16_t type_sizeof(typedecl_t t) {
    return type_kind_sizeof(t.kind);
}


typedecl_t type_ptrof(typedecl_t t, uint8_t point) {
    typedecl_t result;
    result.is_origin = t.is_origin;
    result.pointer = point;
    return result;
}

typedecl_t type_base_new(type_kind kind) {
    typedecl_t result = {
            .is_origin = true,
            .kind = kind,
            .value_decl = 0,
    };

    result.in_heap = type_default_in_heap(result);

    return result;
}

typedecl_t type_new(type_kind kind, void *value) {
    typedecl_t result = {
            .kind = kind,
            .value_decl = value
    };
    return result;
}

/**
 * 仅做 reflect, 不写入任何 table 中
 * @param t
 * @return
 */
rtype_t reflect_type(typedecl_t t) {
    rtype_t rtype = {0};
    rtype.in_heap = t.in_heap;

    switch (t.kind) {
        case TYPE_INT:
            rtype = rtype_int();
            break;
        case TYPE_FLOAT:
            rtype = rtype_float();
            break;
        case TYPE_BOOL:
            rtype = rtype_bool();
            break;
        case TYPE_BYTE:
            rtype = rtype_byte();
            break;
        case TYPE_STRING:
            rtype = rtype_string();
            break;
        case TYPE_LIST:
            rtype = rtype_list(t.list_decl);
            break;
        case TYPE_ARRAY:
            rtype = rtype_array(t.array_decl);
            break;
        case TYPE_MAP:
            rtype = rtype_map(t.map_decl);
            break;
        case TYPE_SET:
            rtype = rtype_set(t.set_decl);
            break;
        case TYPE_TUPLE:
            rtype = rtype_tuple(t.tuple_decl);
            break;
        case TYPE_STRUCT:
            rtype = rtype_struct(t.struct_decl);
            break;
        case TYPE_FN:
            rtype = rtype_fn(t.fn_decl);
            break;
        case TYPE_ANY:
            rtype = rtype_any(t.any_decl);
            break;
        default:
            return rtype; // element_rtype element_rtype
//            assertf(false, "cannot reflect type kind=%d", t.kind);
    }
    return rtype;
}

rtype_t ct_reflect_type(typedecl_t t) {
    rtype_t rtype = reflect_type(t);
    if (!rtype.kind) {
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


rtype_t rt_reflect_type(typedecl_t t) {
    rtype_t rtype = reflect_type(t);

    // TODO 应该在 runtime 中实现，且写入到 rt reflect_type 中
    return rtype;
}

uint64_t calc_gc_bits_size(uint64_t size) {
    size = align(size, POINTER_SIZE);

    uint64_t gc_bits_size = size / POINTER_SIZE;

    // 8bit  = 1byte, 再次对齐
    gc_bits_size = align(gc_bits_size, 8);

    return gc_bits_size;
}

byte *malloc_gc_bits(uint64_t size) {
    uint64_t gc_bits_size = calc_gc_bits_size(size);
    return mallocz(gc_bits_size);
}

typedecl_ident_t *typedecl_ident_new(char *literal) {
    typedecl_ident_t *t = NEW(typedecl_ident_t);
    t->literal = literal;
    return t;
}

bool type_need_gc(typedecl_t t) {
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
    ct_rtype_size += calc_gc_bits_size(rtype.size);
    ct_rtype_count += 1;

    return index;
}

/**
 * compile time
 * @param t
 * @return
 */
uint ct_find_rtype_index(typedecl_t t) {
    rtype_t rtype = ct_reflect_type(t);
    assertf(rtype.hash, "type reflect failed");
    uint64_t index = (uint64_t) table_get(ct_rtype_table, itoa(rtype.hash));
    assertf(index, "notfound index by ct_rtype_table,hash=%d", rtype.hash);
    return index;
}

/**
 * 一般标量类型其值默认会存储在 stack 中
 * 其他复合类型默认会在堆上创建，stack 中仅存储一个 ptr 指向堆内存。
 * 可以通过 kind 进行判断。
 * 后续会同一支持标量类型堆中存储，以及复合类型的栈中存储
 * @param typedecl
 * @return
 */
bool type_default_in_heap(typedecl_t typedecl) {
    if (typedecl.kind == TYPE_ANY ||
        typedecl.kind == TYPE_STRING ||
        typedecl.kind == TYPE_LIST ||
        typedecl.kind == TYPE_ARRAY ||
        typedecl.kind == TYPE_MAP ||
        typedecl.kind == TYPE_SET ||
        typedecl.kind == TYPE_TUPLE ||
        typedecl.kind == TYPE_STRUCT ||
        typedecl.kind == TYPE_FN) {
        return true;
    }
    return false;
}

/**
 * rtype 在堆外占用的空间大小,比如 stack,global,list value, struct value 中的占用的 size 的大小
 * 如果类型没有存储在堆中，则其在堆外占用的大小是就是类型本身的大小，如果类型存储在堆中，其在堆外存储的是指向堆的指针
 * 占用 POINTER_SIZE 大小
 * @param rtype
 * @return
 */
uint64_t rtype_heap_out_size(rtype_t *rtype) {
    if (rtype->in_heap) {
        return POINTER_SIZE;
    }
    return rtype->size;
}

/**
 * 需要按 key 对齐
 * @param type
 * @param key
 * @return
 */
uint64_t type_struct_offset(typedecl_struct_t *t, char *key) {
    uint64_t offset = 0;
    for (int i = 0; i < t->count; ++i) {
        typedecl_struct_property_t p = t->properties[i];
        uint64_t item_size = type_sizeof(p.type);
        offset = align(offset, item_size);
        if (str_equal(p.key, key)) {
            // found
            return offset;
        }
        offset += item_size;
    }

    assertf(false, "key=%v not found in struct", key);
    return 0;
}



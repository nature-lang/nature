#include "type.h"
#include "src/cross.h"
#include "assertf.h"
#include "helper.h"
#include "bitmap.h"
#include "ct_list.h"
#include "custom_links.h"


rtype_t rtype_base(type_kind kind) {
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
            .size =  sizeof(n_pointer_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
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
            .size = sizeof(n_string_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
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
            .size =  sizeof(n_list_t),
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
rtype_t rtype_array(type_array_t *t) {
    rtype_t element_rtype = t->element_rtype;
    uint64_t element_size = rtype_out_size(&element_rtype, POINTER_SIZE);

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
static rtype_t rtype_map(type_map_t *t) {
    rtype_t key_rtype = reflect_type(t->key_type);
    rtype_t value_rtype = reflect_type(t->value_type);

    char *str = fixed_sprintf("%d_%lu_%lu", TYPE_MAP, key_rtype.hash, value_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(n_map_t),
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
static rtype_t rtype_set(type_set_t *t) {
    rtype_t key_rtype = reflect_type(t->element_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_SET, key_rtype.hash);
    uint32_t hash = hash_string(str);
    rtype_t rtype = {
            .size =  sizeof(n_set_t),
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
            .gc_bits = malloc_gc_bits(POINTER_SIZE * 2)
    };

    bitmap_set(rtype.gc_bits, 0);

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
            .gc_bits = malloc_gc_bits(POINTER_SIZE)
    };
    rtype.last_ptr = 8;
    bitmap_set(rtype.gc_bits, 0);

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
    uint64_t need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};

    uint64_t *element_hash_list = malloc(sizeof(uint64_t) * t->properties->length);

    // 记录需要 gc 的 key 的
    for (int i = 0; i < t->properties->length; ++i) {
        struct_property_t *property = ct_list_value(t->properties, i);

        uint16_t item_size = type_sizeof(property->type);

        // 按 offset 对齐
        offset = align_up(offset, item_size);

        // 计算 element_rtype
        rtype_t element_rtype = ct_reflect_type(property->type);

        str = str_connect(str, itoa(element_rtype.hash));
        bool need_gc = type_need_gc(property->type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }

        offset += item_size;
        element_hash_list[i] = element_rtype.hash;
    }

    uint64_t size = align_up(offset, t->align);
    rtype_t rtype = {
            .size = size,
            .hash = hash_string(str),
            .kind = TYPE_STRUCT,
            .gc_bits = malloc_gc_bits(size),
            .element_count = t->properties->length,
            .element_hashes = element_hash_list,
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
        offset = align_up(offset, item_size);
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
    uint64_t size = align_up(offset, max);


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
    assert(t > 0);

    switch (t) {
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
//        case TYPE_CPTR:
//        case TYPE_INT:
//        case TYPE_UINT:
//        case TYPE_FLOAT:
//            return POINTER_SIZE; // 固定大小
        default:
            return POINTER_SIZE;
    }
}

static uint16_t type_struct_sizeof(type_struct_t *s) {
    uint16_t size = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        uint16_t element_size;
        uint8_t element_align;
        if (p->type.kind == TYPE_STRUCT) {
            element_size = type_struct_sizeof(p->type.struct_);
            element_align = p->type.struct_->align; // 一般是 struct 中的最大值
        } else {
            element_size = type_sizeof(p->type);
            element_align = element_size;
        }

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
uint16_t type_sizeof(type_t t) {
    if (t.kind == TYPE_STRUCT) {
        return type_struct_sizeof(t.struct_);
    }

    if (t.kind == TYPE_ARRAY) {
        return t.array->length * type_sizeof(t.array->element_type);
    }

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
        case TYPE_UNION:
            rtype = rtype_union(t.union_);
            break;
        default:
            if (is_integer(t.kind) || is_float(t.kind) || t.kind == TYPE_NULL) {
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

    // 8bit  = 1byte, 再次对齐
    gc_bits_size = align_up(gc_bits_size, 8);

    return gc_bits_size;
}

uint8_t *malloc_gc_bits(uint64_t size) {
    uint64_t gc_bits_size = calc_gc_bits_size(size, POINTER_SIZE);
    return mallocz(gc_bits_size);
}

type_param_t *type_formal_new(char *literal) {
    type_param_t *t = NEW(type_param_t);
    t->ident = literal;
    return t;
}

type_alias_t *type_alias_new(char *literal, char *import_as) {
    type_alias_t *t = NEW(type_alias_t);
    t->ident = literal;
    t->import_as = import_as;
    return t;
}

bool type_need_gc(type_t t) {
    if (t.in_heap) {
        return true;
    }
    return false;
}

rtype_t *rtype_push(rtype_t rtype) {
    uint64_t index = ct_rtype_list->length;
    ct_list_push(ct_rtype_list, &rtype);

    ct_rtype_size += sizeof(rtype_t);
    ct_rtype_size += calc_gc_bits_size(rtype.size, POINTER_SIZE);
    ct_rtype_size += (rtype.element_count * sizeof(uint64_t));
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
uint64_t rtype_out_size(rtype_t *rtype, uint8_t ptr_size) {
    assert(rtype);

    if (rtype->in_heap) {
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
        uint64_t item_size = type_sizeof(p->type);
        offset = align_up(offset, item_size);
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


int64_t type_tuple_offset(type_tuple_t *t, uint64_t index) {
    uint64_t offset = 0;
    for (int i = 0; i < t->elements->length; ++i) {
        type_t *typedecl = ct_list_value(t->elements, i);
        uint64_t item_size = type_sizeof(*typedecl);
        offset = align_up(offset, item_size);

        if (i == index) {
            // found
            return offset;
        }
        offset += item_size;
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

/**
 * 生成用于 gc 的 rtype
 * @param count
 * @param ...
 * @return
 */
rtype_t *gc_rtype(type_kind kind, uint32_t count, ...) {
    // count = 1 = 8byte = 1 gc_bit 初始化 gc bits
    char *str = itoa(kind);

    va_list valist;
    /* 初始化可变参数列表 */
    va_start(valist, count);
    for (int i = 0; i < count; i++) {
        type_kind arg_kind = va_arg(valist, type_kind);
        str = str_connect(str, itoa(arg_kind));
    }
    va_end(valist);

    uint64_t hash = hash_string(str);
    rtype_t *rtype = table_get(rt_rtype_table, itoa(hash));
    if (rtype) {
        return rtype;
    }

    rtype = NEW(rtype_t);
    rtype->size = count * POINTER_SIZE;
    if (rtype->size == 0) {
        rtype->size = type_kind_sizeof(kind);
    }

    rtype->kind = kind;
    rtype->last_ptr = 0; // 最后一个包含指针的字节数, 使用该字段判断是否包含指针
    rtype->gc_bits = malloc_gc_bits(count * POINTER_SIZE);

    /* 初始化可变参数列表 */
    va_start(valist, count);
    for (int i = 0; i < count; i++) {
        type_kind arg_kind = va_arg(valist, type_kind);
        if (arg_kind == TYPE_GC_SCAN) {
            bitmap_set(rtype->gc_bits, i);
            rtype->last_ptr = (i + 1) * POINTER_SIZE;
        } else if (arg_kind == TYPE_GC_NOSCAN) {
//            bitmap_clear(rtype.gc_bits, i);
        } else {
            assertf(false, "gc rtype kind exception");
        }
    }
    va_end(valist);

    rtype->hash = hash;
    rtype->in_heap = kind_in_heap(kind);

    table_set(rt_rtype_table, itoa(rtype->hash), rtype);
    return rtype;
}

/**
 * 默认就是不 gc 数组
 * @param count
 * @return
 */
rtype_t *gc_rtype_array(type_kind kind, uint32_t length) {
    // 更简单的计算一下 hash 即可 array, len + scan 计算即可
    char *str = fixed_sprintf("%d_%lu_%lu", kind, length, TYPE_POINTER);
    uint64_t hash = hash_string(str);
    rtype_t *rtype = table_get(rt_rtype_table, itoa(hash));
    if (rtype) {
        return rtype;
    }

    // count = 1 = 8byte = 1 gc_bit 初始化 gc bits
    rtype = NEW(rtype_t);
    rtype->size = length * POINTER_SIZE;
    rtype->kind = kind;
    rtype->last_ptr = 0; // 最后一个包含指针的字节数, 使用该字段判断是否包含指针
    rtype->gc_bits = malloc_gc_bits(length * POINTER_SIZE);
    rtype->hash = hash;
    rtype->in_heap = true;
    table_set(rt_rtype_table, itoa(rtype->hash), rtype);
    return rtype;
}


bool type_union_compare(type_union_t *left, type_union_t *right) {
    // 因为 any 的作用域大于非 any 的作用域
    if (right->any && !left->any) {
        return false;
    }

    // 创建一个标记数组，用于标记left中的类型是否已经匹配
    // 遍历right中的类型，确保每个类型都存在于left中
    for (int i = 0; i < right->elements->length; ++i) {
        type_t *right_type = ct_list_value(right->elements, i);

        // 检查right_type是否存在于left中
        bool type_found = false;
        for (int j = 0; j < left->elements->length; ++j) {
            type_t *left_type = ct_list_value(left->elements, j);
            if (type_compare(*left_type, *right_type)) {
                type_found = true;
                break;
            }
        }

        // 如果right_type不存在于left中，则释放内存并返回false
        if (!type_found) {
            return false;
        }
    }

    return true;
}

/**
 * 比较前都已经还原为原始类型了
 * @param left
 * @param right
 * @return
 */
bool type_compare(type_t left, type_t right) {
    assertf(left.status == REDUCTION_STATUS_DONE && right.status == REDUCTION_STATUS_DONE,
            "type not origin, left: '%s', right: '%s'",
            type_kind_str[left.kind],
            type_kind_str[right.kind]);

    assertf(left.kind != TYPE_UNKNOWN && right.kind != TYPE_UNKNOWN, "type unknown cannot checking");

    if (is_gen_any(left) || is_gen_any(right)) {
        return true;
    }

    if (cross_kind_trans(left.kind) != cross_kind_trans(right.kind)) {
        return false;
    }

    if (left.kind == TYPE_UNION) {
        type_union_t *left_union_decl = left.union_;
        type_union_t *right_union_decl = right.union_;

        if (left_union_decl->any) {
            return true;
        }

        return type_union_compare(left_union_decl, right_union_decl);
    }

    if (left.kind == TYPE_MAP) {
        type_map_t *left_map_decl = left.map;
        type_map_t *right_map_decl = right.map;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }

        return true;
    }

    if (left.kind == TYPE_SET) {
        type_set_t *left_decl = left.set;
        type_set_t *right_decl = right.set;

        if (!type_compare(left_decl->element_type, right_decl->element_type)) {
            return false;
        }

        return true;
    }

    if (left.kind == TYPE_LIST) {
        type_list_t *left_list_decl = left.list;
        type_list_t *right_list_decl = right.list;
        return type_compare(left_list_decl->element_type, right_list_decl->element_type);
    }

    if (left.kind == TYPE_TUPLE) {
        type_tuple_t *left_tuple = left.tuple;
        type_tuple_t *right_tuple = right.tuple;

        if (left_tuple->elements->length != right_tuple->elements->length) {
            return false;
        }
        for (int i = 0; i < left_tuple->elements->length; ++i) {
            type_t *left_item = ct_list_value(left_tuple->elements, i);
            type_t *right_item = ct_list_value(right_tuple->elements, i);
            if (!type_compare(*left_item, *right_item)) {
                return false;
            }
        }
        return true;
    }

    if (left.kind == TYPE_FN) {
        type_fn_t *left_type_fn = left.fn;
        type_fn_t *right_type_fn = right.fn;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        // TODO rest 支持
        if (left_type_fn->param_types->length != right_type_fn->param_types->length) {
            return false;
        }

        for (int i = 0; i < left_type_fn->param_types->length; ++i) {
            type_t *left_formal_type = ct_list_value(left_type_fn->param_types, i);
            type_t *right_formal_type = ct_list_value(right_type_fn->param_types, i);
            if (!type_compare(*left_formal_type, *right_formal_type)) {
                return false;
            }
        }
        return true;
    }

    if (left.kind == TYPE_STRUCT) {
        type_struct_t *left_struct = left.struct_;
        type_struct_t *right_struct = right.struct_;
        if (left_struct->properties->length != right_struct->properties->length) {
            return false;
        }

        for (int i = 0; i < left_struct->properties->length; ++i) {
            struct_property_t *left_property = ct_list_value(left_struct->properties, i);
            struct_property_t *right_property = ct_list_value(right_struct->properties, i);

            // key 比较
            if (!str_equal(left_property->key, right_property->key)) {
                return false;
            }

            // type 比较
            if (!type_compare(left_property->type, right_property->type)) {
                return false;
            }
        }

        return true;
    }

    if (left.kind == TYPE_POINTER) {
        type_t left_pointer = left.pointer->value_type;
        type_t right_pointer = right.pointer->value_type;
        return type_compare(left_pointer, right_pointer);
    }

    return true;
}

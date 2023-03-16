#include "type.h"
#include "assertf.h"
#include "helper.h"
#include "bitmap.h"

static reflect_type_t rtype_base(type_kind kind) {
    uint32_t hash = hash_string(itoa(kind));
    reflect_type_t rtype = {
            .size = type_kind_sizeof(kind),  // 单位 byte
            .hash = hash,
            .last_ptr = 0,
            .kind = TYPE_BOOL,
    };
    rtype.gc_bits = malloc_gc_bits(rtype.size);

    return rtype;
}

static reflect_type_t rtype_int(type_decl_int_t t) {
    return rtype_base(TYPE_INT);
}

static reflect_type_t rtype_float(type_decl_float_t t) {
    return rtype_base(TYPE_FLOAT);
}

static reflect_type_t rtype_bool(type_decl_bool_t t) {
    return rtype_base(TYPE_BOOL);
}

/**
 * hash = type_kind
 * @param t
 * @return
 */
static reflect_type_t rtype_string(typedecl_string_t *t) {
    uint32_t hash = hash_string(itoa(TYPE_STRING));
    reflect_type_t rtype = {
            .size = t->count,  // count 表示字符串的程度，单位已经是 byte 了
            .hash = hash,
            .last_ptr = 0,
            .kind = TYPE_STRING,
            .gc_bits = malloc_gc_bits(t->count),
    };
    return rtype;
}

/**
 * hash = type_kind + element_type_hash
 * @param t
 * @return
 */
static reflect_type_t rtype_list(typedecl_list_t *t) {
    type_t element_type = t->type;
    reflect_type_t element_rtype = reflect_type(element_type);

    char *str = fixed_sprintf("%d_%lu", TYPE_LIST, element_rtype.hash);
    uint32_t hash = hash_string(str);
    reflect_type_t rtype = {
            .size =  sizeof(memory_list_t),
            .hash = hash,
            .last_ptr = POINTER_SIZE,
            .kind = TYPE_LIST,
    };
    // 计算 gc_bits
    byte *gc_bits = malloc_gc_bits(rtype.size);
    bitmap_set(gc_bits, 0);
    rtype.gc_bits = gc_bits;

    return rtype;
}

/**
 * hash = type_kind + count + element_type_hash
 * @param t
 * @return
 */
static reflect_type_t rtype_array(typedecl_array_t *t) {
    type_t element_type = t->type;
    reflect_type_t element_rtype = reflect_type(element_type);
    uint64_t element_size = type_sizeof(element_type);

    char *str = fixed_sprintf("%d_%lu_%lu", TYPE_ARRAY, t->count, element_rtype.hash);
    uint32_t hash = hash_string(str);
    reflect_type_t rtype = {
            .size = element_size * t->count,
            .hash = hash,
            .kind = TYPE_ARRAY,
    };
    rtype.gc_bits = malloc_gc_bits(rtype.size);
    bool need_gc = type_need_gc(element_type);
    if (need_gc) {
        rtype.last_ptr = element_size * t->count;

        // need_gc 暗示了 8byte 对齐了
        for (int i = 0; i < rtype.size / POINTER_SIZE; ++i) {
            bitmap_set(rtype.gc_bits, i);
        }
    }

    return rtype;
}

/**
 * TODO 未实现
 * @param t
 * @return
 */
static reflect_type_t rtype_map(typedecl_map_t *t) {
    reflect_type_t rtype = {};
    return rtype;
}

/**
 * TODO 未实现
 * @param t
 * @return
 */
static reflect_type_t rtype_set(typedecl_set_t *t) {
    reflect_type_t rtype = {};
    return rtype;
}

/**
 * TODO 未实现
 * @param t
 * @return
 */
static reflect_type_t rtype_tuple(typedecl_tuple_t *t) {
    reflect_type_t rtype = {};
    return rtype;
}

/**
 * rtype any 真的需要实现吗？
 * hash = type_kind
 * @param t
 * @return
 */
static reflect_type_t rtype_any(typedecl_any_t *t) {
    uint32_t hash = hash_string(itoa(TYPE_ANY));

    reflect_type_t rtype = {
            .size = POINTER_SIZE * 2, // rtype + value(并不知道 value 的类型)
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
static reflect_type_t rtype_fn(typedecl_fn_t *t) {
    char *str = itoa(TYPE_FN);
    reflect_type_t return_rtype = reflect_type(t->return_type);
    str = str_connect(str, itoa(return_rtype.hash));
    for (int i = 0; i < t->formals_count; ++i) {
        reflect_type_t formal_type = reflect_type(t->formals_types[i]);
        str = str_connect(str, itoa(formal_type.hash));
    }
    reflect_type_t rtype = {
            .size = POINTER_SIZE,
            .hash = hash_string(str),
            .kind = TYPE_FN,
            .last_ptr = 0,
            .gc_bits = malloc_gc_bits(POINTER_SIZE)
    };
    return rtype;
}

/**
 * hash = type_kind + property type hash
 * @param t
 * @return
 */
static reflect_type_t rtype_struct(typedecl_struct_t *t) {
    char *str = itoa(TYPE_STRUCT);
    uint offset = 0;
    uint max = 0;
    uint need_gc_count = 0;
    uint16_t need_gc_offsets[UINT16_MAX] = {0};
    // 记录需要 gc 的 property 的
    for (int i = 0; i < t->count; ++i) {
        typedecl_struct_property_t property = t->properties[i];
        uint16_t item_size = type_sizeof(property.type);
        if (item_size > max) {
            max = item_size;
        }
        // 按 offset 对齐
        offset = align(offset, item_size);
        // 计算 rtype
        reflect_type_t rtype = reflect_type(property.type);
        str = str_connect(str, itoa(rtype.hash));
        bool need_gc = type_need_gc(property.type);
        if (need_gc) {
            need_gc_offsets[need_gc_count++] = offset;
        }
        offset += item_size;
    }
    uint size = align(offset, max);


    reflect_type_t rtype = {
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


uint8_t type_kind_sizeof(type_kind t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_INT8:
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
uint16_t type_sizeof(type_t t) {
    return type_kind_sizeof(t.kind);
}


type_t type_with_point(type_t t, uint8_t point) {
    type_t result;
    result.is_origin = t.is_origin;
    result.point = point;
    return result;
}

type_t type_base_new(type_kind kind) {
    type_t result = {
            .is_origin = true,
            .kind = kind,
            .value_decl = 0,
    };

    return result;
}

type_t type_new(type_kind kind, void *value) {
    type_t result = {
            .kind = kind,
            .value_decl = value
    };
    return result;
}

reflect_type_t reflect_type(type_t t) {
    reflect_type_t rtype;
    switch (t.kind) {
        case TYPE_INT:
            rtype = rtype_int(t.int_decl);
            break;
        case TYPE_FLOAT:
            rtype = rtype_float(t.float_decl);
            break;
        case TYPE_BOOL:
            rtype = rtype_bool(t.bool_decl);
            break;
        case TYPE_STRING:
            rtype = rtype_string(t.string_decl);
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
            assertf(false, "cannot reflect type kind=%d", t.kind);
    }

    if (!table_exist(rtype_table, itoa(rtype.hash))) {
        // 添加到 rtypes 并得到 index(rtype 是值传递并 copy)
        uint64_t index = rtypes_push(rtype);
        // 将 index 添加到 table
        table_set(rtype_table, itoa(rtype.hash), (void *) index);
        rtypes[index].index = index; // 索引反填
        rtype.index = index;
    }

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

typedecl_ident_t *type_decl_ident_new(char *literal) {
    typedecl_ident_t *t = NEW(typedecl_ident_t);
    t->literal = literal;
    return t;
}

bool type_need_gc(type_t t) {
    if (t.point == 0) {
        return false;
    }
    if (t.kind == TYPE_BOOL ||
        t.kind == TYPE_FLOAT ||
        t.kind == TYPE_INT ||
        t.kind == TYPE_INT8 ||
        t.kind == TYPE_INT16 ||
        t.kind == TYPE_INT32 ||
        t.kind == TYPE_INT64) {
        return false;
    }

    return true;
}

uint64_t rtypes_push(reflect_type_t rtype) {
    uint64_t index = rtype_count++;
    rtypes[index] = rtype;

    rtype_size += sizeof(reflect_type_t);
    rtype_size += calc_gc_bits_size(rtype.size);

    return index;
}

/**
 * compile time
 * @param t
 * @return
 */
uint find_rtype_index(type_t t) {
    reflect_type_t rtype = reflect_type(t);
    assertf(rtype.hash, "type reflect failed");
    uint64_t index = table_get(rtype_table, itoa(rtype.hash));
    assertf(index, "notfound index by rtype_table,hass=%d", rtype.hash);
    return index;
}


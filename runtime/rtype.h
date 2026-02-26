#ifndef RUNTIME_RTYPE_H
#define RUNTIME_RTYPE_H

#include "utils/bitmap.h"
#include "utils/custom_links.h"
#include "utils/sc_map.h"
#include "utils/type.h"

extern struct sc_map_64v rt_rtype_map;

// GC_RTYPE(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
extern rtype_t linkco_rtype; // rtype 预生成

// 添加 hash table gc_rtype(TYPE_UINT8, 0);
extern rtype_t string_element_rtype;

// 添加 hash table gc_rtype(TYPE_UINT64, 0);
extern rtype_t uint64_rtype;

// 添加 hash table  (GC_RTYPE(TYPE_STRING, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);)
extern rtype_t string_rtype;

// 默认会被 rtypes_deserialize 覆盖为 compile 中的值
extern rtype_t string_ref_rtype;

// GC_RTYPE(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
extern rtype_t errort_trace_rtype;

extern rtype_t throwable_rtype;

extern rtype_t errort_rtype;

// GC_RTYPE(TYPE_GC_ENV, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
extern rtype_t envs_rtype;

// GC_RTYPE(TYPE_GC_ENV_VALUE, 1, TYPE_GC_SCAN)
extern rtype_t env_value_rtype;

//  GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN)
extern rtype_t std_arg_rtype;

// GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
extern rtype_t os_env_rtype;

// GC_RTYPE(TYPE_VEC, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN)
extern rtype_t vec_rtype;

extern rtype_t fn_rtype;

// 默认是 uint8[8] == uint8* , 因为指针占用 8 byte
#define GC_RTYPE(_kind, _count, ...) ({                                   \
    assert(_count <= 32);                                                 \
    uint64_t _gc_bits = 0;                                                \
    uint64_t _size = 0;                                                   \
    uint64_t _last_ptr = 0;                                               \
    if (_count > 0) {                                                     \
        uint8_t _bit_values[_count] = {__VA_ARGS__};                      \
        for (int _i = 0; _i < _count; _i++) {                             \
            if (_bit_values[_i]) {                                        \
                _gc_bits |= 1 << _i;                                      \
                _last_ptr = (_i + 1) * POINTER_SIZE;                      \
            }                                                             \
        }                                                                 \
    }                                                                     \
    _size = _count * POINTER_SIZE;                                        \
    if (_size == 0) _size = type_kind_sizeof(_kind);                      \
    uint64_t _stack_size = _size;                                         \
    if (kind_in_heap(_kind)) _stack_size = POINTER_SIZE;                  \
    uint64_t _hash = ((uint64_t) _kind << 56) | (_size << 32) | _gc_bits; \
    (rtype_t){                                                            \
            .gc_heap_size = _size,                                           \
            .storage_size = _stack_size,                                    \
            .kind = _kind,                                                \
            .last_ptr = _last_ptr,                                        \
            .malloc_gc_bits_offset = -1,                                  \
            .hashes_offset = -1,                                          \
            .gc_bits = _gc_bits,                                          \
            .hash = _hash,                                                \
    };                                                                    \
})

/**
 * TODO struct/arr 计算异常, 应该采用递归的方式正确计算
 * @param element_rtype
 * @param length
 * @return
 */
static inline rtype_t rti_rtype_array(rtype_t *element_rtype, uint64_t length) {
    assert(element_rtype);

    rtype_t rtype = {
            .gc_heap_size = element_rtype->storage_size * length,
            .hash = 0, // runtime 生成的没有 hash 值，不需要进行 hash 定位
            .kind = TYPE_ARR,
            .length = length,
            .malloc_gc_bits_offset = -1,
            .hashes_offset = -1,
    };

    rtype.last_ptr = element_rtype->last_ptr > 0; // 根据 0 和 1 在 heap_arena_bits_set 进行特殊处理

    return rtype;
}

static inline void builtin_rtype_init() {
    // 初始化协程结构体 rtype
    linkco_rtype = GC_RTYPE(TYPE_STRUCT, 7, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_SCAN,
                            TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);

    // 初始化字符串元素 rtype
    string_element_rtype = GC_RTYPE(TYPE_UINT8, 0);
    sc_map_put_64v(&rt_rtype_map, string_element_rtype.hash, &string_element_rtype);

    // 初始化 uint64 rtype
    uint64_rtype = GC_RTYPE(TYPE_UINT64, 0);
    sc_map_put_64v(&rt_rtype_map, uint64_rtype.hash, &uint64_rtype);

    // 初始化字符串 rtype
    string_rtype = GC_RTYPE(TYPE_STRING, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                            TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, string_rtype.hash, &string_rtype);

    string_ref_rtype = GC_RTYPE(TYPE_STRING, 5, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                                TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, string_rtype.hash, &string_rtype);

    // 测试 gc_bits
    assert(bitmap_test((uint8_t *) &string_rtype.gc_bits, 0) == 1);
    assert(bitmap_test((uint8_t *) &string_rtype.gc_bits, 1) == 0);
    assert(bitmap_test((uint8_t *) &string_rtype.gc_bits, 2) == 0);
    assert(bitmap_test((uint8_t *) &string_rtype.gc_bits, 3) == 0);

    // 初始化错误追踪 rtype
    errort_trace_rtype = GC_RTYPE(TYPE_STRUCT, 12,
                                  TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                                  TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                                  TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, errort_trace_rtype.hash, &errort_trace_rtype);

    throwable_rtype = GC_RTYPE(TYPE_INTERFACE, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, throwable_rtype.hash, &throwable_rtype);

    // 初始化错误 rtype
    errort_rtype = GC_RTYPE(TYPE_STRUCT, 6,
                            TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN,
                            TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, errort_rtype.hash, &errort_rtype);

    // 初始化环境变量 rtype
    envs_rtype = GC_RTYPE(TYPE_GC_ENV, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    sc_map_put_64v(&rt_rtype_map, envs_rtype.hash, &envs_rtype);

    // 初始化环境变量值 rtype
    env_value_rtype = GC_RTYPE(TYPE_GC_ENV_VALUE, 1, TYPE_GC_SCAN);
    sc_map_put_64v(&rt_rtype_map, env_value_rtype.hash, &env_value_rtype);

    // 初始化标准参数 rtype
    std_arg_rtype = GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);

    // 初始化系统环境变量 rtype
    os_env_rtype = GC_RTYPE(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);

    // 初始化向量 rtype
    vec_rtype = GC_RTYPE(TYPE_VEC, 5, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);

    // 初始化函数 rtype
    fn_rtype = GC_RTYPE(TYPE_FN, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
}


rtype_t *rt_find_rtype(int64_t hash);

void *rt_find_data(int64_t offset);

void *rt_find_strtable(int64_t offset);

#endif //RTYPE_H

#ifndef NATURE_TYPE_H
#define NATURE_TYPE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <float.h>
#include "slice.h"
#include "table.h"
#include "ct_list.h"

#ifndef POINTER_SIZE
#define POINTER_SIZE sizeof(void *)
#endif

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

typedef union {
    int64_t int_value;
    int64_t i64_value;
    int32_t i32_value;
    int16_t i16_value;
    int8_t i8_value;
    uint64_t uint_value;
    uint64_t u64_value;
    uint32_t u32_value;
    uint16_t u16_value;
    uint8_t u8_value;
    bool bool_value;
    double f64_value;
    float f32_value;
    void *ptr_value;
} value_casting;

typedef enum {
    REDUCTION_STATUS_UNDO = 1,
    REDUCTION_STATUS_DOING,
    REDUCTION_STATUS_DONE
} reduction_status_t;

typedef enum {
    // 基础类型
    TYPE_NULL = 1,
    TYPE_BOOL,

    TYPE_INT8,
    TYPE_UINT8, // uint8 ~ int 的顺序不可变，用于隐式类型转换
    TYPE_INT16,
    TYPE_UINT16,
    TYPE_INT32,
    TYPE_UINT32, // value=10
    TYPE_INT64,
    TYPE_UINT64,
    TYPE_INT, // value=15
    TYPE_UINT,

    TYPE_FLOAT32,
    TYPE_FLOAT, // f64
    TYPE_FLOAT64, // value = 5

    // 复合类型
    TYPE_STRING,
    TYPE_LIST,
    TYPE_ARRAY,
    TYPE_MAP, // value = 20
    TYPE_SET,
    TYPE_TUPLE,
    TYPE_STRUCT,
    TYPE_FN,
    TYPE_POINTER,


    // 编译时特殊临时类型,或者是没有理解是啥意思的类型(主要是编译器前端在使用这些类型)
    TYPE_CPTR, // 表示通用的指针，通常用于与 c 进行交互, 关键字 cptr
    TYPE_VOID, // 表示函数无返回值
    TYPE_UNKNOWN, // var a = 1, a 的类型就是 unknown
    TYPE_RAW_STRING, // c 语言中的 string, 目前主要用于 lir 中的 string imm
    TYPE_ALIAS, // 声明一个新的类型时注册的 type 的类型是这个
    TYPE_FORMAL, // type formal param type foo<f1, f2> = f1|f2, 其中 f1 就是一个 formal
    TYPE_SELF,
    TYPE_GEN,
    TYPE_UNION,

    // runtime 中使用的一种需要 gc 的 pointer base type 结构
    TYPE_GC,
    TYPE_GC_SCAN,
    TYPE_GC_NOSCAN,
    TYPE_GC_FN,
    TYPE_GC_ENV,
    TYPE_GC_ENV_VALUE,
    TYPE_GC_ENV_VALUES,
    TYPE_GC_UPVALUE
} type_kind;

static string type_kind_str[] = {
        [TYPE_GC] = "gc",
        [TYPE_GC_FN] = "runtime_fn",
        [TYPE_GC_ENV] = "env",
        [TYPE_GC_ENV_VALUE] = "env_value",
        [TYPE_GC_ENV_VALUES] = "env_values",
        [TYPE_GC_UPVALUE] = "upvalue",

        [TYPE_ARRAY] = "array",

        [TYPE_GEN] = "gen",
        [TYPE_UNION] = "union",

        [TYPE_STRING] = "string",
        [TYPE_RAW_STRING] = "raw_string",
        [TYPE_BOOL] = "bool",
        [TYPE_FLOAT] = "float",
        [TYPE_FLOAT32] = "f32",
        [TYPE_FLOAT64] = "f64",
        [TYPE_INT] = "int",
        [TYPE_UINT] = "uint",
        [TYPE_INT8] = "i8",
        [TYPE_INT16] = "i16",
        [TYPE_INT32] = "i32",
        [TYPE_INT64] = "i64",
        [TYPE_UINT8] = "u8",
        [TYPE_UINT16] = "u16",
        [TYPE_UINT32] = "u32",
        [TYPE_UINT64] = "u64",
        [TYPE_VOID] = "void",
        [TYPE_UNKNOWN] = "unknown",
        [TYPE_STRUCT] = "struct", // ast_struct_decl
        [TYPE_ALIAS] = "type_alias",
        [TYPE_LIST] = "list",
        [TYPE_MAP] = "map",
        [TYPE_SET] = "set",
        [TYPE_TUPLE] = "tuple",
        [TYPE_FN] = "fn",
        [TYPE_POINTER] = "pointer", // ptr<type>
        [TYPE_CPTR] = "cptr", // p<type>
        [TYPE_NULL] = "null",
        [TYPE_SELF] = "self",
};

// reflect type
// 所有的 type 都可以转化成该结构
typedef struct {
    uint64_t index; // 全局 index,在 linker 时 ct_reflect_type 的顺序会被打乱，需要靠 index 进行复原
    uint64_t size; //  无论存储在堆中还是栈中,这里的 size 都是该类型的实际的值的 size
    uint8_t in_heap; // 是否再堆中存储，如果数据存储在 heap 中，其在 stack,global,list value,struct value 中存储的都是 pointer 数据
    uint64_t hash; // 做类型推断时能够快速判断出类型是否相等
    uint64_t last_ptr; // 类型对应的堆数据中最后一个包含指针的字节数
    type_kind kind; // 类型的种类
    uint8_t *gc_bits; // 类型 bit 数据(按 uint8 对齐)

    uint8_t align; // struct/list 最终对齐的字节数
    uint16_t element_count; // struct/tuple 类型的长度
    uint64_t *element_hashes; // struct/tuple 每个类型的种类
} rtype_t;

// 类型描述信息 start
typedef int64_t type_int_t; // 左边是 nature 中的类型，右边是 c 中的类型

typedef double type_float_t;

typedef uint8_t type_bool_t;

/**
 *  custom_type a = 1, 此时 custom_type 就是 ident 类型
 *  custom_type 是一个自定义的 type, 其可能是 struct，也可能是 int 等等
 *  但是在类型描述上来说，其就是一个 ident
 */
typedef struct type_alias_t type_alias_t;

typedef struct type_formal_t type_formal_t;

typedef struct {
    bool any; // any gen
    list_t *elements; // type_t
} type_gen_t;

typedef struct {
    bool any;
    list_t *elements; // type_t*
} type_union_t;

typedef struct type_string_t type_string_t; // 类型不完全声明

typedef struct type_list_t type_list_t;

typedef struct type_pointer_t type_pointer_t;

typedef struct type_array_t type_array_t;

typedef struct type_map_t type_map_t;

typedef struct type_set_t type_set_t;

// (int, int, float)
typedef struct {
    list_t *elements; //  type_t
} type_tuple_t;

typedef struct type_struct_t type_struct_t; // 目前只有 string

typedef struct type_fn_t type_fn_t;

// 类型的描述信息，无论是否还原，类型都会在这里呈现
typedef struct type_t {
    union {
        void *value;
        type_list_t *list;
        type_array_t *array;
        type_map_t *map;
        type_set_t *set;
        type_tuple_t *tuple;
        type_struct_t *struct_;
        type_fn_t *fn;
        type_alias_t *alias; // 这个其实是自定义类型的 ident
        type_formal_t *formal;
        type_gen_t *gen; // type t0 = gen i8|i16|i32|i64|u8|u16|u32|u64
        type_pointer_t *pointer;
        type_union_t *union_;
    };
    type_kind kind;
    reduction_status_t status;
    int line;
    int column;
    bool in_heap; // 当前类型对应的值是否存储在 heap 中, list/array/map/set/tuple/struct/fn/any 默认存储在堆中
} type_t;

// list 如果自己持有一个动态的 data 呢？一旦 list 发生了扩容，那么需要新从新申请一个 data 区域
// 在 runtime_malloc 中很难描述这一段数据的类型？其实其本质就是一个 fixed array 结构，所以直接搞一个 array_t 更好描述 gc_bits
// 反而更好处理？
struct type_list_t {
    type_t element_type;
    uint64_t length;
};

// p<value_type>
struct type_pointer_t {
    type_t value_type;
};

/**
 * data 指针指向什么？首先 data 需要存在在 mheap 中，因为其值可能会变。
 * 那其应该执行 type_array_t 还是 type_array_t.data ? type_array_t 是编译时的概念，其根本不在 mheap 中
 * 那是 type_array_t.data? 同理 type_array_t 是编译时的数据，所以起根本就不会有 data 字段。
 * data 一定是指向堆内存中的数据，而数组在堆内存中的存储的数据是 malloc(sizeof(element_type) * count) !
 * data 就指向这个数据。string_t 并不需要关心这 string_t 这样的数据的释放等问题，gc 能够识别到，毕竟所在堆中的数据都是类型确定的
 * 这里的 string_t 按理说同样是元数据，所以不应该有 *data 这样的数据？无所谓了，最重要的是 count 数据
 *
 * 为什么要注释掉 data? 这仅仅是类型，不保存数据
 * 并且也不知道数据是如何如何存储在内存中
 */
struct type_string_t {
};

// type foo<formal> = fn(formal, int):formal
struct type_formal_t {
    char *ident;
};

struct type_alias_t {
    char *import_as; // 可能为 null (foo.bar)
    char *ident; // 类型名称 type my_int = int
    // 可以包含多个实际参数,实际参数由类型组成
    list_t *args; // type_t*
};

// 假设已经知道了数组元素的类型，又如何计算其是否为指针呢
// nature 反而比较简单，只要不是标量元素，其他元素其值一定是一个指针，包括 struct
// golang 中由于 struct 也是标量，所以其需要拆解 struct 才能填充元素是否为指针
// 假如 type_array_t 是编译时的数据，那编译时根本就不知道 *data 的值是多少！
// void* ptr =  malloc(sizeof(element_type) * count) // 数组初始化后最终会得到这样一份数据，这个数据将会存在的 var 中
struct type_array_t {
    uint64_t length;
//    type_t element_type;
    rtype_t element_rtype;
};

/**
 * set{int}
 */
struct type_set_t {
    type_t element_type;
};


/**
 * map{int:int}
 */
struct type_map_t {
    type_t key_type;
    type_t value_type;
};

// 这里应该用 c string 吗？ 衡量的标准是什么？标准是这个数据用在哪里,key 这种数据一旦确定就不会变化了,就将其存储在编译时就行了
typedef struct {
    type_t type;
    char *key;
    void *right; // ast_expr
} struct_property_t;

// 比如 type_struct_t 结构，如何能够将其传递到运行时，一旦运行时知道了该结构，编译时就不用费劲心机的在 lir 中传递该数据了？
// 可以通过连接器传递，但是其长度不规则,尤其是指针嵌套着指针的情况，所以将其序列化传递到 runtime 是很困难的事情
// golang 中的 gc_bits 也是不定长的数据，怎么传递？ map,slice 都还好说 可以在 runtime 里面生成
// 那 struct 呢？
struct type_struct_t {
    char *ident;
//    uint8_t count;
//    struct_property_t properties[UINT8_MAX]; // 属性列表,其每个元素的长度都是不固定的？有不固定的数组吗?
    list_t *properties; // struct_property_t
};

/**
 * type_fn_t 是什么样的结构？怎么存储在堆内存中?
 * NO, fn 并不存储在堆中，而是存储在 .text section 中。
 * type_fn_t f 指向的函数是 text 中的虚拟地址。f() 本质上是调用 call 指令进行 rip 指针的跳转
 * 所以其 reflect size = 8 且 gc_bits = 1
 * type_fn_t 在堆内存中仅仅是一个指针数据，指向堆内存, 这里的数据就是编译器前端的一个类型描述
 */
struct type_fn_t {
    char *name; // 可选的函数名称，并不是所有的函数类型都能改得到函数名称
    type_t return_type;
    list_t *formal_types; // type_t
    bool rest;
};
// 类型描述信息 end

// 类型对应的数据在内存中的存储形式 --- start
// 部分类型的数据(复合类型)只能在堆内存中存储

typedef struct {
    uint8_t *data;
    // 非必须，放进来做 rt_call 比较方便,不用再多传参数了
    // 内存优化时可以优化掉这个参数
    uint64_t element_rtype_hash;
    uint64_t capacity; // 预先申请的容量大小
    uint64_t length; // 实际占用的位置的大小
} n_list_t;

// 指针在 64位系统中占用的大小就是 8byte = 64bit
typedef addr_t n_pointer_t;

typedef struct {
    uint8_t *data; // [uint8_t] 标识 uint 数组
    uint64_t length; // 不包含 \0 的字符串长度
} n_string_t;

typedef uint8_t n_bool_t;

typedef uint8_t n_array_t; // 数组在内存中的变现形式就是 byte 列表

typedef int64_t n_int_t;
typedef int64_t n_int64_t;
typedef uint64_t n_uint_t;
typedef uint64_t n_cptr_t;
typedef uint32_t n_u32_t;
typedef uint16_t n_u16_t;

typedef double n_float_t;
typedef double n_f64_t;
typedef float n_f32_t;

typedef uint8_t n_struct_t; // 长度不确定

typedef uint8_t n_tuple_t; // 长度不确定

typedef struct {
    uint64_t *hash_table; // key 的 hash 表结构, 存储的值是 values 表的 index, 类型是 int64
    uint8_t *key_data;
    uint8_t *value_data;
    uint64_t key_rtype_hash; // key rtype index
    uint64_t value_rtype_hash;
    uint64_t length; // 实际的元素的数量
    uint64_t capacity; // 当达到一定的负载后将会触发 rehash
} n_map_t;

typedef struct {
    uint64_t *hash_table;
    uint8_t *key_data; // hash 冲突时进行检测使用
    uint64_t key_rtype_hash;
    uint64_t length;
    uint64_t capacity;
} n_set_t;

typedef struct {
    void *fn_data;
} n_fn_t; // 就占用一个指针大小

/**
 * 不能随便调换顺序，这是 gc 的顺序
 */
typedef struct {
    value_casting value;
    rtype_t *rtype;
} n_union_t;

typedef struct {
    n_string_t *msg;
    uint8_t has;
} n_errort;

// 所有的类型都会有一个唯一标识，从而避免类型的重复，不重复的类型会被加入到其中
// list 的唯一标识， 比如 [int] a, [int] b , [float] c   等等，其实只有一种类型
// 区分是否是同一种类型，就看 ct_reflect_type 中的 gc_bits 是否一致

rtype_t reflect_type(type_t t);

rtype_t ct_reflect_type(type_t t);

/**
 * 将 ct_rtypes 填入到 ct_rtypes 中并返回索引
 * @param rtype
 * @return
 */
rtype_t *rtype_push(rtype_t rtype);

uint64_t ct_find_rtype_hash(type_t t);

/**
 * 其对应的 var 在栈上占用的空间，而不是其在堆内存中的大小
 * @param t
 * @return
 */
uint8_t type_kind_sizeof(type_kind t);

/**
 * 类型在内存中(stack,array,var,reg) 中占用的大小,单位 byte
 * @param t
 * @return
 */
uint16_t type_sizeof(type_t t);

rtype_t rtype_base(type_kind kind);

rtype_t rtype_array(type_array_t *t);

/**
 * 基于当前 nature 中所有的栈中的数据都小于等于 8BYTE 的拖鞋之举
 * 后续 nature 一定会支持 symbol 或者 stack 中的一个 var 存储的对象大于 8byte
 *
 * stack 中存储的数据如果是一个指针，那么就需要 gc
 * 一些符合对象默认就是指针也是需要 gc 的
 * @param t
 * @return
 */
bool type_need_gc(type_t t);

type_t type_ptrof(type_t t);

type_formal_t *type_formal_new(char *literal);

type_alias_t *type_alias_new(char *literal, char *import_as);

type_kind to_gc_kind(type_kind kind);

bool type_compare(type_t left, type_t right);


/**
 * size 对应的 gc_bits 占用的字节数量
 * @param size
 * @return
 */
uint64_t calc_gc_bits_size(uint64_t size, uint8_t ptr_size);

/**
 * size 表示原始数据的程度，单位 byte
 * @param size
 * @return
 */
uint8_t *malloc_gc_bits(uint64_t size);

uint64_t rtype_out_size(rtype_t *rtype, uint8_t ptr_size);

uint64_t type_struct_offset(type_struct_t *s, char *key);

struct_property_t *type_struct_property(type_struct_t *s, char *key);

uint64_t type_tuple_offset(type_tuple_t *t, uint64_t index);

rtype_t *gc_rtype(type_kind kind, uint32_t count, ...);

rtype_t *gc_rtype_array(type_kind kind, uint32_t count);

/**
 * 一般标量类型其值默认会存储在 stack 中
 * 其他复合类型默认会在堆上创建，stack 中仅存储一个 ptr 指向堆内存。
 * 可以通过 kind 进行判断。
 * 后续会同一支持标量类型堆中存储，以及复合类型的栈中存储
 * @param type
 * @return
 */
static inline bool kind_in_heap(type_kind kind) {
    assert(kind > 0);
    return kind == TYPE_UNION ||
           kind == TYPE_STRING ||
           kind == TYPE_LIST ||
           kind == TYPE_ARRAY ||
           kind == TYPE_MAP ||
           kind == TYPE_SET ||
           kind == TYPE_TUPLE ||
           kind == TYPE_STRUCT ||
           kind == TYPE_FN;
}

static inline bool is_list_u8(type_t t) {
    if (t.kind != TYPE_LIST) {
        return false;
    }

    assert(t.list);

    if (t.list->element_type.kind != TYPE_UINT8) {
        return false;
    }

    return true;
}

static inline type_t type_basic_new(type_kind kind) {
    type_t result = {
            .status = REDUCTION_STATUS_DONE,
            .kind = kind,
            .value = 0,
    };

    result.in_heap = kind_in_heap(kind);

    return result;
}

static inline type_t type_new(type_kind kind, void *value) {
    type_t result = {
            .kind = kind,
            .value = value
    };
    return result;
}

static inline bool is_float(type_kind kind) {
    return kind == TYPE_FLOAT || kind == TYPE_FLOAT32 || kind == TYPE_FLOAT64;
}


static inline bool is_integer(type_kind kind) {
    return kind == TYPE_INT ||
           kind == TYPE_INT8 ||
           kind == TYPE_INT16 ||
           kind == TYPE_INT32 ||
           kind == TYPE_INT64 ||
           kind == TYPE_UINT ||
           kind == TYPE_UINT8 ||
           kind == TYPE_UINT16 ||
           kind == TYPE_UINT32 ||
           kind == TYPE_UINT64;
}

static inline bool is_gen_any(type_t type) {
    return type.kind == TYPE_GEN && type.gen->any;
}

static inline bool is_number(type_kind kind) {
    return is_float(kind) || is_integer(kind);
}


static inline bool can_type_casting(type_kind kind) {
    return is_number(kind) || kind == TYPE_BOOL;
}

static inline bool is_alloc_stack(type_t t) {
    return t.kind == TYPE_STRUCT || t.kind == TYPE_STRING;
}

/**
 * 可以直接使用 0 进行填充的值，通常就是简单类型
 * @param t
 * @return
 */
static inline bool is_zero_type(type_t t) {
    return is_integer(t.kind) ||
           is_float(t.kind) ||
           t.kind == TYPE_NULL ||
           t.kind == TYPE_BOOL ||
           t.kind == TYPE_UNION;
}

/**
 * 不需要进行类型还原的类型
 * @param t
 * @return
 */
static inline bool is_origin_type(type_t t) {
    return is_integer(t.kind) ||
           is_float(t.kind) ||
           t.kind == TYPE_CPTR ||
           t.kind == TYPE_NULL ||
           t.kind == TYPE_BOOL ||
           t.kind == TYPE_STRING ||
           t.kind == TYPE_VOID;
}

static inline bool is_reduction_type(type_t t) {
    return t.kind == TYPE_STRUCT
           || t.kind == TYPE_MAP
           || t.kind == TYPE_LIST
           || t.kind == TYPE_TUPLE
           || t.kind == TYPE_SET
           || t.kind == TYPE_FN
           || t.kind == TYPE_POINTER;
}

static inline bool is_qword_int(type_kind kind) {
    return kind == TYPE_INT64 || kind == TYPE_UINT64 || kind == TYPE_UINT || kind == TYPE_INT;
}

static inline bool union_type_contains(type_t union_type, type_t sub) {
    assert(union_type.kind == TYPE_UNION);
    assert(sub.kind != TYPE_UNION);

    if (union_type.union_->any) {
        return true;
    }

    for (int i = 0; i < union_type.union_->elements->length; ++i) {
        type_t *t = ct_list_value(union_type.union_->elements, i);
        if (type_compare(*t, sub)) {
            return true;
        }
    }

    return false;
}

/**
 * @param left
 * @param right
 * @return
 */
static inline type_t number_type_lift(type_kind left, type_kind right) {
    assertf(is_number(left) && is_number(right), "type lift kind must number");
    if (left == right) {
        return type_basic_new(left);
    }

    if (is_float(left) || is_float(right)) {
        return type_basic_new(TYPE_FLOAT64);
    }

    if (left >= right) {
        return type_basic_new(left);
    }

    return type_basic_new(right);
}

static bool integer_range_check(type_kind kind, int64_t i) {
    switch (kind) {
        case TYPE_UINT8:
            return i >= 0 && i <= UINT8_MAX;
        case TYPE_INT8:
            return i >= INT8_MIN && i <= INT8_MAX;
        case TYPE_UINT16:
            return i >= 0 && i <= UINT16_MAX;
        case TYPE_INT16:
            return i >= INT16_MIN && i <= INT16_MAX;
        case TYPE_UINT32:
            return i >= 0 && i <= UINT32_MAX;
        case TYPE_INT32:
            return i >= INT32_MIN && i <= INT32_MAX;
        case TYPE_UINT64:
            return i >= 0 && i <= UINT64_MAX;
        case TYPE_INT64:
            return i >= INT64_MIN && i <= INT64_MAX;
        default:
            return false;
    }
}

static bool float_range_check(type_kind kind, double f) {
    switch (kind) {
        case TYPE_FLOAT32:
            return f >= -FLT_MAX && f <= FLT_MAX;
        case TYPE_FLOAT64:
            return f >= -DBL_MAX && f <= DBL_MAX;
        default:
            return false;
    }
}

#endif //NATURE_TYPE_H

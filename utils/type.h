#ifndef NATURE_TYPE_H
#define NATURE_TYPE_H

#include <float.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ct_list.h"
#include "slice.h"
#include "table.h"

#ifndef POINTER_SIZE
#define POINTER_SIZE sizeof(void *)
#endif

#define THROWABLE_IDENT "throwable"

#define ALL_T_IDENT "all_t"
#define FN_T_IDENT "fn_t"
#define INTEGER_T_IDENT "integer_t"
#define FLOATER_T_IDENT "floater_t"

#define ASSIST_PREEMPT_YIELD_IDENT "assist_preempt_yield"
#define TLS_YIELD_SAFEPOINT_IDENT "tls_yield_safepoint"
#define GLOBAL_SAFEPOINT_IDENT "global_safepoint"

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

#define TYPE_GC_SCAN 1
#define TYPE_GC_NOSCAN 0

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
    REDUCTION_STATUS_DOING = 2,
    REDUCTION_STATUS_DOING2 = 3,
    REDUCTION_STATUS_DONE = 4
} reduction_status_t;

typedef enum {
    // 基础类型
    TYPE_NULL = 1,
    TYPE_BOOL = 2,

    TYPE_INT8 = 3,
    TYPE_UINT8 = 4, // uint8 ~ int 的顺序不可变，用于隐式类型转换
    TYPE_INT16 = 5,
    TYPE_UINT16 = 6,
    TYPE_INT32 = 7,
    TYPE_UINT32 = 8,
    TYPE_INT64 = 9,
    TYPE_INT = 9,
    TYPE_UINT64 = 10,
    TYPE_UINT = 10,

    TYPE_FLOAT32 = 11,
    TYPE_FLOAT64 = 12,
    TYPE_FLOAT = 12,

    // 复合类型
    TYPE_STRING = 13,
    TYPE_VEC = 14,
    TYPE_ARR = 15,
    TYPE_MAP = 16,
    TYPE_SET = 17,
    TYPE_TUPLE = 18,
    TYPE_STRUCT = 19,
    TYPE_FN = 20, // 具体的 fn 类型
    TYPE_CHAN = 21,
    TYPE_COROUTINE_T = 22,

    // 指针类型
    TYPE_PTR = 23, // ptr<T> 不允许为 null 的安全指针
    // 允许为 null 的指针， unsafe_ptr<type>, 可以通过 is 断言，可以通过 as 转换为 ptr<>。
    // 其在内存上，等于一个指针的占用大小
    TYPE_RAWPTR = 24, // rawptr<T> // 允许为 null 的不安全指针，也可能是错乱的悬空指针，暂时无法保证其正确性
    TYPE_ANYPTR = 25, // anyptr 没有具体类型，相当于 uintptr

    TYPE_UNION = 26,
    TYPE_INTERFACE = 27,
    TYPE_TAGGED_UNION = 28,
    TYPE_ENUM = 29,

    TYPE_VOID, // 表示函数无返回值
    TYPE_UNKNOWN, // var a = 1, a 的类型就是 unknown
    TYPE_RAW_STRING, // c 语言中的 string, 目前主要用于 lir 中的 string imm

    TYPE_FN_T, // 底层类型
    TYPE_INTEGER_T, // 底层类型
    TYPE_FLOATER_T, // 底层类型
    TYPE_ALL_T, // 通配所有类型

    TYPE_IDENT,


    TYPE_GC_ENV,
    TYPE_GC_ENV_VALUE,
    TYPE_GC_ENV_VALUES,
} type_kind;

typedef enum {
    TYPE_IDENT_DEF = 1,
    TYPE_IDENT_ALIAS,
    TYPE_IDENT_GENERICS_PARAM,
    TYPE_IDENT_BUILTIN, // int/float/vec/string...
    TYPE_IDENT_INTERFACE, // type.impls 部分专用
    TYPE_IDENT_TAGGER_UNION,
    TYPE_IDENT_UNKNOWN, // use 就是还不能确定是 type alias 还是 type def
} type_ident_kind;

static string type_kind_str[] = {
        [TYPE_GC_ENV] = "env",
        [TYPE_GC_ENV_VALUE] = "env_value",
        [TYPE_GC_ENV_VALUES] = "env_values",

        [TYPE_ARR] = "arr",

        [TYPE_UNION] = "union",
        [TYPE_TAGGED_UNION] = "tagged_union",

        [TYPE_STRING] = "string",
        [TYPE_RAW_STRING] = "raw_string",
        [TYPE_BOOL] = "bool",
        // [TYPE_FLOAT] = "float",
        [TYPE_FLOAT32] = "f32",
        [TYPE_FLOAT64] = "f64",
        // [TYPE_INT] = "int",
        // [TYPE_UINT] = "uint",
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
        [TYPE_IDENT] = "ident",
        [TYPE_COROUTINE_T] = "coroutine_t",
        [TYPE_CHAN] = "chan",
        [TYPE_VEC] = "vec",
        [TYPE_MAP] = "map",
        [TYPE_SET] = "set",
        [TYPE_TUPLE] = "tup",
        [TYPE_FN] = "fn",

        // 底层类型
        [TYPE_FN_T] = "fn_t",
        [TYPE_INTEGER_T] = "integer_t",
        [TYPE_FLOATER_T] = "floater_t",
        [TYPE_ALL_T] = "all_t",

        [TYPE_PTR] = "ptr", // ptr<type>
        [TYPE_RAWPTR] = "rawptr", // rawptr<type>
        [TYPE_ANYPTR] = "anyptr", // anyptr
        [TYPE_NULL] = "null",
        [TYPE_ENUM] = "enum",
};

typedef struct {
    int64_t name_offset;
    int64_t hash; // type hash
    int64_t offset; // offset of field
} rtype_field_t;

// reflect type
// 所有的 type 都可以转化成该结构
typedef struct {
    uint64_t ident_offset;
    uint64_t heap_size; // 无论存储在堆中还是栈中,这里的 size 都是该类型的实际的值的 size
    uint64_t stack_size;

    // pointer
    int64_t hash; // 做类型推断时能够快速判断出类型是否相等
    uint64_t last_ptr; // 类型对应的堆数据中最后一个包含指针的字节数
    type_kind kind; // 类型的种类

    // ct rtype 使用该字段
    int64_t malloc_gc_bits_offset; // NULL == -1

    // 类型 bit 数据(按 uint8 对齐), 在内存中分配空间, 如果为 NULL, 则直接使用 gc_bits
    // runtime GC_RTYPE 使用该字段(malloc_gc_bits_offset 中的数据无法再进一步修改)
    uint64_t gc_bits; // 从右到左，每个 bit 代表一个指针的位置，如果为 1，表示该位置是一个指针，需要 gc
    uint8_t align; // struct/list 最终对齐的字节数
    uint16_t length; // struct/tuple/array 类型的长度
    int64_t hashes_offset;
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

typedef struct type_param_t type_param_t;

typedef struct {
    bool any;
    bool nullable; // 通过 ? 声明的类型
    list_t *elements; // type_t
} type_union_t;

typedef struct {
    list_t *elements; // type_t
} type_interface_t;

typedef struct type_string_t type_string_t; // 类型不完全声明

typedef struct type_vec_t type_vec_t;

typedef struct type_coroutine_t type_coroutine_t;

typedef struct type_chan_t type_chan_t;

typedef struct type_ptr_t type_ptr_t, type_rawptr_t;

typedef struct type_array_t type_array_t;

typedef struct type_map_t type_map_t;

typedef struct type_set_t type_set_t;

// (int, int, float)
typedef struct {
    list_t *elements; // type_t
} type_tuple_t;

typedef struct type_struct_t type_struct_t; // 目前只有 string

typedef struct type_tagged_union_t type_tagged_union_t; // 目前只有 string

typedef struct type_enum_t type_enum_t;

typedef struct type_fn_t type_fn_t;

// 类型的描述信息，无论是否还原，类型都会在这里呈现
typedef struct type_t {
    char *import_as; // 可能为 null, foo.car 时， foo 就是 module_ident
    char *ident; // 当 type.kind == ALIAS/PARAM 时，此处缓存一下 alias/formal ident, 用于 dump error
    list_t *args; // type def 和 type impl 都存在 args，此时呈共用关系
    type_ident_kind ident_kind; // TYPE_ALIAS/TYPE_PARAM/TYPE_DEF

    union {
        void *value;
        type_vec_t *vec;
        type_chan_t *chan;
        type_array_t *array;
        type_map_t *map;
        type_set_t *set;
        type_tuple_t *tuple;
        type_struct_t *struct_;
        type_tagged_union_t *tagged_union;
        type_enum_t *enum_;
        type_fn_t *fn;
        type_ptr_t *ptr;
        type_union_t *union_;
        type_interface_t *interface;
    };
    type_kind kind;

    reduction_status_t status;

    // type_alias + args 进行 reduction 还原之前，将其参数缓存下来
    int line;
    int column;
    bool in_heap; // 当前类型对应的值是否存储在 heap 中, list/array/map/set/tuple/struct/fn/any 默认存储在堆中
} type_t;

/**
 * [int]
 * [int]
 * [int]
 */
struct type_vec_t {
    type_t element_type;
};

struct type_coroutine_t {
};

struct type_chan_t {
    type_t element_type;
};

// ptr<value_type>
struct type_ptr_t {
    type_t value_type;
};

/**
 * data 指针指向什么？首先 data 需要存在在 mheap 中，因为其值可能会变。
 * 那其应该执行 type_array_t 还是 type_array_t.data ? type_array_t 是编译时的概念，其根本不在 mheap 中
 * 那是 type_array_t.data? 同理 type_array_t 是编译时的数据，所以起根本就不会有 data 字段。
 * data 一定是指向堆内存中的数据，而数组在堆内存中的存储的数据是 malloc(sizeof(element_type) * count) !
 * data 就指向这个数据。string_t 并不需要关心这 string_t 这样的数据的释放等问题，gc
 * 能够识别到，毕竟所在堆中的数据都是类型确定的 这里的 string_t 按理说同样是元数据，所以不应该有 *data
 * 这样的数据？无所谓了，最重要的是 count 数据
 *
 * 为什么要注释掉 data? 这仅仅是类型，不保存数据
 * 并且也不知道数据是如何如何存储在内存中
 */
struct type_string_t {
};

// T a = 12
// [T] b = []
struct type_param_t {
    char *ident;
};

struct type_alias_t {
    char *import_as; // 可能为 null (foo.bar)
    char *ident; // 类型名称 type my_int = int

    // 可以包含多个实际参数,实际参数由类型组成, 当然实际参数也可能是 generic type, 比如 fn test<T>(alias<T>) 这种情况
    list_t *args; // type_t
};

// 假设已经知道了数组元素的类型，又如何计算其是否为指针呢
// nature 反而比较简单，只要不是标量元素，其他元素其值一定是一个指针，包括 struct
// golang 中由于 struct 也是标量，所以其需要拆解 struct 才能填充元素是否为指针
// 假如 type_array_t 是编译时的数据，那编译时根本就不知道 *data 的值是多少！
// void* ptr =  malloc(sizeof(element_type) * count) // 数组初始化后最终会得到这样一份数据，这个数据将会存在的 var 中
struct type_array_t {
    void *length_expr; // analyzer 之前是 ast_expr
    uint64_t length; // analyzer 完成后计算出来
    type_t element_type; // 这个必须要有呀
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

// 这种数据一旦确定就不会变化了,就将其存储在编译时就行了
typedef struct {
    type_t type;
    char *name;
    void *right; // ast_expr, 不允许 fn def
    int64_t align;
} struct_property_t;

// 比如 type_struct_t 结构，如何能够将其传递到运行时，一旦运行时知道了该结构，编译时就不用费劲心机的在 lir
// 中传递该数据了？ 可以通过连接器传递，但是其长度不规则,尤其是指针嵌套着指针的情况，所以将其序列化传递到 runtime
// 是很困难的事情 golang 中的 gc_bits 也是不定长的数据，怎么传递？ map,slice 都还好说 可以在 runtime 里面生成 那 struct
// 呢？
struct type_struct_t {
    char *ident;
    list_t *properties; // struct_property_t
};

typedef struct {
    char *tag;
    type_t type;
} tagged_union_element_t;

struct type_tagged_union_t {
    char *ident;
    list_t *elements; // tagged_union_element_t
};

/**
 * enum 成员属性
 * type color = enum {
 *     RED,      // value 默认从 0 开始
 *     GREEN,    // value = 1
 *     BLUE = 5, // 显式指定 value = 5
 * }
 * type option = enum {
 *     some(int),  // 带关联数据的变体
 *     none,       // 简单变体
 * }
 */
typedef struct {
    char *name; // 成员名称，如 RED, GREEN, BLUE
    void *value_expr; // ast_expr, 可选的值表达式
    int64_t value;
    type_t *payload_type; // 可选的关联数据类型，NULL 表示简单变体，TYPE_TUPLE 用于多字段
} enum_property_t;

/**
 * enum 类型
 * 支持两种形式:
 * 1. type color = enum { ... }        // 简单枚举，底层类型为 int
 * 2. type option = enum { some(T), none } // 带关联数据的枚举 (tagged union)
 */
struct type_enum_t {
    type_t element_type; // 底层类型（discriminant），默认为 TYPE_INT
    list_t *properties; // enum_property_t 列表
    bool has_payload; // true 表示存在带关联数据的变体
};

/**
 * type_fn_t 是什么样的结构？怎么存储在堆内存中?
 * NO, fn 并不存储在堆中，而是存储在 .text section 中。
 * type_fn_t f 指向的函数是 text 中的虚拟地址。f() 本质上是调用 call 指令进行 rip 指针的跳转
 * 所以其 reflect size = 8 且 gc_bits = 1
 * type_fn_t 在堆内存中仅仅是一个指针数据，指向堆内存, 这里的数据就是编译器前端的一个类型描述
 */
struct type_fn_t {
    char *fn_name; // 可选的函数名称，并不是所有的函数类型都能改得到函数名称
    type_t return_type;
    list_t *param_types; // type_t
    bool is_rest;
    bool is_errable;
    bool is_tpl;
    int self_kind;
};

// 类型描述信息 end

// 类型对应的数据在内存中的存储形式 --- start
// 部分类型的数据(复合类型)只能在堆内存中存储

typedef struct {
    uint8_t *data;
    int64_t length; // 实际占用的位置的大小
    int64_t capacity; // 预先申请的容量大小
    int64_t element_size;
    int64_t hash;
} n_vec_t, n_string_t;

// 通过 gc malloc 申请
typedef struct linkco_t linkco_t;

typedef struct {
    linkco_t *head;
    linkco_t *rear;
} waitq_t;

/**
 * buf 采用环形队列设计，添加 buf_len 记录当前元素数量， buf_cap 记录队列大小
 *
 */
typedef struct {
    n_vec_t *buf;
    waitq_t sendq;
    waitq_t recvq;

    int64_t buf_front; // 队列首元素对应的数组索引
    int64_t buf_rear;

    int64_t msg_size;
    pthread_mutex_t lock;
    bool closed;
    bool successful; // 默认是 true, 一旦变成 false 就永远是 false, 和 successful 对应
} n_chan_t;

typedef struct {
    value_casting value;
    void *ref;
} upvalue_t;

typedef struct {
    void **values;
    uint64_t length;
} envs_t;

typedef struct {
    envs_t *envs;
    addr_t fn_addr;
} n_fn_t;

// 指针在 64位系统中占用的大小就是 8byte = 64bit
typedef addr_t n_ptr_t, n_rawptr_t;

typedef uint8_t n_bool_t;

typedef uint8_t n_array_t; // 数组在内存中的变现形式就是 byte 列表

typedef addr_t n_anyptr_t;

typedef int64_t n_int_t;
typedef int64_t n_int64_t;
typedef uint64_t n_uint_t;
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

/**
 * 不能随便调换顺序，这是 gc rtype 的顺序
 */
typedef struct {
    value_casting value;
    rtype_t *rtype;
} n_union_t;

typedef struct {
    value_casting value; // need gc
    int64_t tag_hash;
} n_tagged_union_t;

typedef struct {
    value_casting value;
    int64_t *methods; // methods
    rtype_t *rtype;
    int64_t method_count;
} n_interface_t;

typedef struct {
    n_string_t *path;
    n_string_t *ident;
    n_int_t line;
    n_int_t column;
} n_trace_t;

typedef struct {
    n_string_t *msg;
    uint8_t panic;
} n_errort;

/**
 * 将 ct_rtypes 填入到 ct_rtypes 中并返回索引
 * @param rtype
 * @return
 */
rtype_t *rtype_push(rtype_t rtype);

/**
 * 其对应的 var 在栈上占用的空间，而不是其在堆内存中的大小
 * @param t
 * @return
 */
int64_t type_kind_sizeof(type_kind t);

int64_t type_struct_alignof(type_struct_t *s);

int64_t type_struct_sizeof(type_struct_t *s);

/**
 * 类型在内存中(stack,array,var,reg) 中占用的大小,单位 byte
 * @param t
 * @return
 */
int64_t type_sizeof(type_t t);

bool type_can_size(type_t t);

int64_t type_alignof(type_t t);

/**
 * 基于当前 nature 中所有的栈中的数据都小于等于 8BYTE 的拖鞋之举
 * 后续 nature 一定会支持 symbol 或者 stack 中的一个 var 存储的对象大于 8byte
 *
 * stack 中存储的数据如果是一个指针，那么就需要 gc
 * 一些符合对象默认就是指针也是需要 gc 的
 * @param t
 * @return
 */
bool type_is_pointer_heap(type_t t);

type_t type_ptrof(type_t t);

type_t type_rawptrof(type_t t);

type_param_t *type_param_new(char *literal);

type_alias_t *type_alias_new(char *literal, char *import_module_ident);

type_kind to_gc_kind(type_kind kind);

char *type_format(type_t t);

char *type_origin_format(type_t t);

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

uint64_t type_struct_offset(type_struct_t *s, char *key);

struct_property_t *type_struct_property(type_struct_t *s, char *key);

int64_t type_tuple_offset(type_tuple_t *t, uint64_t index);

/**
 * 一般标量类型其值默认会存储在 stack 中
 * 其他复合类型默认会在堆上创建，stack 中仅存储一个 ptr 指向堆内存。
 * @param type
 * @return
 */
static inline bool kind_in_heap(type_kind kind) {
    assert(kind > 0);
    return kind == TYPE_UNION || kind == TYPE_TAGGED_UNION || kind == TYPE_STRING || kind == TYPE_VEC ||
           kind == TYPE_MAP || kind == TYPE_SET || kind == TYPE_TUPLE || kind == TYPE_GC_ENV ||
           kind == TYPE_FN || kind == TYPE_COROUTINE_T || kind == TYPE_CHAN || kind == TYPE_INTERFACE;
}

static inline bool is_vec_u8(type_t t) {
    if (t.kind != TYPE_VEC) {
        return false;
    }

    assert(t.vec);

    if (t.vec->element_type.kind != TYPE_UINT8) {
        return false;
    }

    return true;
}

type_t type_kind_new(type_kind kind);

type_t type_new(type_kind kind, void *value);

static inline bool ident_is_generics_param(type_t *t) {
    if (t->kind != TYPE_IDENT) {
        return false;
    }

    return t->ident_kind == TYPE_IDENT_GENERICS_PARAM;
}

static inline bool type_is_ident(type_t *t) {
    if (t->kind != TYPE_IDENT) {
        return false;
    }

    return t->ident_kind == TYPE_IDENT_DEF || t->ident_kind == TYPE_IDENT_INTERFACE || t->ident_kind == TYPE_IDENT_TAGGER_UNION || t->ident_kind == TYPE_IDENT_UNKNOWN;
}

static inline type_t type_ident_new(char *ident, type_ident_kind kind) {
    type_t t = type_kind_new(TYPE_IDENT);
    t.status = REDUCTION_STATUS_UNDO;
    t.ident = ident;
    t.ident_kind = kind;
    t.args = NULL;
    return t;
}

static inline type_t type_floater_t_new() {
    type_t t = type_kind_new(TYPE_IDENT);
    t.status = REDUCTION_STATUS_UNDO;
    t.kind = TYPE_FLOAT;
    t.ident = FLOATER_T_IDENT;
    t.ident_kind = TYPE_IDENT_BUILTIN;
    t.args = NULL;
    return t;
}

static inline type_t type_array_new(type_kind element_type_kind, uint64_t length) {
    type_array_t *t = NEW(type_array_t);
    t->length = length;
    t->element_type = type_kind_new(element_type_kind);
    return type_new(TYPE_ARR, t);
}

static inline type_t interface_throwable() {
    return type_ident_new(THROWABLE_IDENT, TYPE_IDENT_INTERFACE);
}

static inline bool must_assign_value(type_t t) {
    if (t.kind == TYPE_FN || t.kind == TYPE_PTR || t.kind == TYPE_INTERFACE) {
        return true;
    }

    if (t.kind == TYPE_UNION && !t.union_->nullable) {
        return true;
    }

    return false;
}

static inline bool is_float(type_kind kind) {
    return kind == TYPE_FLOAT || kind == TYPE_FLOAT32 || kind == TYPE_FLOAT64;
}


static inline bool is_signed(type_kind kind) {
    return kind == TYPE_INT ||
           kind == TYPE_INT8 || kind == TYPE_INT16 || kind == TYPE_INT32 || kind == TYPE_INT64;
}

static inline bool is_unsigned(type_kind kind) {
    return kind == TYPE_UINT || kind == TYPE_UINT8 || kind == TYPE_UINT16 || kind == TYPE_UINT32 || kind == TYPE_UINT64;
}

static inline bool is_integer(type_kind kind) {
    return is_signed(kind) || is_unsigned(kind);
}

static inline bool is_any(type_t t) {
    if (t.kind != TYPE_UNION) {
        return false;
    }

    return t.union_->any;
}

static inline bool is_integer_or_anyptr(type_kind kind) {
    return is_integer(kind) || kind == TYPE_ANYPTR;
}


static inline bool is_number(type_kind kind) {
    return is_float(kind) || is_integer(kind);
}

static inline bool is_scala_type(type_t t) {
    return is_number(t.kind) || t.kind == TYPE_BOOL; // TODO test || t.kind == TYPE_ANYPTR;
}

static inline bool is_stack_ref_big_type(type_t t) {
    return t.kind == TYPE_STRUCT || t.kind == TYPE_ARR;
}

static inline bool is_stack_ref_big_type_kind(type_kind kind) {
    return kind == TYPE_STRUCT || kind == TYPE_ARR;
}

static inline bool is_stack_alloc_type(type_t t) {
    return is_number(t.kind) || t.kind == TYPE_BOOL || t.kind == TYPE_STRUCT || t.kind == TYPE_ARR;
}

static inline bool is_impl_builtin_type(type_kind kind) {
    return is_number(kind) || kind == TYPE_BOOL || kind == TYPE_MAP || kind == TYPE_SET || kind == TYPE_VEC || kind == TYPE_CHAN ||
           kind == TYPE_STRING || kind == TYPE_COROUTINE_T;
}

static inline bool is_stack_type(type_kind kind) {
    return is_number(kind) || kind == TYPE_BOOL || kind == TYPE_STRUCT || kind == TYPE_ARR || kind == TYPE_ENUM;
}

static inline bool is_stack_impl(type_kind kind) {
    return is_number(kind) || kind == TYPE_BOOL || kind == TYPE_ANYPTR || kind == TYPE_ARR || kind == TYPE_STRUCT || kind == TYPE_ENUM;
}

static inline bool is_heap_impl(type_kind kind) {
    return kind == TYPE_MAP || kind == TYPE_SET || kind == TYPE_VEC || kind == TYPE_CHAN ||
           kind == TYPE_STRING || kind == TYPE_COROUTINE_T || kind == TYPE_UNION;
}

static inline bool is_gc_alloc(type_kind kind) {
    return kind == TYPE_PTR ||
           kind == TYPE_RAWPTR ||
           kind == TYPE_ANYPTR ||
           kind == TYPE_MAP ||
           kind == TYPE_STRING ||
           kind == TYPE_SET ||
           kind == TYPE_VEC ||
           kind == TYPE_TUPLE ||
           kind == TYPE_COROUTINE_T ||
           kind == TYPE_CHAN ||
           kind == TYPE_UNION ||
           kind == TYPE_INTERFACE ||
           kind == TYPE_FN;
}

/**
 * 不需要进行类型还原的类型
 * @param t
 * @return
 */
static inline bool is_origin_type(type_t t) {
    return is_integer(t.kind) || is_float(t.kind) || t.kind == TYPE_ANYPTR || t.kind == TYPE_VOID ||
           t.kind == TYPE_NULL || t.kind == TYPE_BOOL ||
           t.kind == TYPE_STRING || t.kind == TYPE_FN_T || t.kind == TYPE_ALL_T;
}

static inline bool is_clv_default_type(type_t t) {
    return is_number(t.kind) || t.kind == TYPE_ANYPTR || t.kind == TYPE_RAWPTR || t.kind == TYPE_NULL ||
           t.kind == TYPE_BOOL ||
           t.kind == TYPE_VOID;
}

static inline bool is_struct_ptr(type_t t) {
    return t.kind == TYPE_PTR && t.ptr->value_type.kind == TYPE_STRUCT;
}

static inline bool is_struct_rawptr(type_t t) {
    return t.kind == TYPE_RAWPTR && t.ptr->value_type.kind == TYPE_STRUCT;
}

static inline bool is_map_set_key_type(type_kind kind) {
    return is_number(kind) || kind == TYPE_BOOL || kind == TYPE_STRING || kind == TYPE_PTR || kind == TYPE_RAWPTR ||
           kind == TYPE_ANYPTR || kind == TYPE_CHAN || kind == TYPE_STRUCT || kind == TYPE_ARR || kind == TYPE_ENUM;
}

static inline bool is_complex_type(type_t t) {
    return t.kind == TYPE_STRUCT || t.kind == TYPE_MAP || t.kind == TYPE_VEC || t.kind == TYPE_CHAN ||
           t.kind == TYPE_ARR ||
           t.kind == TYPE_TUPLE ||
           t.kind == TYPE_SET || t.kind == TYPE_FN || t.kind == TYPE_PTR || t.kind == TYPE_RAWPTR || t.kind == TYPE_ENUM;
}

static inline bool is_qword_int(type_kind kind) {
    return kind == TYPE_INT64 || kind == TYPE_UINT64 || kind == TYPE_UINT || kind == TYPE_INT;
}

/**
 * @param left
 * @param right
 * @return
 */
static inline type_t number_type_lift(type_kind left, type_kind right) {
    assertf(is_number(left) && is_number(right), "type lift kind must number");
    if (left == right) {
        return type_kind_new(left);
    }

    if (is_float(left) || is_float(right)) {
        return type_kind_new(TYPE_FLOAT64);
    }

    if (left >= right) {
        return type_kind_new(left);
    }

    return type_kind_new(right);
}

static inline bool integer_range_check(type_kind kind, int64_t i) {
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
            return (uint64_t) i >= 0 && (uint64_t) i <= UINT64_MAX;
        case TYPE_INT64:
            return i >= INT64_MIN && i <= INT64_MAX;
        default:
            return false;
    }
}

static inline bool float_range_check(type_kind kind, double f) {
    switch (kind) {
        case TYPE_FLOAT32:
            return f >= -FLT_MAX && f <= FLT_MAX;
        case TYPE_FLOAT64:
            return f >= -DBL_MAX && f <= DBL_MAX;
        default:
            return false;
    }
}

static inline type_t type_integer_t_new() {
    type_t t = type_kind_new(TYPE_IDENT);
    t.status = REDUCTION_STATUS_DONE;
    t.kind = TYPE_INT;
    t.ident = INTEGER_T_IDENT;
    t.ident_kind = TYPE_IDENT_BUILTIN;
    t.args = NULL;
    return t;
}


#endif // NATURE_TYPE_H

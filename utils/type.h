#ifndef NATURE_TYPE_H
#define NATURE_TYPE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include "slice.h"
#include "table.h"
#include "ct_list.h"

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

#define POINTER_SIZE sizeof(void*)
#define INT_SIZE sizeof(int64_t)

typedef uint8_t byte;


typedef union {
    int64_t int_value;
    int64_t i64_value;
    int32_t i32_value;
    int8_t i16_value;
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
    TYPE_FLOAT32,
    TYPE_FLOAT, // f64
    TYPE_FLOAT64, // value = 5

    TYPE_UINT8, // uint8 ~ int 的顺序不可变，用于隐式类型转换
    TYPE_INT8,
    TYPE_UINT16,
    TYPE_INT16,
    TYPE_UINT32, // value=10
    TYPE_INT32,
    TYPE_UINT64,
    TYPE_INT64,
    TYPE_UINT,
    TYPE_INT, // value=15

    // 复合类型
    TYPE_ANY, // value = 16
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
    TYPE_VOID, // 表示函数无返回值
    TYPE_UNKNOWN, // var a = 1, a 的类型就是 unknown
    TYPE_RAW_STRING, // c 语言中的 string, 目前主要用于 lir 中的 string imm
    TYPE_IDENT, // 声明一个新的类型时注册的 type 的类型是这个
    TYPE_SELF,

    // runtime 中使用的一种需要 gc 的 pointer base type 结构
    TYPE_GC,
    TYPE_GC_SCAN,
    TYPE_GC_NOSCAN,
} type_kind;

static string type_kind_string[] = {
        [TYPE_STRING] = "string",
        [TYPE_RAW_STRING] = "raw_string",
        [TYPE_BOOL] = "bool",
        [TYPE_FLOAT] = "float",
        [TYPE_FLOAT32] = "f32",
        [TYPE_FLOAT64] = "f64",
        [TYPE_INT] = "int",
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
        [TYPE_ANY] = "any",
        [TYPE_STRUCT] = "struct", // ast_struct_decl
        [TYPE_IDENT] = "type_ident", // char*
        [TYPE_LIST] = "list",
        [TYPE_MAP] = "map",
        [TYPE_SET] = "set",
        [TYPE_TUPLE] = "tuple",
        [TYPE_FN] = "fn",
        [TYPE_POINTER] = "pointer", // p<type>
        [TYPE_NULL] = "null",
        [TYPE_SELF] = "self",
};

// reflect type
// 所有的 type 都可以转化成该结构
typedef struct {
    uint64_t index; // 全局 index,在 linker 时 ct_reflect_type 的顺序会被打乱，需要靠 index 进行复原
    uint64_t size; //  无论存储在堆中还是栈中,这里的 size 都是该类型的实际的值的 size
    uint8_t in_heap; // 是否再堆中存储，如果数据存储在 heap 中，其在 stack,global,list value,struct value 中存储的都是 pointer 数据
    uint32_t hash; // 做类型推断时能够快速判断出类型是否相等
    uint64_t last_ptr; // 类型对应的堆数据中最后一个包含指针的字节数
    type_kind kind; // 类型的种类
    byte *gc_bits; // 类型 bit 数据(按 uint8 对齐)
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
typedef struct type_ident_t type_ident_t;

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

typedef struct type_any_t type_any_t;


// 通用类型声明,本质上和 any 没有什么差别,能够表示任何类型
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
        type_any_t *any;
        type_ident_t *ident;
        type_pointer_t *pointer;
    };
    type_kind kind;
    reduction_status_t status;
    bool in_heap; // 当前类型对应的值是否存储在 heap 中, list/array/map/set/tuple/struct/fn/any 默认存储在堆中
} type_t;

// list 如果自己持有一个动态的 data 呢？一旦 list 发生了扩容，那么需要新从新申请一个 data 区域
// 在 runtime_malloc 中很难描述这一段数据的类型？其实其本质就是一个 fixed array 结构，所以直接搞一个 array_t 更好描述 gc_bits
// 反而更好处理？
struct type_list_t {
    type_t element_type;
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

struct type_ident_t {
    string literal; // 类型名称 type my_int = int
};

// 假设已经知道了数组元素的类型，又如何计算其是否为指针呢
// nature 反而比较简单，只要不是标量元素，其他元素其值一定是一个指针，包括 struct
// golang 中由于 struct 也是标量，所以其需要拆解 struct 才能填充元素是否为指针
// 假如 type_array_t 是编译时的数据，那编译时根本就不知道 *data 的值是多少！
// void* ptr =  malloc(sizeof(element_type) * count) // 数组初始化后最终会得到这样一份数据，这个数据将会存在的 var 中
struct type_array_t {
    uint64_t length;
    type_t element_type;
    rtype_t element_rtype;
};

/**
 * set{int}
 */
struct type_set_t {
    type_t key_type;
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

/**
 * type_any_t 到底是类型，还是数据?, type_any_t 应该只是一个类型的描述，类似 type_array_t 一样
 * 比如 type_array_t 中有 element_type 和 count, 通过这两个数据我们能够知道其在内存中存储的数据的程度
 * 以及每一块内存存放了什么东西，但是并不知道数据实际存放在了哪里。
 *
 * 同理，如果 type_any_t 是类型的话，那就不知道存储的是什么,所以必须删除 void* value!
 * 但是给定 void* value,能够知道其内存中存储了什么东西！
 */
struct type_any_t {
//    uint64_t size; // 16byte,一部分存储原始值，一部分存储 element_rtype 数据！
    // element_rtype 和 value 都是变化的数据，所以类型描述信息中啥也没有，啥也不需要知道
//    rtype_t *element_rtype; // 这样的话 new any_t 太麻烦了
//    uint64_t rtype_index; // 这样定位更快
//    void *value;
};

// 类型描述信息 end

// 类型对应的数据在内存中的存储形式 --- start
// 部分类型的数据(复合类型)只能在堆内存中存储

typedef struct {
    byte *array_data;
    // 非必须，放进来做 rt_call 比较方便,不用再多传参数了
    // 内存优化时可以优化掉这个参数
    uint64_t element_rtype_index;
    uint64_t capacity; // 预先申请的容量大小
    uint64_t length; // 实际占用的位置的大小
} memory_list_t;

// 指针在 64位系统中占用的大小就是 8byte = 64bit
typedef addr_t memory_pointer_t;

typedef struct {
    byte *array_data;
    uint64_t length;
} memory_string_t;

typedef uint8_t memory_bool_t;

typedef byte memory_array_t; // 数组在内存中的变现形式就是 byte 列表

typedef int64_t memory_int_t;

typedef double memory_float_t;
typedef double memory_f64_t;
typedef float memory_f32_t;

typedef byte memory_struct_t; // 长度不确定

typedef byte memory_tuple_t; // 长度不确定

typedef struct {
    uint64_t *hash_table; // key 的 hash 表结构, 存储的值是 values 表的 index, 类型是 int64
    byte *key_data;
    byte *value_data;
    uint64_t key_index; // key rtype index
    uint64_t value_index;
    uint64_t length; // 实际的元素的数量
    uint64_t capacity; // 当达到一定的负载后将会触发 rehash
} memory_map_t;

typedef struct {
    uint64_t *hash_table;
    byte *key_data; // hash 冲突时进行检测使用
    uint64_t key_index;
    uint64_t length;
    uint64_t capacity;
} memory_set_t;

typedef struct {
    void *fn_data;
} memory_fn_t; // 就占用一个指针大小

typedef struct {
    value_casting value;
    rtype_t *rtype; // TODO use rtype index
} memory_any_t;


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
uint64_t rtypes_push(rtype_t rtype);

uint64_t ct_find_rtype_index(type_t t);

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


type_ident_t *typeuse_ident_new(string literal);

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
byte *malloc_gc_bits(uint64_t size);

uint64_t rtype_heap_out_size(rtype_t *rtype, uint8_t ptr_size);

uint64_t type_struct_offset(type_struct_t *s, char *key);

struct_property_t *type_struct_property(type_struct_t *s, char *key);

uint64_t type_tuple_offset(type_tuple_t *t, uint64_t index);

rtype_t gc_rtype(uint32_t count, ...);

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
    if (kind == TYPE_ANY ||
        kind == TYPE_STRING ||
        kind == TYPE_LIST ||
        kind == TYPE_ARRAY ||
        kind == TYPE_MAP ||
        kind == TYPE_SET ||
        kind == TYPE_TUPLE ||
        kind == TYPE_STRUCT ||
        kind == TYPE_FN) {
        return true;
    }
    return false;
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

static inline bool is_number(type_kind kind) {
    return is_float(kind) || is_integer(kind);
}

/**
 * 不需要进行类型还原的类型
 * @param t
 * @return
 */
static inline bool is_basic_type(type_t t) {
    return is_integer(t.kind) ||
           is_float(t.kind) ||
           t.kind == TYPE_NULL ||
           t.kind == TYPE_BOOL ||
           t.kind == TYPE_STRING ||
           t.kind == TYPE_ANY ||
           t.kind == TYPE_VOID;

}

static inline bool is_complex_type(type_t t) {
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

/**
 * int(-1) + uint(a) 时，int > uint, 宽度相同时， int 大于 uint
 * @param left
 * @param right
 * @return
 */
static inline type_t basic_type_select(type_kind left, type_kind right) {
    if (left >= right) {
        return type_basic_new(left);
    }


    return type_basic_new(right);
}

type_kind to_gc_kind(type_kind kind);

#endif //NATURE_TYPE_H

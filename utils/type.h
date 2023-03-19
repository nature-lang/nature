#ifndef NATURE_TYPE_H
#define NATURE_TYPE_H

#include <stdlib.h>
#include <stdint.h>
#include "slice.h"
#include "table.h"

#define POINTER_SIZE 8 // 单位 byte
#define INT_SIZE 8 // int 类型的占用的字节，随着平台的不同而不同

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

typedef uint8_t byte;

typedef enum {
    // 基础类型
    TYPE_NULL = 1,
    TYPE_BOOL,
    TYPE_FLOAT, // 默认 8BIT = DOUBLE
    TYPE_INT, // 默认 8BIT
    TYPE_INT8,
    TYPE_BYTE, // 字节类型？
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    // 复合类型
    TYPE_ANY,
    TYPE_STRING, // 10
    TYPE_LIST,
    TYPE_ARRAY,
    TYPE_MAP,
    TYPE_SET,
    TYPE_TUPLE,
    TYPE_STRUCT,
    TYPE_FN,

    // 编译时特殊临时类型,或者是没有理解是啥意思的类型(主要是编译器前端在使用这些类型)
    TYPE_VOID, // 表示函数无返回值
    TYPE_UNKNOWN, // 类型推断时没有推断出当前表达式的值
    TYPE_RAW_STRING, // c 语言中的 string, 目前主要用于 lir 中的 string imm
    TYPE_IDENT, // 声明一个新的类型时注册的 type 的类型是这个
} type_kind;

static string type_to_string[] = {
        [TYPE_STRING] = "string",
        [TYPE_RAW_STRING] = "string_RAW",
        [TYPE_BOOL] = "bool",
        [TYPE_FLOAT] = "float",
        [TYPE_INT] = "int",
        [TYPE_VOID] = "void",
        [TYPE_UNKNOWN] = "unknown",
        [TYPE_ANY] = "any",
        [TYPE_STRUCT] = "struct", // ast_struct_decl
        [TYPE_IDENT] = "decl", // char*
        [TYPE_ARRAY] = "array",
        [TYPE_MAP] = "map", // ast_map_decl
        [TYPE_FN] = "fn",
        [TYPE_NULL] = "null",
};

// reflect type
// 所有的 type 都可以转化成该结构
typedef struct {
    uint index; // 全局 index,在 linker 时 ct_reflect_type 的顺序会被打乱，需要靠 index 进行复原
    uint size; // 类型占用的 size,和 gc_bits 联合起来使用
    uint32_t hash; // 做类型推断时能够快速判断出类型是否相等
    uint last_ptr; // 类型对应的堆数据中最后一个包含指针的字节数
    uint8_t kind; // 类型的种类
    byte *gc_bits; // 类型 bit 数据(按 uint8 对齐)
} rtype_t;


// 类型描述信息 start
typedef int64_t type_decl_int_t; // 左边是 nature 中的类型，右边是 c 中的类型

typedef double type_decl_float_t;

typedef uint8_t type_decl_bool_t;

/**
 *  custom_type a = 1, 此时 custom_type 就是 ident 类型
 *  custom_type 是一个自定义的 type, 其可能是 struct，也可能是 int 等等
 *  但是在类型描述上来说，其就是一个 ident
 */
typedef struct typedecl_ident_t typedecl_ident_t;

typedef struct typedecl_string_t typedecl_string_t; // 类型不完全声明

typedef struct typedecl_list_t typedecl_list_t;

typedef struct typedecl_array_t typedecl_array_t;

typedef struct typedecl_map_t typedecl_map_t;

typedef struct typedecl_set_t typedecl_set_t;

typedef struct typedecl_tuple_t typedecl_tuple_t;

typedef struct typedecl_struct_t typedecl_struct_t; // 目前只有 string

typedef struct typedecl_fn_t typedecl_fn_t;

typedef struct typedecl_any_t typedecl_any_t;

// 通用类型声明,本质上和 any 没有什么差别,能够表示任何类型
typedef struct type_t {
    union {
//        type_decl_int_t int_decl;
//        type_decl_float_t float_decl;
//        type_decl_bool_t bool_decl;
//        typedecl_string_t *string_decl;
        typedecl_list_t *list_decl;
        typedecl_array_t *array_decl;
        typedecl_map_t *map_decl;
        typedecl_set_t *set_decl;
        typedecl_tuple_t *tuple_decl;
        typedecl_struct_t *struct_decl;
        typedecl_fn_t *fn_decl;
        typedecl_any_t *any_decl;
        typedecl_ident_t *ident_decl;
        void *value_decl;
    };
    type_kind kind;
    bool is_origin; // type a = int, type b = a，int is origin
    uint8_t point; // 指针等级, 如果等于0 表示非指针, 例如 int*** a; a 的 point 等于 3
} type_t;

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

typedef struct {
    byte *array_data;
    uint64_t length;
} memory_string_t;

typedef byte *memory_array_t; // 数组在内存中的变现形式就是 byte 列表

typedef int64_t memory_int_t;

typedef double memory_float_t;

typedef byte *memory_struct_t; // 长度不确定

typedef struct {
    void *fn_data;
} memory_fn_t; // 就占用一个指针大小

typedef struct {
    rtype_t *rtype;
    union {
        void *value;
        double float_value;
        bool bool_value;
        int64_t int_value;
    };
} memory_any_t;


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
struct typedecl_string_t {
//    int size; // 存储在 heap 中的数据的大小 count + ptr(data) 的长度
//    int count; // 字符串的长度(理论上也是可变的，和 list 的 count 一样，无法在类型声明阶段就明确,所以这个毫无意义！)
//    void *data; // 这里引用了什么？是 type_array_t 元数据还是 type_array_t 存储在 heap 中的数据？
};

struct typedecl_ident_t {
    string literal; // 类型名称 type my_int = int
};

// 假设已经知道了数组元素的类型，又如何计算其是否为指针呢
// nature 反而比较简单，只要不是标量元素，其他元素其值一定是一个指针，包括 struct
// golang 中由于 struct 也是标量，所以其需要拆解 struct 才能填充元素是否为指针
// 假如 type_array_t 是编译时的数据，那编译时根本就不知道 *data 的值是多少！
// void* ptr =  malloc(sizeof(element_type) * count) // 数组初始化后最终会得到这样一份数据，这个数据将会存在的 var 中
struct typedecl_array_t {
    uint64_t length;
    type_t element_type;
    rtype_t element_rtype;
};

// list 如果自己持有一个动态的 data 呢？一旦 list 发生了扩容，那么需要新从新申请一个 data 区域
// 在 runtime_malloc 中很难描述这一段数据的类型？其实其本质就是一个 fixed array 结构，所以直接搞一个 array_t 更好描述 gc_bits
// 反而更好处理？
struct typedecl_list_t {
//    int size; // 存储在 mheap 中的数据,count + cap + data, element_type 就不用存了，编译时确定之后就不会变了
    // 描述 list 类型是只需要一个 type, 比如 [int] !,且越界等行为在 compiler 都是无法判断的
    // 所以 count,capacity 都是没有意义的数据！
//    int capacity; // 预先申请的容量大小
//    int count; // 实际占用的位置的大小
    type_t element_type;
    // 类型描述信息根本就不能有值这个东西出现
//    void *data; // 引用的是一个 array 结构
};

/**
 * map{int:int}
 */
struct typedecl_map_t {
    int size; // TODO 完全没考虑过怎么搞
    type_t key_type;
    type_t value_type;
};

// 这里应该用 c string 吗？ 衡量的标准是什么？标准是这个数据用在哪里,key 这种数据一旦确定就不会变化了,就将其存储在编译时就行了
typedef struct {
    char *key;
//    int align_offset; // 对齐后起始地址,描述信息里面要这个也没啥用
    type_t type; // 这里存放的数据的大小是固定的吗？
} typedecl_struct_property_t;

// 比如 type_struct_t 结构，如何能够将其传递到运行时，一旦运行时知道了该结构，编译时就不用费劲心机的在 lir 中传递该数据了？
// 可以通过连接器传递，但是其长度不规则,尤其是指针嵌套着指针的情况，所以将其序列化传递到 runtime 是很困难的事情
// golang 中的 gc_bits 也是不定长的数据，怎么传递？ map,slice 都还好说 可以在 runtime 里面生成
// 那 struct 呢？
struct typedecl_struct_t {
    int size; // 占用的 mheap 的空间(按每个 property 对齐后的数据)
    int8_t count; // 属性的个数，用于遍历
    typedecl_struct_property_t properties[UINT16_MAX]; // 属性列表,其每个元素的长度都是不固定的？有不固定的数组吗?
};

/**
 * type_fn_t 是什么样的结构？怎么存储在堆内存中?
 * NO, fn 并不存储在堆中，而是存储在 .text section 中。
 * type_fn_t f 指向的函数是 text 中的虚拟地址。f() 本质上是调用 call 指令进行 rip 指针的跳转
 * 所以其 reflect size = 8 且 gc_bits = 1
 * type_fn_t 在堆内存中仅仅是一个指针数据，指向堆内存, 这里的数据就是编译器前端的一个类型描述
 */
struct typedecl_fn_t {
    int size; // 占用的堆内存的大小
    type_t return_type;
    type_t formals_types[UINT8_MAX];
    uint8_t formals_count;
    bool rest_param;
};

/**
 * type_any_t 到底是类型，还是数据?, type_any_t 应该只是一个类型的描述，类似 type_array_t 一样
 * 比如 type_array_t 中有 element_type 和 count, 通过这两个数据我们能够知道其在内存中存储的数据的程度
 * 以及每一块内存存放了什么东西，但是并不知道数据实际存放在了哪里。
 *
 * 同理，如果 type_any_t 是类型的话，那就不知道存储的是什么,所以必须删除 void* value!
 * 但是给定 void* value,能够知道其内存中存储了什么东西！
 */
struct typedecl_any_t {
    uint size; // 16byte,一部分存储原始值，一部分存储 element_rtype 数据！
    // element_rtype 和 value 都是变化的数据，所以类型描述信息中啥也没有，啥也不需要知道
//    rtype_t *element_rtype; // 这样的话 new any_t 太麻烦了
//    uint rtype_index; // 这样定位更快
//    void *value;
};

// 所有的类型都会有一个唯一标识，从而避免类型的重复，不重复的类型会被加入到其中
// list 的唯一标识， 比如 [int] a, [int] b , [float] c   等等，其实只有一种类型
// 区分是否是同一种类型，就看 ct_reflect_type 中的 gc_bits 是否一致
// TODO 先申请一个巨大的尺寸用着。等通用 dynamic list 结构开发吧。
rtype_t rtypes[UINT16_MAX];
uint64_t rtype_count;
uint64_t rtype_size; // 序列化后的 size
table_t *rtype_table; // 每当有一个新的类型产生，都会注册在该表中，值为 slice 的索引！

rtype_t reflect_type(type_t t);

rtype_t ct_reflect_type(type_t t);

rtype_t rt_reflect_type(type_t t);

/**
 * 将 rtypes 填入到 rtypes 中并返回索引
 * @param rtype
 * @return
 */
uint64_t rtypes_push(rtype_t rtype);

uint find_rtype_index(type_t t);

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

type_t type_with_point(type_t t, uint8_t point);

type_t type_base_new(type_kind kind);

type_t type_new(type_kind kind, void *value);

typedecl_ident_t *type_decl_ident_new(string literal);

/**
 * size 对应的 gc_bits 占用的字节数量
 * @param size
 * @return
 */
uint64_t calc_gc_bits_size(uint64_t size);

/**
 * size 表示原始数据的程度，单位 byte
 * @param size
 * @return
 */
byte *malloc_gc_bits(uint64_t size);

#endif //NATURE_TYPE_H

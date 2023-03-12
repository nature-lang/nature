#ifndef NATURE_TYPE_H
#define NATURE_TYPE_H

#include <stdlib.h>
#include <stdint.h>
#include "slice.h"
#include "table.h"

#define POINTER_SIZE 8; // 单位 byte
#define INT_SIZE 8; // int 类型的占用的字节，随着平台的不同而不同

typedef enum {
    // 基础类型
    TYPE_NULL = 1,
    TYPE_BOOL,
    TYPE_FLOAT, // 默认 8BIT = DOUBLE
    TYPE_INT, // 默认 8BIT
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    // 复合类型
    TYPE_ANY,
    TYPE_STRING,
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
    TYPE_DEF, // 声明一个新的类型时注册的 type 的类型是这个
} type_kind;

// 类型描述信息 start
typedef int64_t type_int_t; // 左边是 nature 中的类型，右边是 c 中的类型

typedef double type_float_t;

typedef uint8_t type_bool_t;

typedef struct type_string_t type_string_t; // 类型不完全声明

typedef struct type_list_t type_list_t;

typedef struct type_array_t type_array_t;

typedef struct type_map_t type_map_t;

typedef struct type_set_t type_set_t;

typedef struct type_tuple_t type_tuple_t;

typedef struct type_struct_t type_struct_t; // 目前只有 string

typedef struct type_fn_t type_fn_t;

typedef struct type_any_t type_any_t;

// 通用类型声明,本质上和 any 没有什么差别,能够表示任何类型
typedef struct type_t {
    union {
        type_int_t int_;
        type_float_t float_;
        type_bool_t bool_;
        type_string_t *string_;
        type_list_t *list_;
        type_array_t *array_;
        type_map_t *map_;
        type_set_t *set_;
        type_tuple_t *tuple_;
        type_struct_t *struct_;
        type_fn_t *fn_;
        void *value_;
    };
    type_kind kind;
    bool is_origin; // type a = int, type b = a，int is origin
    uint8_t point; // 指针等级, 如果等于0 表示非指针, 例如 int*** a; a 的 point 等于 3
} type_t;

// 类型描述信息 end


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
    int size; // 存储在 heap 中的数据的大小 count + ptr(data) 的长度
    int count; // 字符串的长度
//    void *data; // 这里引用了什么？是 type_array_t 元数据还是 type_array_t 存储在 heap 中的数据？
};

// 假设已经知道了数组元素的类型，又如何计算其是否为指针呢
// nature 反而比较简单，只要不是标量元素，其他元素其值一定是一个指针，包括 struct
// golang 中由于 struct 也是标量，所以其需要拆解 struct 才能填充元素是否为指针
// 假如 type_array_t 是编译时的数据，那编译时根本就不知道 *data 的值是多少！
// void* ptr =  malloc(sizeof(element_type) * count) // 数组初始化后最终会得到这样一份数据，这个数据将会存在的 var 中
struct type_array_t {
    int size; // 存储在 mheap 中的数据，sizeof(element_type) * count, 其他的 type 就不用存了，编译时都知道了
    int count;
    type_t element_type;
//    uint8_t *data; // 数组的值,这里的值同样是不会变的,即使其不会变，我们也无法知道其值是多少。所以这个 data 是毫无意义的
};

// list 如果自己持有一个动态的 data 呢？一旦 list 发生了扩容，那么需要新从新申请一个 data 区域
// 在 runtime_malloc 中很难描述这一段数据的类型？其实其本质就是一个 fixed array 结构，所以直接搞一个 array_t 更好描述 gc_bits
// 反而更好处理？
struct type_list_t {
    int size; // 存储在 mheap 中的数据,count + cap + data, element_type 就不用存了，编译时确定之后就不会变了
    // 描述 list 类型是只需要一个 type, 比如 [int] !,且越界等行为在 compiler 都是无法判断的
    // 所以 count,capacity 都是没有意义的数据！
//    int capacity; // 实际占用的位置的大小
//    int count; // 预先申请的容量大小
    type_t element_type;
    // 类型描述信息根本就不能有值这个东西出现
//    void *data; // 引用的是一个 array 结构
};

// 这里应该用 c string 吗？ 衡量的标准是什么？标准是这个数据用在哪里,key 这种数据一旦确定就不会变化了,就将其存储在编译时就行了
typedef struct {
    char *key;
    int align_offset; // 对齐后起始地址
    type_t type; // 这里存放的数据的大小是固定的吗？
} struct_property_t;

// 比如 type_struct_t 结构，如何能够将其传递到运行时，一旦运行时知道了该结构，编译时就不用费劲心机的在 lir 中传递该数据了？
// 可以通过连接器传递，但是其长度不规则,尤其是指针嵌套着指针的情况，所以将其序列化传递到 runtime 是很困难的事情
// golang 中的 gc_bits 也是不定长的数据，怎么传递？ map,slice 都还好说 可以在 runtime 里面生成
// 那 struct 呢？
struct type_struct_t {
    int size; // 占用的 mheap 的空间(按每个 property 对齐后的数据)
    int8_t count; // 属性的个数，用于遍历
    struct_property_t *properties; // 属性列表,其每个元素的长度都是不固定的？有不固定的数组吗?
};

/**
 * type_fn_t 是什么样的结构？怎么存储在堆内存中?
 * NO, fn 并不存储在堆中，而是存储在 .text section 中。
 * type_fn_t f 指向的函数是 text 中的虚拟地址。f() 本质上是调用 call 指令进行 rip 指针的跳转
 * 所以其 reflect size = 8 且 gc_bits = 1
 * type_fn_t 在堆内存中仅仅是一个指针数据，指向堆内存, 这里的数据就是编译器前端的一个类型描述
 */
struct type_fn_t {
    int size; // 占用的堆内存的大小
    type_t return_type;
    type_t formals_types[UINT8_MAX];
    uint8_t formals_count;
    bool rest_param;
};


// 所有的 type 都可以转化成该结构
typedef struct {
    uint index; // 全局 index,在 linker 时 reflect_type 的顺序会被打乱，需要靠 index 进行复原
    uint size; // 类型占用的 size,和 gc_bits 联合起来使用
    uint hash; // 做类型推断时能够快速判断出类型是否相等
    uint last_ptr; // 类型对应的堆数据中最后一个包含指针的字节数
    uint8_t kind; // 类型的种类
    uint8_t *gc_bits; // 类型 bit 数据(按 uint8 对齐)
} reflect_type_t;

/**
 * type_any_t 到底是类型，还是数据?, type_any_t 应该只是一个类型的描述，类似 type_array_t 一样
 * 比如 type_array_t 中有 element_type 和 count, 通过这两个数据我们能够知道其在内存中存储的数据的程度
 * 以及每一块内存存放了什么东西，但是并不知道数据实际存放在了哪里。
 *
 * 同理，如果 type_any_t 是类型的话，那就不知道存储的是什么,所以必须删除 void* value!
 * 但是给定 void* value,能够知道其内存中存储了什么东西！
 */
struct type_any_t {
    uint size; // 16byte,一部分存储原始值，一部分存储 rtype 数据！
    // rtype 和 value 都是变化的数据，所以类型描述信息中啥也没有，啥也不需要知道
//    reflect_type_t *rtype; // 这样的话 new any_t 太麻烦了
//    uint rtype_index; // 这样定位更快
//    void *value;
};

// 所有的类型都会有一个唯一标识，从而避免类型的重复，不重复的类型会被加入到其中
// list 的唯一标识， 比如 [int] a, [int] b , [float] c   等等，其实只有一种类型
// 区分是否是同一种类型，就看 reflect_type 中的 gc_bits 是否一致
slice_t *rtypes;
table_t *rtype_table; // 每当有一个新的类型产生，都会注册在该表中，值为 slice 的索引！

reflect_type_t rtype(type_t t);

reflect_type_t rtype_int(type_int_t t);

reflect_type_t rtype_float(type_float_t t);

reflect_type_t rtype_bool(type_bool_t t);

reflect_type_t rtype_string(type_string_t t);

reflect_type_t rtype_list(type_list_t t);

reflect_type_t rtype_array(type_array_t t);

reflect_type_t rtype_map(type_map_t t);

reflect_type_t rtype_set(type_set_t t);

reflect_type_t rtype_tuple(type_tuple_t t);

// 仅该类型和 array 类型会随着元素的个数变化而变化
reflect_type_t rtype_struct(type_struct_t t);

reflect_type_t rtype_any(type_any_t t);

/**
 * 将 reflect_types 进行序列化
 * @param count
 * @return
 */
uint8_t *rtypes_serialize(slice_t *reflect_types, uint64_t *count);

slice_t *rtypes_deserialize(uint8_t *data, uint64_t data_count);

reflect_type_t *find_rtype(uint index);

/**
 * 其对应的 var 在栈上占用的空间，而不是其在堆内存中的大小
 * @param t
 * @return
 */
uint8_t type_kind_sizeof(type_kind t);

type_t type_with_point(type_t t, uint8_t point);

type_t type_base_new(type_kind kind);

type_t type_new(type_kind kind, void *value);


#endif //NATURE_TYPE_H

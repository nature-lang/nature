# import [reflect](https://github.com/nature-lang/nature/blob/master/std/reflect/main.n)

运行时反射工具库，提供类型内省和值操作功能

## const NULL

```
const NULL = 1
```

null 类型的类型常量

## const BOOL

```
const BOOL = 2
```

布尔类型的类型常量

## const I8

```
const I8 = 3
```

8位有符号整数类型的类型常量

## const U8

```
const U8 = 4
```

8位无符号整数类型的类型常量

## const I16

```
const I16 = 5
```

16位有符号整数类型的类型常量

## const U16

```
const U16 = 6
```

16位无符号整数类型的类型常量

## const I32

```
const I32 = 7
```

32位有符号整数类型的类型常量

## const U32

```
const U32 = 8
```

32位无符号整数类型的类型常量

## const I64

```
const I64 = 9
```

64位有符号整数类型的类型常量

## const U64

```
const U64 = 10
```

64位无符号整数类型的类型常量

## const INT

```
const INT = 9
```

int 类型的类型常量（编译时转换为 i64）

## const UINT

```
const UINT = 10
```

uint 类型的类型常量（编译时转换为 u64）

## const F32

```
const F32 = 11
```

32位浮点数类型的类型常量

## const F64

```
const F64 = 12
```

64位浮点数类型的类型常量

## const FLOAT

```
const FLOAT = 12
```

float 类型的类型常量（编译时转换为 f64）

## const STRING

```
const STRING = 13
```

字符串类型的类型常量

## const VEC

```
const VEC = 14
```

向量类型的类型常量

## const ARR

```
const ARR = 15
```

数组类型的类型常量

## const MAP

```
const MAP = 16
```

映射类型的类型常量

## const SET

```
const SET = 17
```

集合类型的类型常量

## const TUP

```
const TUP = 18
```

元组类型的类型常量

## const STRUCT

```
const STRUCT = 19
```

结构体类型的类型常量

## const FN

```
const FN = 20
```

函数类型的类型常量

## const CHAN

```
const CHAN = 21
```

通道类型的类型常量

## const COROUTINE_T

```
const COROUTINE_T = 22
```

协程类型的类型常量

## const PTR

```
const PTR = 23
```

指针类型的类型常量

## const RAWPTR

```
const RAWPTR = 24
```

原始指针类型的类型常量

## const ANYPTR

```
const ANYPTR = 25
```

任意指针类型的类型常量

## const UNION

```
const UNION = 26
```

联合类型的类型常量

## const PTR_SIZE

```
const PTR_SIZE = 8
```

指针大小（字节数）

## fn find_rtype

```
fn find_rtype(int hash):rawptr<rtype_t>
```

通过哈希值查找运行时类型信息

## fn find_data

```
fn find_data(int offset):anyptr
```

在指定偏移量处查找数据

## fn find_strtable

```
fn find_strtable(int offset):libc.cstr
```

在指定偏移量处查找字符串表中的字符串

## fn typeof_hash

```
fn typeof_hash(i64 hash):type_t!
```

从哈希值获取类型信息

## fn sizeof_rtype

```
fn sizeof_rtype(rawptr<rtype_t> r):int
```

获取运行时类型的大小

## fn typeof

```
fn typeof<T>(T v):type_t!
```

获取值的类型信息

## fn valueof

```
fn valueof<T>(T v):value_t!
```

为支持的类型（vec、map、set）创建值包装器

## type field_t

```
type field_t = struct{
    string name
    int hash
    int offset
}
```

表示结构体字段，包含名称、哈希值和偏移量信息

## type type_t

```
type type_t = struct{
    int size
    int hash
    int kind
    u8 align
    [int] hashes
    [field_t] fields
}
```

表示类型信息，包括大小、哈希值、类型种类和字段详情

### fn type_t.to_string

```
fn type_t.to_string():string
```

将类型种类转换为字符串表示

## type rtype_struct_t

```
type rtype_struct_t = struct{
    i64 name_offset
    i64 hash
    i64 offset
}
```

运行时结构体类型信息

## type rtype_t

```
type rtype_t = struct{
    u64 ident_offset
    u64 size
    u8 in_heap
    i64 hash
    u64 last_ptr
    u32 kind
    i64 malloc_gc_bits_offset
    u64 gc_bits
    u8 align
    u16 length
    i64 hashes_offset
}
```

运行时类型信息结构

## type vec_t

```
type vec_t = struct{
    anyptr data
    i64 length
    i64 capacity
    i64 element_size
    i64 hash
}
```

内部向量结构表示

## type string_t

```
type string_t = vec_t
```

内部字符串结构（vec_t 的别名）

## type map_t

```
type map_t = struct{
    anyptr hash_table
    anyptr key_data
    anyptr value_data
    i64 key_hash
    i64 value_hash
    i64 length
    i64 capacity
}
```

内部映射结构表示

## type set_t

```
type set_t = struct{
    anyptr hash_table
    anyptr key_data
    i64 key_hash
    i64 length
    i64 capacity
}
```

内部集合结构表示

## type union_t

```
type union_t = struct{
    anyptr value
    rawptr<rtype_t> rtype
}
```

内部联合结构表示

## type value_t

```
type value_t = struct{
    type_t t
    anyptr v
}
```

包含类型信息和值指针的值包装器

### fn value_t.len

```
fn value_t.len():int
```

获取包装值的长度（适用于 vec、map、set 类型）
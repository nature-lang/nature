# import [runtime](https://github.com/nature-lang/nature/blob/master/std/runtime/main.n)

运行时系统工具库，提供内存管理、垃圾回收和核心数据结构功能

## fn vec_new

```
fn vec_new(int hash, int element_hash, int len, anyptr value_ref):anyptr!
```

创建一个新的向量，指定哈希值、元素哈希值、长度和初始值引用

## fn vec_cap

```
fn vec_cap(int hash, int element_hash, int cap):anyptr!
```

创建一个指定容量的新向量

## fn vec_push

```
fn vec_push(anyptr list, int element_hash, anyptr val)
```

向向量末尾添加一个元素

## fn vec_grow

```
fn vec_grow(anyptr list, int element_hash, int custom_capacity)
```

将向量容量扩展到指定大小

## fn vec_slice

```
fn vec_slice(anyptr list, int start, int end):anyptr!
```

从开始索引到结束索引创建向量的切片

## fn vec_append

```
fn vec_append(anyptr list1, anyptr list2, int element_hash)
```

将 list2 的元素追加到 list1

## fn vec_concat

```
fn vec_concat(anyptr list1, anyptr list2, int element_hash):anyptr
```

连接两个向量并返回新向量

## fn set_new

```
fn set_new(int hash, int key_hash):anyptr
```

创建一个新的集合，指定哈希值和键哈希值

## fn set_add

```
fn set_add(anyptr s, anyptr key):bool
```

向集合添加一个键，如果键被添加则返回 true

## fn set_contains

```
fn set_contains(anyptr s, anyptr key):bool
```

检查集合是否包含指定的键

## fn set_delete

```
fn set_delete(anyptr s, anyptr key)
```

从集合中移除一个键

## fn map_new

```
fn map_new(int hash, int key_hash, int value_hash):anyptr
```

创建一个新的映射，指定哈希值、键哈希值和值哈希值

## fn map_delete

```
fn map_delete(anyptr m, anyptr key)
```

从映射中移除一个键值对

## fn map_contains

```
fn map_contains(anyptr m, anyptr key):bool
```

检查映射是否包含指定的键

## fn map_assign

```
fn map_assign(anyptr m, anyptr key_ref):anyptr
```

获取或创建指定键的值引用

## fn string_ref

```
fn string_ref(anyptr s):anyptr
```

获取字符串的引用

## fn processor_index

```
fn processor_index():int
```

获取当前处理器索引

## fn gc

```
fn gc()
```

强制执行垃圾回收

## fn malloc_bytes

```
fn malloc_bytes():i64
```

获取 malloc 分配的字节数

## fn gc_malloc

```
fn gc_malloc(int hash):anyptr
```

使用垃圾回收器分配内存，指定哈希值

## fn gc_malloc_size

```
fn gc_malloc_size(int size):anyptr
```

使用垃圾回收器分配指定大小的内存

## fn string_new

```
fn string_new(anyptr s):string
```

从指针创建新字符串

## fn string_ref_new

```
fn string_ref_new(anyptr s, int len):string
```

从指针创建指定长度的新字符串
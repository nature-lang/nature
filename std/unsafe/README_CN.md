# import unsafe

绕过正常安全检查的不安全操作，包括原始指针操作和内存操作

## fn vec_new

```
fn vec_new<T>(ptr<T> p, int len):vec<T>!
```

从原始指针和长度创建新的向量。长度必须为非负数，否则抛出错误

## fn ptr_to

```
fn ptr_to<T>(anyptr p):T
```

解引用 anyptr 并返回类型 T 的值

## fn ptr_copy

```
fn ptr_copy<T>(anyptr dst, ptr<T> src)
```

使用类型 T 的大小将内存从源指针复制到目标指针
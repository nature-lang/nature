# import [mem](https://github.com/nature-lang/nature/tree/master/std/mem/main.n)

内存操作和字节序转换工具库

## fn copy

```
fn copy<T>([u8] buf, ptr<T> dst):void!
```

将缓冲区数据复制到指定类型的原始指针位置

## fn write_u8_le

```
fn write_u8_le(u8 i):[u8]!
```

将 u8 值以小端序写入字节数组

## fn write_u16_le

```
fn write_u16_le(u16 i):[u8]!
```

将 u16 值以小端序写入字节数组

## fn write_u32_le

```
fn write_u32_le(u32 i):[u8]!
```

将 u32 值以小端序写入字节数组

## fn write_u64_le

```
fn write_u64_le(u64 i):[u8]!
```

将 u64 值以小端序写入字节数组

## fn write_i8_le

```
fn write_i8_le(i8 i):[u8]!
```

将 i8 值以小端序写入字节数组

## fn write_i16_le

```
fn write_i16_le(i16 i):[u8]!
```

将 i16 值以小端序写入字节数组

## fn write_i32_le

```
fn write_i32_le(i32 i):[u8]!
```

将 i32 值以小端序写入字节数组

## fn write_i64_le

```
fn write_i64_le(i64 i):[u8]!
```

将 i64 值以小端序写入字节数组

## fn write_f32_le

```
fn write_f32_le(f32 f):[u8]!
```

将 f32 值以小端序写入字节数组

## fn write_f64_le

```
fn write_f64_le(f64 f):[u8]!
```

将 f64 值以小端序写入字节数组

## fn write_u8_be

```
fn write_u8_be(u8 i):[u8]!
```

将 u8 值以大端序写入字节数组

## fn write_u16_be

```
fn write_u16_be(u16 i):[u8]!
```

将 u16 值以大端序写入字节数组

## fn write_u32_be

```
fn write_u32_be(u32 i):[u8]!
```

将 u32 值以大端序写入字节数组

## fn write_u64_be

```
fn write_u64_be(u64 i):[u8]!
```

将 u64 值以大端序写入字节数组

## fn write_i8_be

```
fn write_i8_be(i8 i):[u8]!
```

将 i8 值以大端序写入字节数组

## fn write_i16_be

```
fn write_i16_be(i16 i):[u8]!
```

将 i16 值以大端序写入字节数组

## fn write_i32_be

```
fn write_i32_be(i32 i):[u8]!
```

将 i32 值以大端序写入字节数组

## fn write_i64_be

```
fn write_i64_be(i64 i):[u8]!
```

将 i64 值以大端序写入字节数组

## fn write_f32_be

```
fn write_f32_be(f32 f):[u8]!
```

将 f32 值以大端序写入字节数组

## fn write_f64_be

```
fn write_f64_be(f64 f):[u8]!
```

将 f64 值以大端序写入字节数组

## fn read_u8_le

```
fn read_u8_le([u8] buf):u8!
```

从字节数组中以小端序读取 u8 值

## fn read_u16_le

```
fn read_u16_le([u8] buf):u16!
```

从字节数组中以小端序读取 u16 值

## fn read_u32_le

```
fn read_u32_le([u8] buf):u32!
```

从字节数组中以小端序读取 u32 值

## fn read_u64_le

```
fn read_u64_le([u8] buf):u64!
```

从字节数组中以小端序读取 u64 值

## fn read_i8_le

```
fn read_i8_le([u8] buf):i8!
```

从字节数组中以小端序读取 i8 值

## fn read_i16_le

```
fn read_i16_le([u8] buf):i16!
```

从字节数组中以小端序读取 i16 值

## fn read_i32_le

```
fn read_i32_le([u8] buf):i32!
```

从字节数组中以小端序读取 i32 值

## fn read_i64_le

```
fn read_i64_le([u8] buf):i64!
```

从字节数组中以小端序读取 i64 值

## fn read_u8_be

```
fn read_u8_be([u8] buf):u8!
```

从字节数组中以大端序读取 u8 值

## fn read_u16_be

```
fn read_u16_be([u8] buf):u16!
```

从字节数组中以大端序读取 u16 值

## fn read_u32_be

```
fn read_u32_be([u8] buf):u32!
```

从字节数组中以大端序读取 u32 值

## fn read_u64_be

```
fn read_u64_be([u8] buf):u64!
```

从字节数组中以大端序读取 u64 值

## fn read_i8_be

```
fn read_i8_be([u8] buf):i8!
```

从字节数组中以大端序读取 i8 值

## fn read_i16_be

```
fn read_i16_be([u8] buf):i16!
```

从字节数组中以大端序读取 i16 值

## fn read_i32_be

```
fn read_i32_be([u8] buf):i32!
```

从字节数组中以大端序读取 i32 值

## fn read_i64_be

```
fn read_i64_be([u8] buf):i64!
```

从字节数组中以大端序读取 i64 值

## fn read_f32_le

```
fn read_f32_le([u8] buf):f32!
```

从字节数组中以小端序读取 f32 值

## fn read_f64_le

```
fn read_f64_le([u8] buf):f64!
```

从字节数组中以小端序读取 f64 值

## fn read_f32_be

```
fn read_f32_be([u8] buf):f32!
```

从字节数组中以大端序读取 f32 值

## fn read_f64_be

```
fn read_f64_be([u8] buf):f64!
```

从字节数组中以大端序读取 f64 值
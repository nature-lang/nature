# import [mem](https://github.com/nature-lang/nature/tree/master/std/mem/main.n)

Memory operations and byte order conversion utilities

## fn copy

```
fn copy<T>([u8] buf, ptr<T> dst):void!
```

Copy buffer data to the specified type's raw pointer location

## fn write_u8_le

```
fn write_u8_le(u8 i):[u8]!
```

Write u8 value to byte array in little-endian order

## fn write_u16_le

```
fn write_u16_le(u16 i):[u8]!
```

Write u16 value to byte array in little-endian order

## fn write_u32_le

```
fn write_u32_le(u32 i):[u8]!
```

Write u32 value to byte array in little-endian order

## fn write_u64_le

```
fn write_u64_le(u64 i):[u8]!
```

Write u64 value to byte array in little-endian order

## fn write_i8_le

```
fn write_i8_le(i8 i):[u8]!
```

Write i8 value to byte array in little-endian order

## fn write_i16_le

```
fn write_i16_le(i16 i):[u8]!
```

Write i16 value to byte array in little-endian order

## fn write_i32_le

```
fn write_i32_le(i32 i):[u8]!
```

Write i32 value to byte array in little-endian order

## fn write_i64_le

```
fn write_i64_le(i64 i):[u8]!
```

Write i64 value to byte array in little-endian order

## fn write_f32_le

```
fn write_f32_le(f32 f):[u8]!
```

Write f32 value to byte array in little-endian order

## fn write_f64_le

```
fn write_f64_le(f64 f):[u8]!
```

Write f64 value to byte array in little-endian order

## fn write_u8_be

```
fn write_u8_be(u8 i):[u8]!
```

Write u8 value to byte array in big-endian order

## fn write_u16_be

```
fn write_u16_be(u16 i):[u8]!
```

Write u16 value to byte array in big-endian order

## fn write_u32_be

```
fn write_u32_be(u32 i):[u8]!
```

Write u32 value to byte array in big-endian order

## fn write_u64_be

```
fn write_u64_be(u64 i):[u8]!
```

Write u64 value to byte array in big-endian order

## fn write_i8_be

```
fn write_i8_be(i8 i):[u8]!
```

Write i8 value to byte array in big-endian order

## fn write_i16_be

```
fn write_i16_be(i16 i):[u8]!
```

Write i16 value to byte array in big-endian order

## fn write_i32_be

```
fn write_i32_be(i32 i):[u8]!
```

Write i32 value to byte array in big-endian order

## fn write_i64_be

```
fn write_i64_be(i64 i):[u8]!
```

Write i64 value to byte array in big-endian order

## fn write_f32_be

```
fn write_f32_be(f32 f):[u8]!
```

Write f32 value to byte array in big-endian order

## fn write_f64_be

```
fn write_f64_be(f64 f):[u8]!
```

Write f64 value to byte array in big-endian order

## fn read_u8_le

```
fn read_u8_le([u8] buf):u8!
```

Read u8 value from byte array in little-endian order

## fn read_u16_le

```
fn read_u16_le([u8] buf):u16!
```

Read u16 value from byte array in little-endian order

## fn read_u32_le

```
fn read_u32_le([u8] buf):u32!
```

Read u32 value from byte array in little-endian order

## fn read_u64_le

```
fn read_u64_le([u8] buf):u64!
```

Read u64 value from byte array in little-endian order

## fn read_i8_le

```
fn read_i8_le([u8] buf):i8!
```

Read i8 value from byte array in little-endian order

## fn read_i16_le

```
fn read_i16_le([u8] buf):i16!
```

Read i16 value from byte array in little-endian order

## fn read_i32_le

```
fn read_i32_le([u8] buf):i32!
```

Read i32 value from byte array in little-endian order

## fn read_i64_le

```
fn read_i64_le([u8] buf):i64!
```

Read i64 value from byte array in little-endian order

## fn read_u8_be

```
fn read_u8_be([u8] buf):u8!
```

Read u8 value from byte array in big-endian order

## fn read_u16_be

```
fn read_u16_be([u8] buf):u16!
```

Read u16 value from byte array in big-endian order

## fn read_u32_be

```
fn read_u32_be([u8] buf):u32!
```

Read u32 value from byte array in big-endian order

## fn read_u64_be

```
fn read_u64_be([u8] buf):u64!
```

Read u64 value from byte array in big-endian order

## fn read_i8_be

```
fn read_i8_be([u8] buf):i8!
```

Read i8 value from byte array in big-endian order

## fn read_i16_be

```
fn read_i16_be([u8] buf):i16!
```

Read i16 value from byte array in big-endian order

## fn read_i32_be

```
fn read_i32_be([u8] buf):i32!
```

Read i32 value from byte array in big-endian order

## fn read_i64_be

```
fn read_i64_be([u8] buf):i64!
```

Read i64 value from byte array in big-endian order

## fn read_f32_le

```
fn read_f32_le([u8] buf):f32!
```

Read f32 value from byte array in little-endian order

## fn read_f64_le

```
fn read_f64_le([u8] buf):f64!
```

Read f64 value from byte array in little-endian order

## fn read_f32_be

```
fn read_f32_be([u8] buf):f32!
```

Read f32 value from byte array in big-endian order

## fn read_f64_be

```
fn read_f64_be([u8] buf):f64!
```

Read f64 value from byte array in big-endian order
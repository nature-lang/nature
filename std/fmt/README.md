# import [fmt](https://github.com/nature-lang/nature/tree/master/std/fmt/main.n)

String formatting and parsing library for formatted input and output operations.

## fn sprintf

```
fn sprintf(string format, ...[any] args):string
```

Format string with arguments and return formatted string.

Format specifiers:
- `%d` - Integer (int, i8, i16, i32, i64, uint, u8, u16, u32, u64)
- `%s` - String
- `%c` - Character (u8)
- `%f` - Float (float, f32, f64) with precision support (e.g., %.2f)
- `%v` - Any value (automatic type detection)
- `%%` - Literal percent sign

Width and padding:
- `%5d` - Right-aligned with width 5
- `%05d` - Zero-padded with width 5
- `%.2f` - Float with 2 decimal places

## fn printf

```
fn printf(string format, ...[any] args)
```

Format and print string to standard output.

Uses the same format specifiers as sprintf.

## fn sscanf

```
fn sscanf(string str, string format, ...[any] args):int
```

Parse formatted string and extract values into arguments.

Format specifiers:
- `%d` - Parse integer
- `%x` - Parse hexadecimal number
- `%o` - Parse octal number
- `%s` - Parse string (non-whitespace characters)
- `%c` - Parse single character or fixed-width string
- `%f` - Parse floating-point number

Returns the number of successfully parsed arguments.
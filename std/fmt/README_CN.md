# import [fmt](https://github.com/nature-lang/nature/tree/master/std/fmt/main.n)

字符串格式化和解析库，用于格式化输入输出操作。

## fn sprintf

```
fn sprintf(string format, ...[any] args):string
```

使用参数格式化字符串并返回格式化后的字符串。

格式说明符：
- `%d` - 整数 (int, i8, i16, i32, i64, uint, u8, u16, u32, u64)
- `%s` - 字符串
- `%c` - 字符 (u8)
- `%f` - 浮点数 (float, f32, f64) 支持精度设置 (如 %.2f)
- `%v` - 任意值 (自动类型检测)
- `%%` - 字面百分号

宽度和填充：
- `%5d` - 右对齐，宽度为 5
- `%05d` - 零填充，宽度为 5
- `%.2f` - 浮点数保留 2 位小数

## fn printf

```
fn printf(string format, ...[any] args)
```

格式化并打印字符串到标准输出。

使用与 sprintf 相同的格式说明符。

## fn sscanf

```
fn sscanf(string str, string format, ...[any] args):int
```

解析格式化字符串并将值提取到参数中。

格式说明符：
- `%d` - 解析整数
- `%x` - 解析十六进制数
- `%o` - 解析八进制数
- `%s` - 解析字符串 (非空白字符)
- `%c` - 解析单个字符或固定宽度字符串
- `%f` - 解析浮点数

返回成功解析的参数数量。
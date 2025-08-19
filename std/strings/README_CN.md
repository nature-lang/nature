# import [strings](https://github.com/nature-lang/nature/blob/master/std/strings/main.n)

字符串操作工具库，提供常用的字符串处理功能

## fn from

```
fn from(anyptr p):string!
```

从 C 字符串指针创建 nature 字符串

## fn join

```
fn join([string] list, string separator):string
```

使用分隔符连接字符串数组

## fn string.find_char

```
fn string.find_char(u8 char, int after):int
```

从指定位置开始查找字符的索引，未找到返回 -1

## fn string.find_after

```
fn string.find_after(string sub, int after):int
```

从指定位置开始查找子字符串的索引，未找到返回 -1

## fn string.reverse

```
fn string.reverse():string
```

反转字符串中的字符

## fn string.rfind

```
fn string.rfind(string sub):int
```

查找子字符串最后一次出现的索引，未找到返回 -1

## fn string.ends_with

```
fn string.ends_with(string ends):bool
```

检查字符串是否以给定的后缀结尾

## fn string.starts_with

```
fn string.starts_with(string starts):bool
```

检查字符串是否以给定的前缀开头

## fn string.contains

```
fn string.contains(string sub):bool
```

检查字符串是否包含子字符串

## fn string.finish

```
fn string.finish(string cap):string
```

确保字符串以给定后缀结尾，如果没有则添加

## fn string.find

```
fn string.find(string sub):int
```

查找子字符串第一次出现的索引，未找到返回 -1

## fn string.slice

```
fn string.slice(int start, int end):string
```

从开始索引到结束索引提取子字符串

## fn string.split

```
fn string.split(string separator):[string]
```

使用分隔符分割字符串并返回字符串数组

## fn string.ascii

```
fn string.ascii():u8
```

获取第一个字符的 ASCII 值

## fn string.ltrim

```
fn string.ltrim([string] list):string
```

移除列表中指定的前导字符

## fn string.rtrim

```
fn string.rtrim([string] list):string
```

移除列表中指定的尾随字符

## fn string.trim

```
fn string.trim([string] list):string
```

移除列表中指定的前导和尾随字符

## fn string.replace

```
fn string.replace(string sub_old, string sub_new):string
```

将所有旧子字符串替换为新子字符串

## fn string.to_int

```
fn string.to_int():int!
```

将字符串转换为整数，格式无效时可能抛出错误

## fn string.to_float

```
fn string.to_float():float!
```

将字符串转换为浮点数，格式无效时可能抛出错误

## fn string.to_lower

```
fn string.to_lower():string
```

将字符串转换为小写

## fn string.to_upper

```
fn string.to_upper():string
```

将字符串转换为大写
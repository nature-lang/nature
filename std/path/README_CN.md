# import [path](https://github.com/nature-lang/nature/blob/master/std/path/main.n)

文件系统路径操作工具

## fn exists

```
fn exists(string path):bool
```

检查给定路径的文件或目录是否存在

## fn base

```
fn base(string path):string
```

返回路径的最后一个元素，类似于Unix的basename命令

## fn dir

```
fn dir(string path):string
```

返回路径的目录部分，类似于Unix的dirname命令

## fn join

```
fn join(string dst, ...[string] list):string
```

将多个路径段连接成单个路径，使用适当的分隔符

## fn isdir

```
fn isdir(string path):bool
```

检查给定路径是否为目录
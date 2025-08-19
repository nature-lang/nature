# import [os](https://github.com/nature-lang/nature/blob/master/std/os/main.n)

操作系统接口，提供文件系统操作和进程工具

## type signal

```
type signal = u8
```

用于进程信号处理的信号类型

## fn args

```
fn args():[string]
```

获取传递给当前程序的命令行参数

## fn exe

```
fn exe():string!
```

通过读取 /proc/self/exe 获取当前可执行文件的路径

## fn dirs_sort

```
fn dirs_sort([string] dirs)
```

使用冒泡排序按字母顺序对目录条目进行排序

## fn listdir

```
fn listdir(string path):[string]!
```

列出给定路径中的所有文件和目录，排除 '.' 和 '..'

## fn mkdirs

```
fn mkdirs(string dir, u32 mode):void!
```

使用指定权限递归创建目录

## fn remove

```
fn remove(string full_path):void!
```

删除指定路径的文件

## fn rmdir

```
fn rmdir(string dir, bool recursive):void!
```

删除目录，可选择递归删除所有内容
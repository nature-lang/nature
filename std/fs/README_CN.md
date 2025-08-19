# import [fs](https://github.com/nature-lang/nature/tree/master/std/fs/main.n)

文件系统操作，用于读取、写入和管理文件。

## fn stdout

```
fn stdout():ptr<file_t>!
```

获取标准输出并转换为 file_t。

## fn stdin

```
fn stdin():ptr<file_t>!
```

获取标准输入并转换为 file_t。

## fn stderr

```
fn stderr():ptr<file_t>!
```

获取标准错误并转换为 file_t。

## fn discard

```
fn discard():ptr<file_t>!
```

获取一个丢弃文件句柄，忽略所有写入操作。

## type timespec_t

```
type timespec_t = struct {
    u64 sec 
    u64 nsec
}
```

时间规范，包含秒和纳秒。

## type stat_t

```
type stat_t = struct {
    u64 dev
    u64 mode
    u64 nlink
    u64 uid
    u64 gid
    u64 rdev
    u64 ino
    u64 size
    u64 blksize
    u64 blocks
    u64 flags
    u64 gen
    timespec_t atime
    timespec_t mtime
    timespec_t ctime
    timespec_t birthtime
}
```

文件状态信息。

## type file_t

```
type file_t:io.reader, io.writer, io.seeker = struct{
    int fd
    anyptr data
    i64 data_len
    i64 data_cap
    bool closed
}
```

文件表示一个打开的文件描述符。

### from

```
fn from(int fd, string name):ptr<file_t>!
```

从文件描述符创建 file_t 结构。

### open

```
fn open(string path, int flags, int mode):ptr<file_t>!
```

打开文件并返回文件句柄。

### file_t.content

```
fn file_t.content():string!
```

读取完整文件并返回字符串。

### file_t.read

```
fn file_t.read([u8] buf):int!
```

从文件读取数据到缓冲区。

### file_t.write

```
fn file_t.write([u8] buf):int!
```

从缓冲区写入数据到文件。

### file_t.seek

```
fn file_t.seek(int offset, int whence):int!
```

定位到文件中的指定位置。

### file_t.read_at

```
fn file_t.read_at([u8] buf, int offset):int!
```

在指定偏移量处从文件读取数据。

### file_t.write_at

```
fn file_t.write_at([u8] buf, int offset):int!
```

在指定偏移量处向文件写入数据。

### file_t.close

```
fn file_t.close():void
```

关闭文件句柄。

### file_t.stat

```
fn file_t.stat():stat_t!
```

获取文件状态信息。
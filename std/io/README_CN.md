# import [io](https://github.com/nature-lang/nature/tree/master/std/io/main.n)

输入/输出接口和缓冲区实现，用于读取和写入数据。

## type reader

```
type reader = interface{
    fn read([u8] buf):int!
}
```

从数据源读取数据的接口。

## type writer

```
type writer = interface{
    fn write([u8] buf):int!
}
```

向目标写入数据的接口。

## type seeker

```
type seeker = interface{
    fn seek(int offset, int whence):int!
}
```

在数据源中定位到特定位置的接口。

## type buffer

```
type buffer:reader,writer = struct{
    [u8] buf
    int offset
}
```

支持读写操作的缓冲区实现。

### buffer.write

```
fn buffer.write([u8] buf):int!
```

向缓冲区写入数据。

### buffer.write_byte

```
fn buffer.write_byte(u8 byte):int!
```

向缓冲区写入单个字节。

### buffer.read

```
fn buffer.read([u8] buf):int!
```

从缓冲区读取数据。

### buffer.empty

```
fn buffer.empty():bool
```

检查缓冲区是否为空。

### buffer.len

```
fn buffer.len():int
```

获取缓冲区中可用数据的长度。

### buffer.cap

```
fn buffer.cap():int
```

获取缓冲区的容量。

### buffer.truncate

```
fn buffer.truncate(int n):void!
```

将缓冲区截断到指定长度。

### buffer.reset

```
fn buffer.reset():void!
```

重置缓冲区为空状态。

### buffer.read_all

```
fn buffer.read_all():[u8]!
```

读取缓冲区中的所有剩余数据。

# import [io.buf](https://github.com/nature-lang/nature/tree/master/std/io/buf.n)

缓冲 I/O 操作，用于高效的读写操作。

## type reader

```
type reader<T:io.reader>:io.reader = struct{
    T rd
    [u8] buf
    int r
    int w
    bool eof
}
```

包装 io.reader 的缓冲读取器，用于高效的读取操作。

### reader.size

```
fn reader<T:io.reader>.size():int
```

获取内部缓冲区的大小。

### reader.reset

```
fn reader<T:io.reader>.reset(T rd)
```

使用新的底层读取器重置缓冲读取器。

### reader.read

```
fn reader<T:io.reader>.read([u8] buf):int!
```

将数据读取到提供的缓冲区中。

### reader.buffered

```
fn reader<T:io.reader>.buffered():int
```

获取当前缓冲的字节数。

### reader.read_until

```
fn reader<T:io.reader>.read_until(u8 delim):[u8]
```

读取数据直到找到指定的分隔符。

### reader.read_exact

```
fn reader<T:io.reader>.read_exact([u8] buf):void!
```

精确读取指定数量的字节。

### reader.read_byte

```
fn reader<T:io.reader>.read_byte():u8!
```

读取单个字节。

### reader.read_line

```
fn reader<T:io.reader>.read_line():string!
```

读取一行文本，处理 \n 和 \r\n 行结束符。

## type writer

```
type writer<T:io.writer>:io.writer = struct{
    T wr
    [u8] buf
    int n
    bool err
}
```

包装 io.writer 的缓冲写入器，用于高效的写入操作。

### writer.size

```
fn writer<T:io.writer>.size():int
```

获取内部缓冲区的大小。

### writer.reset

```
fn writer<T:io.writer>.reset(T wr)
```

使用新的底层写入器重置缓冲写入器。

### writer.available

```
fn writer<T:io.writer>.available():int
```

获取缓冲区中可用的字节数。

### writer.buffered

```
fn writer<T:io.writer>.buffered():int
```

获取当前缓冲的字节数。

### writer.write

```
fn writer<T:io.writer>.write([u8] buf):int!
```

向缓冲区写入数据。

### writer.write_byte

```
fn writer<T:io.writer>.write_byte(u8 b):void!
```

向缓冲区写入单个字节。
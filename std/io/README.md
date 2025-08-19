# import [io](https://github.com/nature-lang/nature/tree/master/std/io/main.n)

Input/output interfaces and buffer implementation for reading and writing data.

## type reader

```
type reader = interface{
    fn read([u8] buf):int!
}
```

Interface for reading data from a source.

## type writer

```
type writer = interface{
    fn write([u8] buf):int!
}
```

Interface for writing data to a destination.

## type seeker

```
type seeker = interface{
    fn seek(int offset, int whence):int!
}
```

Interface for seeking to a specific position in a data source.

## type buffer

```
type buffer:reader,writer = struct{
    [u8] buf
    int offset
}
```

Buffer implementation that supports both reading and writing operations.

### buffer.write

```
fn buffer.write([u8] buf):int!
```

Write data to the buffer.

### buffer.write_byte

```
fn buffer.write_byte(u8 byte):int!
```

Write a single byte to the buffer.

### buffer.read

```
fn buffer.read([u8] buf):int!
```

Read data from the buffer.

### buffer.empty

```
fn buffer.empty():bool
```

Check if the buffer is empty.

### buffer.len

```
fn buffer.len():int
```

Get the length of available data in the buffer.

### buffer.cap

```
fn buffer.cap():int
```

Get the capacity of the buffer.

### buffer.truncate

```
fn buffer.truncate(int n):void!
```

Truncate the buffer to specified length.

### buffer.reset

```
fn buffer.reset():void!
```

Reset the buffer to empty state.

### buffer.read_all

```
fn buffer.read_all():[u8]!
```

Read all remaining data from the buffer.

# import [io.buf](https://github.com/nature-lang/nature/tree/master/std/io/buf.n)

Buffered I/O operations for efficient reading and writing.

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

Buffered reader that wraps an io.reader for efficient reading operations.

### reader.size

```
fn reader<T:io.reader>.size():int
```

Get the size of the internal buffer.

### reader.reset

```
fn reader<T:io.reader>.reset(T rd)
```

Reset the buffered reader with a new underlying reader.

### reader.read

```
fn reader<T:io.reader>.read([u8] buf):int!
```

Read data into the provided buffer.

### reader.buffered

```
fn reader<T:io.reader>.buffered():int
```

Get the number of bytes currently buffered.

### reader.read_until

```
fn reader<T:io.reader>.read_until(u8 delim):[u8]
```

Read data until the specified delimiter is found.

### reader.read_exact

```
fn reader<T:io.reader>.read_exact([u8] buf):void!
```

Read exactly the specified number of bytes.

### reader.read_byte

```
fn reader<T:io.reader>.read_byte():u8!
```

Read a single byte.

### reader.read_line

```
fn reader<T:io.reader>.read_line():string!
```

Read a line of text, handling both \n and \r\n line endings.

## type writer

```
type writer<T:io.writer>:io.writer = struct{
    T wr
    [u8] buf
    int n
    bool err
}
```

Buffered writer that wraps an io.writer for efficient writing operations.

### writer.size

```
fn writer<T:io.writer>.size():int
```

Get the size of the internal buffer.

### writer.reset

```
fn writer<T:io.writer>.reset(T wr)
```

Reset the buffered writer with a new underlying writer.

### writer.available

```
fn writer<T:io.writer>.available():int
```

Get the number of bytes available in the buffer.

### writer.buffered

```
fn writer<T:io.writer>.buffered():int
```

Get the number of bytes currently buffered.

### writer.write

```
fn writer<T:io.writer>.write([u8] buf):int!
```

Write data to the buffer.

### writer.write_byte

```
fn writer<T:io.writer>.write_byte(u8 b):void!
```

Write a single byte to the buffer.
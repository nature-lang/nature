# import [fs](https://github.com/nature-lang/nature/tree/master/std/fs/main.n)

File system operations for reading, writing, and managing files.

## fn stdout

```
fn stdout():ptr<file_t>!
```

Get the standard output and convert it to file_t.

## fn stdin

```
fn stdin():ptr<file_t>!
```

Get the standard input and convert it to file_t.

## fn stderr

```
fn stderr():ptr<file_t>!
```

Get the standard error and convert it to file_t.

## fn discard

```
fn discard():ptr<file_t>!
```

Get a discard file handle that ignores all writes.

## type timespec_t

```
type timespec_t = struct {
    u64 sec 
    u64 nsec
}
```

Time specification with seconds and nanoseconds.

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

File status information.

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

File represents an open file descriptor.

### from

```
fn from(int fd, string name):ptr<file_t>!
```

Create a file_t structure from file descriptor.

### open

```
fn open(string path, int flags, int mode):ptr<file_t>!
```

Open a file and return file handle.

### file_t.content

```
fn file_t.content():string!
```

Read the complete file and return as string.

### file_t.read

```
fn file_t.read([u8] buf):int!
```

Read data from file into buffer.

### file_t.write

```
fn file_t.write([u8] buf):int!
```

Write data from buffer to file.

### file_t.seek

```
fn file_t.seek(int offset, int whence):int!
```

Seek to specified position in file.

### file_t.read_at

```
fn file_t.read_at([u8] buf, int offset):int!
```

Read data from file at specified offset.

### file_t.write_at

```
fn file_t.write_at([u8] buf, int offset):int!
```

Write data to file at specified offset.

### file_t.close

```
fn file_t.close():void
```

Close the file handle.

### file_t.stat

```
fn file_t.stat():stat_t!
```

Get file status information.
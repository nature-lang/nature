# [import syscall](https://github.com/nature-lang/nature/blob/master/std/syscall/main.n)

System call interface, providing low-level access to operating system services including file operations, process management, networking, and more.

## const MS_ASYNC

```
u32 MS_ASYNC = 0x1
```

Memory synchronization flag for asynchronous operations

## const MS_INVALIDATE

```
u32 MS_INVALIDATE = 0x2
```

Memory synchronization flag to invalidate cached data

## const MS_SYNC

```
u32 MS_SYNC = 0x10
```

Memory synchronization flag for synchronous operations

## const O_RDONLY

```
int O_RDONLY = 0x0000
```

Open file in read-only mode

## const O_WRONLY

```
int O_WRONLY = 0x0001
```

Open file in write-only mode

## const O_RDWR

```
int O_RDWR = 0x0002
```

Open file in read-write mode

## const O_APPEND

```
int O_APPEND = 0x0008
```

Open file in append mode, writes occur at end of file

## const O_CREAT

```
int O_CREAT = 0x0200
```

Create file if it doesn't exist

## const O_EXCL

```
int O_EXCL = 0x0800
```

Used with O_CREAT, file must not exist

## const O_TRUNC

```
int O_TRUNC = 0x0400
```

Truncate file to zero length when opening

## const SEEK_SET

```
int SEEK_SET = 0
```

Seek from beginning of file

## const SEEK_CUR

```
int SEEK_CUR = 1
```

Seek from current position

## const SEEK_END

```
int SEEK_END = 2
```

Seek from end of file

## const STDIN

```
int STDIN = 0
```

Standard input file descriptor

## const STDOUT

```
int STDOUT = 1
```

Standard output file descriptor

## const STDERR

```
int STDERR = 2
```

Standard error file descriptor

## const SIGHUP

```
int SIGHUP = 1
```

Hangup signal

## const SIGINT

```
int SIGINT = 2
```

Interrupt signal (Ctrl+C)

## const SIGQUIT

```
int SIGQUIT = 3
```

Quit signal

## const SIGKILL

```
int SIGKILL = 9
```

Kill signal (cannot be caught or ignored)

## const SIGTERM

```
int SIGTERM = 15
```

Termination signal

## const AF_INET

```
int AF_INET = 0x2
```

IPv4 Internet protocols

## const AF_INET6

```
int AF_INET6 = 0xa
```

IPv6 Internet protocols

## const AF_UNIX

```
int AF_UNIX = 0x1
```

Unix domain sockets

## const SOCK_STREAM

```
int SOCK_STREAM = 0x1
```

TCP socket type

## const SOCK_DGRAM

```
int SOCK_DGRAM = 0x2
```

UDP socket type

## fn call6

```
fn call6(int number, anyptr a1, anyptr a2, anyptr a3, anyptr a4, anyptr a5, anyptr a6):int!
```

Low-level system call interface with up to 6 arguments

## fn open

```
fn open(string filename, int flags, int perm):int!
```

Open a file and return file descriptor

## fn read

```
fn read(int fd, anyptr buf, int len):int!
```

Read data from file descriptor into buffer

## fn readlink

```
fn readlink(string file, [u8] buf):int!
```

Read value of symbolic link

## fn write

```
fn write(int fd, anyptr buf, int len):int!
```

Write data from buffer to file descriptor

## fn close

```
fn close(int fd):void!
```

Close file descriptor

## fn unlink

```
fn unlink(string path):void!
```

Remove file from filesystem

## fn seek

```
fn seek(int fd, int offset, int whence):int!
```

Change file position for read/write operations

## fn fork

```
fn fork():int!
```

Create new process by duplicating current process

## fn exec

```
fn exec(string path, [string] argv, [string] envp):void!
```

Execute program at specified path with arguments and environment

## fn exit

```
fn exit(int status):void!
```

Terminate current process with exit status

## fn getpid

```
fn getpid():int!
```

Get current process ID

## fn getppid

```
fn getppid():int!
```

Get parent process ID

## fn getcwd

```
fn getcwd():string!
```

Get current working directory

## fn kill

```
fn kill(int pid, int sig):void!
```

Send signal to process

## fn wait

```
fn wait(int pid, int option):(int, int)!
```

Wait for child process to change state

## fn chdir

```
fn chdir(string path):void!
```

Change current working directory

## fn chroot

```
fn chroot(string path):void!
```

Change root directory

## fn chown

```
fn chown(string path, u32 uid, u32 gid):void!
```

Change file ownership

## fn chmod

```
fn chmod(string path, u32 mode):void!
```

Change file permissions

## fn mkdir

```
fn mkdir(string path, u32 mode):void!
```

Create directory

## fn rmdir

```
fn rmdir(string path):void!
```

Remove directory

## fn rename

```
fn rename(string oldpath, string newpath):void!
```

Rename file or directory

## fn gettime

```
fn gettime():timespec_t!
```

Get current system time

## fn socket

```
fn socket(int domain, int t, int protocol):int!
```

Create network socket

## fn bind

```
fn bind<T>(int sockfd, T addr):void!
```

Bind socket to address

## fn bind6

```
fn bind6(int sockfd, sockaddr_in6 addr):void!
```

Bind IPv6 socket to address

## fn listen

```
fn listen(int sockfd, int backlog):void!
```

Listen for connections on socket

## fn accept

```
fn accept<T>(int sockfd, ref<T> addr):int!
```

Accept connection on socket

## fn recvfrom

```
fn recvfrom(int sockfd, [u8] buf, int flags):int!
```

Receive data from socket

## fn sendto

```
fn sendto(int sockfd, [u8] buf, int flags):int!
```

Send data to socket

## fn get_envs

```
fn get_envs():[string]
```

Get all environment variables

## fn get_env

```
fn get_env(string key):string
```

Get environment variable value

## fn set_env

```
fn set_env(string key, string value):void!
```

Set environment variable

## fn unshare

```
fn unshare(int flags):void!
```

Create new namespace (Linux only)

## fn mount

```
fn mount(string source, string target, string fs_type, u32 flags, string data):void!
```

Mount filesystem

## fn umount

```
fn umount(string target, u32 flags):void!
```

Unmount filesystem

## type timespec_t

```
type timespec_t = struct {
    i64 sec
    i64 nsec
}
```

Time specification with seconds and nanoseconds

## type stat_t

```
type stat_t = struct {
    u64 dev
    u64 ino
    u64 nlink
    u32 mode
    u32 uid
    u32 gid
    u32 __pad0
    u64 rdev
    i64 size
    i64 blksize
    i64 blocks
    timespec_t atim
    timespec_t mtim
    timespec_t ctim
    [i64;3] __unused
}
```

File status information structure

### fn stat

```
fn stat(string filename):stat_t!
```

Get file status by filename

### fn fstat

```
fn fstat(int fd):stat_t!
```

Get file status by file descriptor

### fn stat_is_blk

```
fn stat_is_blk(u32 mode):bool
```

Check if file is block device

### fn stat_is_chr

```
fn stat_is_chr(u32 mode):bool
```

Check if file is character device

### fn stat_is_dir

```
fn stat_is_dir(u32 mode):bool
```

Check if file is directory

### fn stat_is_fifo

```
fn stat_is_fifo(u32 mode):bool
```

Check if file is FIFO/pipe

### fn stat_is_reg

```
fn stat_is_reg(u32 mode):bool
```

Check if file is regular file

### fn stat_is_lnk

```
fn stat_is_lnk(u32 mode):bool
```

Check if file is symbolic link

### fn stat_is_sock

```
fn stat_is_sock(u32 mode):bool
```

Check if file is socket

## type sockaddr_in

```
type sockaddr_in = struct {
    u16 sin_family
    u16 sin_port
    u32 sin_addr
    [u8;8] sin_zero
}
```

IPv4 socket address structure

## type sockaddr_in6

```
type sockaddr_in6 = struct {
    u8 sin6_len
    u8 sin6_family
    u16 sin6_port
    u32 sin6_flowinfo
    [u32;4] sin6_addr
    u32 sin6_scope_id
}
```

IPv6 socket address structure

## type sockaddr_un

```
type sockaddr_un = struct {
    u8 sun_len
    u8 sun_family
    [u8;104] sun_path
}
```

Unix domain socket address structure
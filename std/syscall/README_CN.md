# [import syscall](https://github.com/nature-lang/nature/blob/master/std/syscall/main.n)

系统调用接口，提供对操作系统服务的底层访问，包括文件操作、进程管理、网络等功能。

## const MS_ASYNC

```
u32 MS_ASYNC = 0x1
```

异步操作的内存同步标志

## const MS_INVALIDATE

```
u32 MS_INVALIDATE = 0x2
```

使缓存数据无效的内存同步标志

## const MS_SYNC

```
u32 MS_SYNC = 0x10
```

同步操作的内存同步标志

## const O_RDONLY

```
int O_RDONLY = 0x0000
```

以只读模式打开文件

## const O_WRONLY

```
int O_WRONLY = 0x0001
```

以只写模式打开文件

## const O_RDWR

```
int O_RDWR = 0x0002
```

以读写模式打开文件

## const O_APPEND

```
int O_APPEND = 0x0008
```

以追加模式打开文件，写入操作在文件末尾进行

## const O_CREAT

```
int O_CREAT = 0x0200
```

如果文件不存在则创建文件

## const O_EXCL

```
int O_EXCL = 0x0800
```

与 O_CREAT 一起使用，文件必须不存在

## const O_TRUNC

```
int O_TRUNC = 0x0400
```

打开文件时将文件截断为零长度

## const SEEK_SET

```
int SEEK_SET = 0
```

从文件开头开始定位

## const SEEK_CUR

```
int SEEK_CUR = 1
```

从当前位置开始定位

## const SEEK_END

```
int SEEK_END = 2
```

从文件末尾开始定位

## const STDIN

```
int STDIN = 0
```

标准输入文件描述符

## const STDOUT

```
int STDOUT = 1
```

标准输出文件描述符

## const STDERR

```
int STDERR = 2
```

标准错误文件描述符

## const SIGHUP

```
int SIGHUP = 1
```

挂起信号

## const SIGINT

```
int SIGINT = 2
```

中断信号 (Ctrl+C)

## const SIGQUIT

```
int SIGQUIT = 3
```

退出信号

## const SIGKILL

```
int SIGKILL = 9
```

杀死信号（无法被捕获或忽略）

## const SIGTERM

```
int SIGTERM = 15
```

终止信号

## const AF_INET

```
int AF_INET = 0x2
```

IPv4 网络协议

## const AF_INET6

```
int AF_INET6 = 0xa
```

IPv6 网络协议

## const AF_UNIX

```
int AF_UNIX = 0x1
```

Unix 域套接字

## const SOCK_STREAM

```
int SOCK_STREAM = 0x1
```

TCP 套接字类型

## const SOCK_DGRAM

```
int SOCK_DGRAM = 0x2
```

UDP 套接字类型

## fn call6

```
fn call6(int number, anyptr a1, anyptr a2, anyptr a3, anyptr a4, anyptr a5, anyptr a6):int!
```

底层系统调用接口，支持最多 6 个参数

## fn open

```
fn open(string filename, int flags, int perm):int!
```

打开文件并返回文件描述符

## fn read

```
fn read(int fd, anyptr buf, int len):int!
```

从文件描述符读取数据到缓冲区

## fn readlink

```
fn readlink(string file, [u8] buf):int!
```

读取符号链接的值

## fn write

```
fn write(int fd, anyptr buf, int len):int!
```

从缓冲区写入数据到文件描述符

## fn close

```
fn close(int fd):void!
```

关闭文件描述符

## fn unlink

```
fn unlink(string path):void!
```

从文件系统中删除文件

## fn seek

```
fn seek(int fd, int offset, int whence):int!
```

改变文件读写操作的位置

## fn fork

```
fn fork():int!
```

通过复制当前进程创建新进程

## fn exec

```
fn exec(string path, [string] argv, [string] envp):void!
```

在指定路径执行程序，传入参数和环境变量

## fn exit

```
fn exit(int status):void!
```

以指定退出状态终止当前进程

## fn getpid

```
fn getpid():int!
```

获取当前进程 ID

## fn getppid

```
fn getppid():int!
```

获取父进程 ID

## fn getcwd

```
fn getcwd():string!
```

获取当前工作目录

## fn kill

```
fn kill(int pid, int sig):void!
```

向进程发送信号

## fn wait

```
fn wait(int pid, int option):(int, int)!
```

等待子进程状态改变

## fn chdir

```
fn chdir(string path):void!
```

改变当前工作目录

## fn chroot

```
fn chroot(string path):void!
```

改变根目录

## fn chown

```
fn chown(string path, u32 uid, u32 gid):void!
```

改变文件所有权

## fn chmod

```
fn chmod(string path, u32 mode):void!
```

改变文件权限

## fn mkdir

```
fn mkdir(string path, u32 mode):void!
```

创建目录

## fn rmdir

```
fn rmdir(string path):void!
```

删除目录

## fn rename

```
fn rename(string oldpath, string newpath):void!
```

重命名文件或目录

## fn gettime

```
fn gettime():timespec_t!
```

获取当前系统时间

## fn socket

```
fn socket(int domain, int t, int protocol):int!
```

创建网络套接字

## fn bind

```
fn bind<T>(int sockfd, T addr):void!
```

将套接字绑定到地址

## fn bind6

```
fn bind6(int sockfd, sockaddr_in6 addr):void!
```

将 IPv6 套接字绑定到地址

## fn listen

```
fn listen(int sockfd, int backlog):void!
```

在套接字上监听连接

## fn accept

```
fn accept<T>(int sockfd, ref<T> addr):int!
```

接受套接字上的连接

## fn recvfrom

```
fn recvfrom(int sockfd, [u8] buf, int flags):int!
```

从套接字接收数据

## fn sendto

```
fn sendto(int sockfd, [u8] buf, int flags):int!
```

向套接字发送数据

## fn get_envs

```
fn get_envs():[string]
```

获取所有环境变量

## fn get_env

```
fn get_env(string key):string
```

获取环境变量值

## fn set_env

```
fn set_env(string key, string value):void!
```

设置环境变量

## fn unshare

```
fn unshare(int flags):void!
```

创建新的命名空间（仅限 Linux）

## fn mount

```
fn mount(string source, string target, string fs_type, u32 flags, string data):void!
```

挂载文件系统

## fn umount

```
fn umount(string target, u32 flags):void!
```

卸载文件系统

## type timespec_t

```
type timespec_t = struct {
    i64 sec
    i64 nsec
}
```

时间规格，包含秒和纳秒

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

文件状态信息结构体

### fn stat

```
fn stat(string filename):stat_t!
```

通过文件名获取文件状态

### fn fstat

```
fn fstat(int fd):stat_t!
```

通过文件描述符获取文件状态

### fn stat_is_blk

```
fn stat_is_blk(u32 mode):bool
```

检查文件是否为块设备

### fn stat_is_chr

```
fn stat_is_chr(u32 mode):bool
```

检查文件是否为字符设备

### fn stat_is_dir

```
fn stat_is_dir(u32 mode):bool
```

检查文件是否为目录

### fn stat_is_fifo

```
fn stat_is_fifo(u32 mode):bool
```

检查文件是否为 FIFO/管道

### fn stat_is_reg

```
fn stat_is_reg(u32 mode):bool
```

检查文件是否为普通文件

### fn stat_is_lnk

```
fn stat_is_lnk(u32 mode):bool
```

检查文件是否为符号链接

### fn stat_is_sock

```
fn stat_is_sock(u32 mode):bool
```

检查文件是否为套接字

## type sockaddr_in

```
type sockaddr_in = struct {
    u16 sin_family
    u16 sin_port
    u32 sin_addr
    [u8;8] sin_zero
}
```

IPv4 套接字地址结构体

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

IPv6 套接字地址结构体

## type sockaddr_un

```
type sockaddr_un = struct {
    u8 sun_len
    u8 sun_family
    [u8;104] sun_path
}
```

Unix 域套接字地址结构体
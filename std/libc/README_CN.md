# import [libc](https://github.com/nature-lang/nature/tree/master/std/libc/main.n)

C 标准库绑定，提供内存管理、字符串操作、系统函数等功能

## type cstr

```
type cstr = anyptr
```

C 风格空字符结尾字符串类型

## type fileptr

```
type fileptr = anyptr
```

文件指针类型，用于文件操作

## type div_t

```
type div_t = struct {
    i32 quot
    i32 rem
}
```

整数除法结果结构，包含商和余数

## type ldiv_t

```
type ldiv_t = struct {
    i64 quot
    i64 rem
}
```

长整数除法结果结构，包含商和余数

## type fpos_t

```
type fpos_t = struct {
    [u8;16] __opaque
    i64 __lldata
    f64 __align
}
```

文件位置类型，用于文件定位操作

## type timespec

```
type timespec = struct {
    i64 tv_sec
    i64 tv_nsec
}
```

时间规格结构，包含秒和纳秒

## type itimerspec

```
type itimerspec = struct {
    timespec it_interval
    timespec it_value
}
```

定时器规格结构，用于间隔定时器

## type tm

```
type tm = struct {
    i32 tm_sec
    i32 tm_min
    i32 tm_hour
    i32 tm_mday
    i32 tm_mon
    i32 tm_year
    i32 tm_wday
    i32 tm_yday
    i32 tm_isdst
    i64 __tm_gmtoff
    cstr __tm_zone
}
```

分解时间结构，用于时间操作

## type flock

```
type flock = struct {
    i16 l_type
    i16 l_whence
    off_t l_start
    off_t l_len
    pid_t l_pid
}
```

文件锁结构，用于文件锁定操作

## type sigset_t

```
type sigset_t = struct {
    [u64;16] __val
}
```

信号集类型，用于信号处理

## type signalfd_siginfo_t

```
type signalfd_siginfo_t = struct {
    u32 ssi_signo
    i32 ssi_errno
    i32 ssi_code
    u32 ssi_pid
    u32 ssi_uid
    i32 ssi_fd
    u32 ssi_tid
    u32 ssi_band
    u32 ssi_overrun
    u32 ssi_trapno
    i32 ssi_status
    i32 ssi_int
    u64 ssi_ptr
    u64 ssi_utime
    u64 ssi_stime
    u64 ssi_addr
    u16 ssi_addr_lsb
    u32 __pad2
    i32 ssi_syscall
    u64 ssi_call_addr
    u32 ssi_arch
    [u8;48] __pad
}
```

信号文件描述符信息结构

## type dir_t

```
type dir_t = anyptr
```

目录流类型，用于目录操作

## type dirent_t

```
type dirent_t = struct {
    u64 ino
    i64 off
    u16 reclen
    u8 t
    [u8;256] name
}
```

目录条目结构，包含文件信息

## fn string.to_cstr

```
fn string.to_cstr(self):cstr
```

将 Nature 字符串转换为 C 风格字符串

## fn cstr.to_string

```
fn cstr.to_string():string
```

将 C 风格字符串转换为 Nature 字符串

## fn atoi

```
fn atoi(cstr str):i32
```

将字符串转换为整数

## fn atol

```
fn atol(cstr str):i64
```

将字符串转换为长整数

## fn atof

```
fn atof(cstr str):f64
```

将字符串转换为双精度浮点数

## fn strtof

```
fn strtof(cstr str, anyptr endptr):f32
```

将字符串转换为单精度浮点数

## fn strtod

```
fn strtod(cstr str, anyptr endptr):f64
```

将字符串转换为双精度浮点数（带结束指针）

## fn strtol

```
fn strtol(cstr str, anyptr endptr, i32 base):i64
```

将字符串转换为指定进制的长整数

## fn strtoul

```
fn strtoul(cstr str, anyptr endptr, i32 base):u64
```

将字符串转换为指定进制的无符号长整数

## fn rand

```
fn rand():i32
```

生成伪随机数

## fn srand

```
fn srand(u32 seed):void
```

设置伪随机数生成器种子

## fn malloc

```
fn malloc(u64 size):anyptr
```

分配指定大小的内存

## fn calloc

```
fn calloc(u64 nmemb, u64 size):anyptr
```

为数组分配内存并初始化为零

## fn realloc

```
fn realloc(anyptr p, u64 size):anyptr
```

重新分配内存为新大小

## fn free

```
fn free(anyptr p):void
```

释放已分配的内存

## fn aligned_alloc

```
fn aligned_alloc(u64 alignment, u64 size):anyptr
```

分配对齐内存

## fn abort

```
fn abort()
```

异常终止程序

## fn atexit

```
fn atexit(anyptr func):i32
```

注册程序终止时调用的函数

## fn exit

```
fn exit(i32 status)
```

以退出状态终止程序

## fn _exit

```
fn _exit(i32 status)
```

立即终止程序

## fn at_quick_exit

```
fn at_quick_exit(anyptr func):i32
```

注册快速退出函数

## fn quick_exit

```
fn quick_exit(i32 status)
```

快速程序终止

## fn getenv

```
fn getenv(cstr name):cstr
```

获取环境变量值

## fn system

```
fn system(cstr command):i32
```

执行系统命令

## fn abs

```
fn abs(i32 x):i32
```

计算整数的绝对值

## fn labs

```
fn labs(i64 x):i64
```

计算长整数的绝对值

## fn div

```
fn div(i32 numer, i32 denom):div_t
```

整数除法并返回商和余数

## fn ldiv

```
fn ldiv(i64 numer, i64 denom):ldiv_t
```

长整数除法并返回商和余数

## fn mblen

```
fn mblen(cstr s, u64 n):i32
```

获取多字节字符长度

## fn mbtowc

```
fn mbtowc(anyptr pwc, cstr s, u64 n):i32
```

将多字节字符转换为宽字符

## fn wctomb

```
fn wctomb(cstr s, i32 wc):i32
```

将宽字符转换为多字节字符

## fn mbstowcs

```
fn mbstowcs(anyptr pwcs, cstr s, u64 n):u64
```

将多字节字符串转换为宽字符串

## fn wcstombs

```
fn wcstombs(cstr s, anyptr pwcs, u64 n):u64
```

将宽字符串转换为多字节字符串

## fn posix_memalign

```
fn posix_memalign(anyptr memptr, u64 alignment, u64 size):i32
```

分配对齐内存（POSIX）

## fn setenv

```
fn setenv(cstr name, cstr value, i32 overwrite):i32
```

设置环境变量

## fn unsetenv

```
fn unsetenv(cstr name):i32
```

取消设置环境变量

## fn putenv

```
fn putenv(cstr str):i32
```

添加或更改环境变量

## fn mkstemp

```
fn mkstemp(cstr template):i32
```

创建唯一临时文件

## fn mkostemp

```
fn mkostemp(cstr template, i32 flags):i32
```

创建带标志的唯一临时文件

## fn mkdtemp

```
fn mkdtemp(cstr template):cstr
```

创建唯一临时目录

## fn mktemp

```
fn mktemp(cstr template):cstr
```

生成唯一临时文件名

## fn mkstemps

```
fn mkstemps(cstr template, i32 suffixlen):i32
```

创建带后缀的唯一临时文件

## fn mkostemps

```
fn mkostemps(cstr template, i32 suffixlen, i32 flags):i32
```

创建带后缀和标志的唯一临时文件

## fn getsubopt

```
fn getsubopt(anyptr optionp, anyptr tokens, anyptr valuep):i32
```

解析子选项参数

## fn rand_r

```
fn rand_r(ptr<u32> seed):i32
```

线程安全的随机数生成器

## fn realpath

```
fn realpath(cstr path, cstr resolved_path):cstr
```

将路径名解析为绝对路径

## fn random

```
fn random():i64
```

生成随机数

## fn srandom

```
fn srandom(u32 seed):void
```

设置随机数生成器种子

## fn initstate

```
fn initstate(u32 seed, cstr state, u64 size):cstr
```

初始化随机数生成器状态

## fn setstate

```
fn setstate(cstr state):cstr
```

设置随机数生成器状态

## fn posix_openpt

```
fn posix_openpt(i32 flags):i32
```

打开伪终端主设备

## fn grantpt

```
fn grantpt(i32 fd):i32
```

授予伪终端从设备访问权限

## fn unlockpt

```
fn unlockpt(i32 fd):i32
```

解锁伪终端主/从设备对

## fn ptsname_r

```
fn ptsname_r(i32 fd, cstr buf, u64 buflen):i32
```

获取伪终端从设备名称（线程安全）

## fn l64a

```
fn l64a(i64 value):cstr
```

将长整数转换为基64 ASCII字符串

## fn a64l

```
fn a64l(cstr s):i64
```

将基64 ASCII字符串转换为长整数

## fn setkey

```
fn setkey(cstr key):void
```

设置加密密钥

## fn drand48

```
fn drand48():f64
```

生成均匀分布的随机数

## fn erand48

```
fn erand48(anyptr xsubi):f64
```

生成带种子的均匀分布随机数

## fn lrand48

```
fn lrand48():i64
```

生成非负随机长整数

## fn nrand48

```
fn nrand48(anyptr xsubi):i64
```

生成带种子的非负随机长整数

## fn mrand48

```
fn mrand48():i64
```

生成有符号随机长整数

## fn jrand48

```
fn jrand48(anyptr xsubi):i64
```

生成带种子的有符号随机长整数

## fn srand48

```
fn srand48(i64 seedval):void
```

设置48位随机数生成器种子

## fn seed48

```
fn seed48(anyptr seed16v):ptr<u16>
```

设置48位随机数生成器种子

## fn lcong48

```
fn lcong48(anyptr param):void
```

设置48位随机数生成器参数

## fn valloc

```
fn valloc(u64 size):anyptr
```

分配页对齐内存

## fn memalign

```
fn memalign(u64 alignment, u64 size):anyptr
```

分配对齐内存

## fn reallocarray

```
fn reallocarray(anyptr p, u64 nmemb, u64 size):anyptr
```

重新分配数组并检查溢出

## fn getloadavg

```
fn getloadavg(ptr<f64> loadavg, i32 nelem):i32
```

获取系统负载平均值

## fn ecvt

```
fn ecvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr
```

将浮点数转换为字符串（指数格式）

## fn fcvt

```
fn fcvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr
```

将浮点数转换为字符串（定点格式）

## fn gcvt

```
fn gcvt(f64 number, i32 ndigit, cstr buf):cstr
```

将浮点数转换为字符串（通用格式）

## fn secure_getenv

```
fn secure_getenv(cstr name):cstr
```

安全地获取环境变量

## fn fopen

```
fn fopen(cstr filename, cstr mode):fileptr
```

打开文件流

## fn freopen

```
fn freopen(cstr filename, cstr mode, fileptr stream):fileptr
```

重新打开文件流

## fn fclose

```
fn fclose(fileptr stream):i32
```

关闭文件流

## fn remove

```
fn remove(cstr filename):i32
```

删除文件或目录

## fn rename

```
fn rename(cstr old_name, cstr new_name):i32
```

重命名文件或目录

## fn feof

```
fn feof(fileptr stream):i32
```

测试文件结束指示器

## fn ferror

```
fn ferror(fileptr stream):i32
```

测试错误指示器

## fn fflush

```
fn fflush(fileptr stream):i32
```

刷新流缓冲区

## fn clearerr

```
fn clearerr(fileptr stream):void
```

清除错误和文件结束指示器

## fn fseek

```
fn fseek(fileptr stream, i64 offset, i32 whence):i32
```

设置文件位置

## fn ftell

```
fn ftell(fileptr stream):i64
```

获取文件位置

## fn rewind

```
fn rewind(fileptr stream):void
```

重置文件位置到开头

## fn fgetpos

```
fn fgetpos(fileptr stream, ptr<fpos_t> pos):i32
```

获取文件位置

## fn fsetpos

```
fn fsetpos(fileptr stream, ptr<fpos_t> pos):i32
```

设置文件位置

## fn fread

```
fn fread(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

从流读取数据

## fn fwrite

```
fn fwrite(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

向流写入数据

## fn fgetc

```
fn fgetc(fileptr stream):i32
```

从流获取字符

## fn getc

```
fn getc(fileptr stream):i32
```

从流获取字符（宏版本）

## fn getchar

```
fn getchar():i32
```

从标准输入获取字符

## fn ungetc

```
fn ungetc(i32 c, fileptr stream):i32
```

将字符推回流

## fn fputc

```
fn fputc(i32 c, fileptr stream):i32
```

向流放入字符

## fn putc

```
fn putc(i32 c, fileptr stream):i32
```

向流放入字符（宏版本）

## fn putchar

```
fn putchar(i32 c):i32
```

向标准输出放入字符

## fn fgets

```
fn fgets(cstr s, i32 size, fileptr stream):cstr
```

从流获取字符串

## fn fputs

```
fn fputs(cstr s, fileptr stream):i32
```

向流放入字符串

## fn puts

```
fn puts(cstr s):i32
```

向标准输出放入字符串并换行

## fn perror

```
fn perror(cstr s):void
```

打印错误消息

## fn setvbuf

```
fn setvbuf(fileptr stream, cstr buffer, i32 mode, u64 size):i32
```

设置流缓冲

## fn setbuf

```
fn setbuf(fileptr stream, cstr buffer):void
```

设置流缓冲区

## fn tmpnam

```
fn tmpnam(cstr s):cstr
```

生成临时文件名

## fn tmpfile

```
fn tmpfile():fileptr
```

创建临时文件

## fn fmemopen

```
fn fmemopen(anyptr buffer, u64 size, cstr mode):fileptr
```

将内存作为流打开

## fn open_memstream

```
fn open_memstream(ptr<cstr> bufp, ptr<u64> sizep):fileptr
```

打开动态内存流

## fn fdopen

```
fn fdopen(i32 fd, cstr mode):fileptr
```

从文件描述符打开流

## fn popen

```
fn popen(cstr command, cstr t):fileptr
```

打开到进程的管道

## fn pclose

```
fn pclose(fileptr stream):i32
```

关闭管道流

## fn fileno

```
fn fileno(fileptr stream):i32
```

从流获取文件描述符

## fn fseeko

```
fn fseeko(fileptr stream, i64 offset, i32 whence):i32
```

设置文件位置（大文件支持）

## fn ftello

```
fn ftello(fileptr stream):i64
```

获取文件位置（大文件支持）

## fn flockfile

```
fn flockfile(fileptr stream):void
```

锁定文件流

## fn ftrylockfile

```
fn ftrylockfile(fileptr stream):i32
```

尝试锁定文件流

## fn funlockfile

```
fn funlockfile(fileptr stream):void
```

解锁文件流

## fn getc_unlocked

```
fn getc_unlocked(fileptr stream):i32
```

无锁获取字符

## fn getchar_unlocked

```
fn getchar_unlocked():i32
```

无锁从标准输入获取字符

## fn putc_unlocked

```
fn putc_unlocked(i32 c, fileptr stream):i32
```

无锁放入字符

## fn putchar_unlocked

```
fn putchar_unlocked(i32 c):i32
```

无锁向标准输出放入字符

## fn fgetc_unlocked

```
fn fgetc_unlocked(fileptr stream):i32
```

无锁从流获取字符

## fn fputc_unlocked

```
fn fputc_unlocked(i32 c, fileptr stream):i32
```

无锁向流放入字符

## fn fflush_unlocked

```
fn fflush_unlocked(fileptr stream):i32
```

无锁刷新流

## fn fread_unlocked

```
fn fread_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

无锁读取数据

## fn fwrite_unlocked

```
fn fwrite_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

无锁写入数据

## fn clearerr_unlocked

```
fn clearerr_unlocked(fileptr stream):void
```

无锁清除错误指示器

## fn feof_unlocked

```
fn feof_unlocked(fileptr stream):i32
```

无锁测试文件结束

## fn ferror_unlocked

```
fn ferror_unlocked(fileptr stream):i32
```

无锁测试错误指示器

## fn fileno_unlocked

```
fn fileno_unlocked(fileptr stream):i32
```

无锁获取文件描述符

## fn fgets_unlocked

```
fn fgets_unlocked(cstr s, i32 size, fileptr stream):cstr
```

无锁获取字符串

## fn fputs_unlocked

```
fn fputs_unlocked(cstr s, fileptr stream):i32
```

无锁放入字符串

## fn getdelim

```
fn getdelim(ptr<cstr> lineptr, ptr<u64> n, i32 delim, fileptr stream):i64
```

从流读取分隔字符串

## fn getline

```
fn getline(ptr<cstr> lineptr, ptr<u64> n, fileptr stream):i64
```

从流读取行

## fn renameat

```
fn renameat(i32 olddirfd, cstr oldpath, i32 newdirfd, cstr newpath):i32
```

相对于目录描述符重命名文件

## fn tempnam

```
fn tempnam(cstr dir, cstr pfx):cstr
```

生成带目录和前缀的临时文件名

## fn cuserid

```
fn cuserid(cstr s):cstr
```

获取用户登录名

## fn setlinebuf

```
fn setlinebuf(fileptr stream):void
```

设置行缓冲

## fn setbuffer

```
fn setbuffer(fileptr stream, cstr buffer, u64 size):void
```

设置带大小的流缓冲区

## fn getw

```
fn getw(fileptr stream):i32
```

从流获取字

## fn putw

```
fn putw(i32 w, fileptr stream):i32
```

向流放入字

## fn fgetln

```
fn fgetln(fileptr stream, ptr<u64> len):cstr
```

从流获取行

## fn fopencookie

```
fn fopencookie(anyptr cookie, cstr mode, cookie_io_functions_t io_funcs):fileptr
```

使用cookie打开自定义流

## fn memcpy

```
fn memcpy(anyptr dst, anyptr src, u64 n):anyptr
```

复制内存块

## fn memmove

```
fn memmove(anyptr dst, anyptr src, u64 n):anyptr
```

移动内存块（处理重叠）

## fn memset

```
fn memset(anyptr s, i32 c, u64 n):anyptr
```

用常量字节填充内存

## fn memcmp

```
fn memcmp(anyptr s1, anyptr s2, u64 n):i32
```

比较内存块

## fn memchr

```
fn memchr(anyptr s, i32 c, u64 n):anyptr
```

在内存块中查找字节

## fn strcpy

```
fn strcpy(cstr dst, cstr src):cstr
```

复制字符串

## fn strncpy

```
fn strncpy(cstr dst, cstr src, u64 n):cstr
```

复制字符串（限制长度）

## fn strcat

```
fn strcat(cstr dst, cstr src):cstr
```

连接字符串

## fn strncat

```
fn strncat(cstr dst, cstr src, u64 n):cstr
```

连接字符串（限制长度）

## fn strcmp

```
fn strcmp(cstr s1, cstr s2):i32
```

比较字符串

## fn strncmp

```
fn strncmp(cstr s1, cstr s2, u64 n):i32
```

比较字符串（限制长度）

## fn strcoll

```
fn strcoll(cstr s1, cstr s2):i32
```

使用区域设置比较字符串

## fn strxfrm

```
fn strxfrm(cstr dst, cstr src, u64 n):u64
```

为区域设置比较转换字符串

## fn strchr

```
fn strchr(cstr s, i32 c):cstr
```

在字符串中查找字符

## fn strrchr

```
fn strrchr(cstr s, i32 c):cstr
```

在字符串中查找字符的最后出现

## fn strcspn

```
fn strcspn(cstr s1, cstr s2):u64
```

获取补集子字符串长度

## fn strspn

```
fn strspn(cstr s1, cstr s2):u64
```

获取子字符串长度

## fn strpbrk

```
fn strpbrk(cstr s1, cstr s2):cstr
```

查找字符的第一次出现

## fn strstr

```
fn strstr(cstr haystack, cstr needle):cstr
```

查找子字符串

## fn strtok

```
fn strtok(cstr str, cstr delim):cstr
```

将字符串分割为标记

## fn strlen

```
fn strlen(cstr s):u64
```

获取字符串长度

## fn error_string

```
fn error_string():string
```

获取错误字符串

## fn strerror

```
fn strerror(i32 errnum):cstr
```

获取错误消息字符串

## fn strtok_r

```
fn strtok_r(cstr str, cstr delim, ptr<cstr> saveptr):cstr
```

将字符串分割为标记（线程安全）

## fn strerror_r

```
fn strerror_r(i32 errnum, cstr buf, u64 buflen):i32
```

获取错误消息字符串（线程安全）

## fn stpcpy

```
fn stpcpy(cstr dst, cstr src):cstr
```

复制字符串并返回指向末尾的指针

## fn stpncpy

```
fn stpncpy(cstr dst, cstr src, u64 n):cstr
```

复制字符串（限制长度）并返回指向末尾的指针

## fn strnlen

```
fn strnlen(cstr s, u64 maxlen):u64
```

获取字符串长度（最大值）

## fn strdup

```
fn strdup(cstr s):cstr
```

复制字符串

## fn strndup

```
fn strndup(cstr s, u64 n):cstr
```

复制字符串（限制长度）

## fn strsignal

```
fn strsignal(i32 sig):cstr
```

获取信号描述字符串

## fn strerror_l

```
fn strerror_l(i32 errnum, locale_t locale):cstr
```

获取带区域设置的错误消息字符串

## fn strcoll_l

```
fn strcoll_l(cstr s1, cstr s2, locale_t locale):cstr
```

使用指定区域设置比较字符串

## fn strxfrm_l

```
fn strxfrm_l(cstr dst, cstr src, u64 n, locale_t locale):u64
```

为指定区域设置比较转换字符串

## fn memmem

```
fn memmem(anyptr haystack, u64 haystacklen, anyptr needle, u64 needlelen):anyptr
```

在内存中查找内存块

## fn memccpy

```
fn memccpy(anyptr dst, anyptr src, i32 c, u64 n):anyptr
```

复制内存直到找到字符

## fn strsep

```
fn strsep(ptr<cstr> strp, cstr delim):cstr
```

分离字符串

## fn strlcat

```
fn strlcat(cstr dst, cstr src, u64 size):u64
```

安全连接字符串

## fn strlcpy

```
fn strlcpy(cstr dst, cstr src, u64 size):u64
```

安全复制字符串

## fn explicit_bzero

```
fn explicit_bzero(anyptr s, u64 n):void
```

安全地清零内存

## fn strverscmp

```
fn strverscmp(cstr s1, cstr s2):i32
```

使用版本号语义比较字符串

## fn strchrnul

```
fn strchrnul(cstr s, i32 c):cstr
```

查找字符或字符串末尾

## fn strcasestr

```
fn strcasestr(cstr haystack, cstr needle):cstr
```

不区分大小写查找子字符串

## fn memrchr

```
fn memrchr(anyptr s, i32 c, u64 n):anyptr
```

查找内存中字节的最后出现

## fn mempcpy

```
fn mempcpy(anyptr dst, anyptr src, u64 n):anyptr
```

复制内存并返回指向末尾的指针

## fn acos

```
fn acos(f64 x):f64
```

计算反余弦

## fn acosf

```
fn acosf(f32 x):f32
```

计算反余弦（单精度）

## fn acosh

```
fn acosh(f64 x):f64
```

计算反双曲余弦

## fn acoshf

```
fn acoshf(f32 x):f32
```

计算反双曲余弦（单精度）

## fn asin

```
fn asin(f64 x):f64
```

计算反正弦

## fn asinf

```
fn asinf(f32 x):f32
```

计算反正弦（单精度）

## fn asinh

```
fn asinh(f64 x):f64
```

计算反双曲正弦

## fn asinhf

```
fn asinhf(f32 x):f32
```

计算反双曲正弦（单精度）

## fn atan

```
fn atan(f64 x):f64
```

计算反正切

## fn atanf

```
fn atanf(f32 x):f32
```

计算反正切（单精度）

## fn atan2

```
fn atan2(f64 y, f64 x):f64
```

计算 y/x 的反正切

## fn atan2f

```
fn atan2f(f32 y, f32 x):f32
```

计算 y/x 的反正切（单精度）

## fn cos

```
fn cos(f64 x):f64
```

计算余弦

## fn cosf

```
fn cosf(f32 x):f32
```

计算余弦（单精度）

## fn sin

```
fn sin(f64 x):f64
```

计算正弦

## fn sinf

```
fn sinf(f32 x):f32
```

计算正弦（单精度）

## fn tan

```
fn tan(f64 x):f64
```

计算正切

## fn tanf

```
fn tanf(f32 x):f32
```

计算正切（单精度）

## fn atanh

```
fn atanh(f64 x):f64
```

计算反双曲正切

## fn atanhf

```
fn atanhf(f32 x):f32
```

计算反双曲正切（单精度）

## fn cosh

```
fn cosh(f64 x):f64
```

计算双曲余弦

## fn coshf

```
fn coshf(f32 x):f32
```

计算双曲余弦（单精度）

## fn sinh

```
fn sinh(f64 x):f64
```

计算双曲正弦

## fn sinhf

```
fn sinhf(f32 x):f32
```

计算双曲正弦（单精度）

## fn sqrt

```
fn sqrt(f64 x):f64
```

计算平方根

## fn sqrtf

```
fn sqrtf(f32 x):f32
```

计算平方根（单精度）

## fn tanh

```
fn tanh(f64 x):f64
```

计算双曲正切

## fn tanhf

```
fn tanhf(f32 x):f32
```

计算双曲正切（单精度）

## fn exp

```
fn exp(f64 x):f64
```

计算指数函数

## fn expf

```
fn expf(f32 x):f32
```

计算指数函数（单精度）

## fn exp2

```
fn exp2(f64 x):f64
```

计算以2为底的指数函数

## fn exp2f

```
fn exp2f(f32 x):f32
```

计算以2为底的指数函数（单精度）

## fn expm1

```
fn expm1(f64 x):f64
```

计算 exp(x) - 1

## fn expm1f

```
fn expm1f(f32 x):f32
```

计算 exp(x) - 1（单精度）

## fn fabs

```
fn fabs(f64 x):f64
```

计算浮点数的绝对值

## fn fabsf

```
fn fabsf(f32 x):f32
```

计算浮点数的绝对值（单精度）

## fn log

```
fn log(f64 x):f64
```

计算自然对数

## fn logf

```
fn logf(f32 x):f32
```

计算自然对数（单精度）

## fn log10

```
fn log10(f64 x):f64
```

计算以10为底的对数

## fn log10f

```
fn log10f(f32 x):f32
```

计算以10为底的对数（单精度）

## fn log1p

```
fn log1p(f64 x):f64
```

计算 log(1 + x)

## fn log1pf

```
fn log1pf(f32 x):f32
```

计算 log(1 + x)（单精度）

## fn log2

```
fn log2(f64 x):f64
```

计算以2为底的对数

## fn log2f

```
fn log2f(f32 x):f32
```

计算以2为底的对数（单精度）

## fn logb

```
fn logb(f64 x):f64
```

提取指数

## fn logbf

```
fn logbf(f32 x):f32
```

提取指数（单精度）

## fn pow

```
fn pow(f64 x, f64 y):f64
```

计算 x 的 y 次幂

## fn powf

```
fn powf(f32 x, f32 y):f32
```

计算 x 的 y 次幂（单精度）

## fn cbrt

```
fn cbrt(f64 x):f64
```

计算立方根

## fn cbrtf

```
fn cbrtf(f32 x):f32
```

计算立方根（单精度）

## fn hypot

```
fn hypot(f64 x, f64 y):f64
```

计算斜边长度

## fn hypotf

```
fn hypotf(f32 x, f32 y):f32
```

计算斜边长度（单精度）

## fn ceil

```
fn ceil(f64 x):f64
```

计算向上取整函数

## fn ceilf

```
fn ceilf(f32 x):f32
```

计算向上取整函数（单精度）

## fn floor

```
fn floor(f64 x):f64
```

计算向下取整函数

## fn floorf

```
fn floorf(f32 x):f32
```

计算向下取整函数（单精度）

## fn trunc

```
fn trunc(f64 x):f64
```

截断为整数

## fn truncf

```
fn truncf(f32 x):f32
```

截断为整数（单精度）

## fn round

```
fn round(f64 x):f64
```

四舍五入到最近整数

## fn roundf

```
fn roundf(f32 x):f32
```

四舍五入到最近整数（单精度）

## fn fmod

```
fn fmod(f64 x, f64 y):f64
```

计算浮点余数

## fn fmodf

```
fn fmodf(f32 x, f32 y):f32
```

计算浮点余数（单精度）

## fn std_args

```
fn std_args():[string]
```

获取命令行参数

## fn htons

```
fn htons(u16 host):u16
```

将主机字节序转换为网络字节序（16位）

## fn waitpid

```
fn waitpid(int pid, ptr<int> status, int options):int
```

等待进程状态改变

## fn sigemptyset

```
fn sigemptyset(ref<sigset_t> sigset):i32
```

初始化信号集为空

## fn sigaddset

```
fn sigaddset(ref<sigset_t> sigset, i32 signo):i32
```

向信号集添加信号

## fn sigfillset

```
fn sigfillset(ref<sigset_t> sigset):i32
```

初始化信号集为满

## fn sigprocmask

```
fn sigprocmask(i32 how, ref<sigset_t> sigset, ptr<sigset_t> oldset):i32
```

检查和更改阻塞信号

## fn signalfd

```
fn signalfd(int fd, ref<sigset_t> mask, i32 flags):i32
```

创建用于接受信号的文件描述符

## fn prctl

```
fn prctl(int option, u64 arg2, u64 arg3, u64 arg4, u64 arg5):int
```

对进程进行操作

## fn uv_hrtime

```
fn uv_hrtime():u64
```

获取高分辨率时间

## fn errno

```
fn errno():int
```

获取当前 errno 值

## fn get_envs

```
fn get_envs():[string]
```

获取所有环境变量

## fn fork

```
fn fork():int
```

创建子进程

## fn getcwd

```
fn getcwd(cstr path, uint size):cstr
```

获取当前工作目录

## fn mmap

```
fn mmap(anyptr addr, int len, int prot, int flags, int fd, int off):anyptr
```

将文件或设备映射到内存

## fn munmap

```
fn munmap(anyptr addr, int len)
```

从内存取消映射文件或设备

## fn isprint

```
fn isprint(u8 c):bool
```

测试是否为可打印字符

## fn isspace

```
fn isspace(u8 c):bool
```

测试是否为空白字符

# import [libc.dirent](https://github.com/nature-lang/nature/tree/master/std/libc/dirent.linux.n)

获取指定目录的相关信息，以及进行目录的递归遍历

## type dir_t
```
type dir_t = anyptr
```

## type dirent_t

```
type dirent_t = struct {
    u64 ino
    i64 off
    u16 reclen
    u8 t
    [u8;256] name
}

```


## fn opendir

```
fn opendir(anyptr str):dir_t
```

打开目录流

## fn readdir

```
fn readdir(dir_t d):ptr<dirent_t>
```

通过目录信息获取目录列表

## fn closedir

```
fn closedir(dir_t d):int
```

关闭目录流

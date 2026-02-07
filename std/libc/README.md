# import [libc](https://github.com/nature-lang/nature/tree/master/std/libc/main.n)

C standard library bindings for memory management, string operations, system functions, and more

## type cstr

```
type cstr = anyptr
```

C-style null-terminated string type

## type fileptr

```
type fileptr = anyptr
```

File pointer type for file operations

## type div_t

```
type div_t = struct {
    i32 quot
    i32 rem
}
```

Structure for division result with quotient and remainder

## type ldiv_t

```
type ldiv_t = struct {
    i64 quot
    i64 rem
}
```

Structure for long division result with quotient and remainder

## type fpos_t

```
type fpos_t = struct {
    [u8;16] __opaque
    i64 __lldata
    f64 __align
}
```

File position type for file positioning operations

## type timespec

```
type timespec = struct {
    i64 tv_sec
    i64 tv_nsec
}
```

Time specification structure with seconds and nanoseconds

## type itimerspec

```
type itimerspec = struct {
    timespec it_interval
    timespec it_value
}
```

Timer specification structure for interval timers

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

Broken-down time structure for time manipulation

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

File lock structure for file locking operations

## type sigset_t

```
type sigset_t = struct {
    [u64;16] __val
}
```

Signal set type for signal handling

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

Signal file descriptor information structure

## type dir_t

```
type dir_t = anyptr
```

Directory stream type for directory operations

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

Directory entry structure containing file information

## fn string.to_cstr

```
fn string.to_cstr(self):cstr
```

Convert Nature string to C-style string

## fn cstr.to_string

```
fn cstr.to_string():string
```

Convert C-style string to Nature string

## fn atoi

```
fn atoi(cstr str):i32
```

Convert string to integer

## fn atol

```
fn atol(cstr str):i64
```

Convert string to long integer

## fn atof

```
fn atof(cstr str):f64
```

Convert string to double-precision floating-point number

## fn strtof

```
fn strtof(cstr str, anyptr endptr):f32
```

Convert string to single-precision floating-point number

## fn strtod

```
fn strtod(cstr str, anyptr endptr):f64
```

Convert string to double-precision floating-point number with end pointer

## fn strtol

```
fn strtol(cstr str, anyptr endptr, i32 base):i64
```

Convert string to long integer with specified base

## fn strtoul

```
fn strtoul(cstr str, anyptr endptr, i32 base):u64
```

Convert string to unsigned long integer with specified base

## fn rand

```
fn rand():i32
```

Generate pseudo-random number

## fn srand

```
fn srand(u32 seed):void
```

Seed pseudo-random number generator

## fn malloc

```
fn malloc(u64 size):anyptr
```

Allocate memory of specified size

## fn calloc

```
fn calloc(u64 nmemb, u64 size):anyptr
```

Allocate memory for array and initialize to zero

## fn realloc

```
fn realloc(anyptr p, u64 size):anyptr
```

Reallocate memory to new size

## fn free

```
fn free(anyptr p):void
```

Free allocated memory

## fn aligned_alloc

```
fn aligned_alloc(u64 alignment, u64 size):anyptr
```

Allocate aligned memory

## fn abort

```
fn abort()
```

Abnormally terminate program

## fn atexit

```
fn atexit(anyptr func):i32
```

Register function to be called at program termination

## fn exit

```
fn exit(i32 status)
```

Terminate program with exit status

## fn _exit

```
fn _exit(i32 status)
```

Terminate program immediately

## fn at_quick_exit

```
fn at_quick_exit(anyptr func):i32
```

Register function for quick exit

## fn quick_exit

```
fn quick_exit(i32 status)
```

Quick program termination

## fn getenv

```
fn getenv(cstr name):cstr
```

Get environment variable value

## fn system

```
fn system(cstr command):i32
```

Execute system command

## fn abs

```
fn abs(i32 x):i32
```

Compute absolute value of integer

## fn labs

```
fn labs(i64 x):i64
```

Compute absolute value of long integer

## fn div

```
fn div(i32 numer, i32 denom):div_t
```

Divide integers and return quotient and remainder

## fn ldiv

```
fn ldiv(i64 numer, i64 denom):ldiv_t
```

Divide long integers and return quotient and remainder

## fn mblen

```
fn mblen(cstr s, u64 n):i32
```

Get length of multibyte character

## fn mbtowc

```
fn mbtowc(anyptr pwc, cstr s, u64 n):i32
```

Convert multibyte character to wide character

## fn wctomb

```
fn wctomb(cstr s, i32 wc):i32
```

Convert wide character to multibyte character

## fn mbstowcs

```
fn mbstowcs(anyptr pwcs, cstr s, u64 n):u64
```

Convert multibyte string to wide character string

## fn wcstombs

```
fn wcstombs(cstr s, anyptr pwcs, u64 n):u64
```

Convert wide character string to multibyte string

## fn posix_memalign

```
fn posix_memalign(anyptr memptr, u64 alignment, u64 size):i32
```

Allocate aligned memory (POSIX)

## fn setenv

```
fn setenv(cstr name, cstr value, i32 overwrite):i32
```

Set environment variable

## fn unsetenv

```
fn unsetenv(cstr name):i32
```

Unset environment variable

## fn putenv

```
fn putenv(cstr str):i32
```

Add or change environment variable

## fn mkstemp

```
fn mkstemp(cstr template):i32
```

Create unique temporary file

## fn mkostemp

```
fn mkostemp(cstr template, i32 flags):i32
```

Create unique temporary file with flags

## fn mkdtemp

```
fn mkdtemp(cstr template):cstr
```

Create unique temporary directory

## fn mktemp

```
fn mktemp(cstr template):cstr
```

Generate unique temporary filename

## fn mkstemps

```
fn mkstemps(cstr template, i32 suffixlen):i32
```

Create unique temporary file with suffix

## fn mkostemps

```
fn mkostemps(cstr template, i32 suffixlen, i32 flags):i32
```

Create unique temporary file with suffix and flags

## fn getsubopt

```
fn getsubopt(anyptr optionp, anyptr tokens, anyptr valuep):i32
```

Parse suboption arguments

## fn rand_r

```
fn rand_r(ptr<u32> seed):i32
```

Thread-safe random number generator

## fn realpath

```
fn realpath(cstr path, cstr resolved_path):cstr
```

Resolve pathname to absolute path

## fn random

```
fn random():i64
```

Generate random number

## fn srandom

```
fn srandom(u32 seed):void
```

Seed random number generator

## fn initstate

```
fn initstate(u32 seed, cstr state, u64 size):cstr
```

Initialize random number generator state

## fn setstate

```
fn setstate(cstr state):cstr
```

Set random number generator state

## fn posix_openpt

```
fn posix_openpt(i32 flags):i32
```

Open pseudo-terminal master

## fn grantpt

```
fn grantpt(i32 fd):i32
```

Grant access to pseudo-terminal slave

## fn unlockpt

```
fn unlockpt(i32 fd):i32
```

Unlock pseudo-terminal master/slave pair

## fn ptsname_r

```
fn ptsname_r(i32 fd, cstr buf, u64 buflen):i32
```

Get name of pseudo-terminal slave (thread-safe)

## fn l64a

```
fn l64a(i64 value):cstr
```

Convert long to radix-64 ASCII string

## fn a64l

```
fn a64l(cstr s):i64
```

Convert radix-64 ASCII string to long

## fn setkey

```
fn setkey(cstr key):void
```

Set encryption key

## fn drand48

```
fn drand48():f64
```

Generate uniformly distributed random number

## fn erand48

```
fn erand48(anyptr xsubi):f64
```

Generate uniformly distributed random number with seed

## fn lrand48

```
fn lrand48():i64
```

Generate non-negative random long integer

## fn nrand48

```
fn nrand48(anyptr xsubi):i64
```

Generate non-negative random long integer with seed

## fn mrand48

```
fn mrand48():i64
```

Generate signed random long integer

## fn jrand48

```
fn jrand48(anyptr xsubi):i64
```

Generate signed random long integer with seed

## fn srand48

```
fn srand48(i64 seedval):void
```

Seed 48-bit random number generator

## fn seed48

```
fn seed48(anyptr seed16v):ptr<u16>
```

Set 48-bit random number generator seed

## fn lcong48

```
fn lcong48(anyptr param):void
```

Set 48-bit random number generator parameters

## fn valloc

```
fn valloc(u64 size):anyptr
```

Allocate page-aligned memory

## fn memalign

```
fn memalign(u64 alignment, u64 size):anyptr
```

Allocate aligned memory

## fn reallocarray

```
fn reallocarray(anyptr p, u64 nmemb, u64 size):anyptr
```

Reallocate array with overflow checking

## fn getloadavg

```
fn getloadavg(ptr<f64> loadavg, i32 nelem):i32
```

Get system load averages

## fn ecvt

```
fn ecvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr
```

Convert floating-point to string (exponential format)

## fn fcvt

```
fn fcvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr
```

Convert floating-point to string (fixed-point format)

## fn gcvt

```
fn gcvt(f64 number, i32 ndigit, cstr buf):cstr
```

Convert floating-point to string (general format)

## fn secure_getenv

```
fn secure_getenv(cstr name):cstr
```

Get environment variable securely

## fn fopen

```
fn fopen(cstr filename, cstr mode):fileptr
```

Open file stream

## fn freopen

```
fn freopen(cstr filename, cstr mode, fileptr stream):fileptr
```

Reopen file stream

## fn fclose

```
fn fclose(fileptr stream):i32
```

Close file stream

## fn remove

```
fn remove(cstr filename):i32
```

Remove file or directory

## fn rename

```
fn rename(cstr old_name, cstr new_name):i32
```

Rename file or directory

## fn feof

```
fn feof(fileptr stream):i32
```

Test end-of-file indicator

## fn ferror

```
fn ferror(fileptr stream):i32
```

Test error indicator

## fn fflush

```
fn fflush(fileptr stream):i32
```

Flush stream buffer

## fn clearerr

```
fn clearerr(fileptr stream):void
```

Clear error and end-of-file indicators

## fn fseek

```
fn fseek(fileptr stream, i64 offset, i32 whence):i32
```

Set file position

## fn ftell

```
fn ftell(fileptr stream):i64
```

Get file position

## fn rewind

```
fn rewind(fileptr stream):void
```

Reset file position to beginning

## fn fgetpos

```
fn fgetpos(fileptr stream, ptr<fpos_t> pos):i32
```

Get file position

## fn fsetpos

```
fn fsetpos(fileptr stream, ptr<fpos_t> pos):i32
```

Set file position

## fn fread

```
fn fread(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

Read data from stream

## fn fwrite

```
fn fwrite(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

Write data to stream

## fn fgetc

```
fn fgetc(fileptr stream):i32
```

Get character from stream

## fn getc

```
fn getc(fileptr stream):i32
```

Get character from stream (macro version)

## fn getchar

```
fn getchar():i32
```

Get character from stdin

## fn ungetc

```
fn ungetc(i32 c, fileptr stream):i32
```

Push character back to stream

## fn fputc

```
fn fputc(i32 c, fileptr stream):i32
```

Put character to stream

## fn putc

```
fn putc(i32 c, fileptr stream):i32
```

Put character to stream (macro version)

## fn putchar

```
fn putchar(i32 c):i32
```

Put character to stdout

## fn fgets

```
fn fgets(cstr s, i32 size, fileptr stream):cstr
```

Get string from stream

## fn fputs

```
fn fputs(cstr s, fileptr stream):i32
```

Put string to stream

## fn puts

```
fn puts(cstr s):i32
```

Put string to stdout with newline

## fn perror

```
fn perror(cstr s):void
```

Print error message

## fn setvbuf

```
fn setvbuf(fileptr stream, cstr buffer, i32 mode, u64 size):i32
```

Set stream buffering

## fn setbuf

```
fn setbuf(fileptr stream, cstr buffer):void
```

Set stream buffer

## fn tmpnam

```
fn tmpnam(cstr s):cstr
```

Generate temporary filename

## fn tmpfile

```
fn tmpfile():fileptr
```

Create temporary file

## fn fmemopen

```
fn fmemopen(anyptr buffer, u64 size, cstr mode):fileptr
```

Open memory as stream

## fn open_memstream

```
fn open_memstream(ptr<cstr> bufp, ptr<u64> sizep):fileptr
```

Open dynamic memory stream

## fn fdopen

```
fn fdopen(i32 fd, cstr mode):fileptr
```

Open stream from file descriptor

## fn popen

```
fn popen(cstr command, cstr t):fileptr
```

Open pipe to process

## fn pclose

```
fn pclose(fileptr stream):i32
```

Close pipe stream

## fn fileno

```
fn fileno(fileptr stream):i32
```

Get file descriptor from stream

## fn fseeko

```
fn fseeko(fileptr stream, i64 offset, i32 whence):i32
```

Set file position (large file support)

## fn ftello

```
fn ftello(fileptr stream):i64
```

Get file position (large file support)

## fn flockfile

```
fn flockfile(fileptr stream):void
```

Lock file stream

## fn ftrylockfile

```
fn ftrylockfile(fileptr stream):i32
```

Try to lock file stream

## fn funlockfile

```
fn funlockfile(fileptr stream):void
```

Unlock file stream

## fn getc_unlocked

```
fn getc_unlocked(fileptr stream):i32
```

Get character without locking

## fn getchar_unlocked

```
fn getchar_unlocked():i32
```

Get character from stdin without locking

## fn putc_unlocked

```
fn putc_unlocked(i32 c, fileptr stream):i32
```

Put character without locking

## fn putchar_unlocked

```
fn putchar_unlocked(i32 c):i32
```

Put character to stdout without locking

## fn fgetc_unlocked

```
fn fgetc_unlocked(fileptr stream):i32
```

Get character from stream without locking

## fn fputc_unlocked

```
fn fputc_unlocked(i32 c, fileptr stream):i32
```

Put character to stream without locking

## fn fflush_unlocked

```
fn fflush_unlocked(fileptr stream):i32
```

Flush stream without locking

## fn fread_unlocked

```
fn fread_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

Read data without locking

## fn fwrite_unlocked

```
fn fwrite_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64
```

Write data without locking

## fn clearerr_unlocked

```
fn clearerr_unlocked(fileptr stream):void
```

Clear error indicators without locking

## fn feof_unlocked

```
fn feof_unlocked(fileptr stream):i32
```

Test end-of-file without locking

## fn ferror_unlocked

```
fn ferror_unlocked(fileptr stream):i32
```

Test error indicator without locking

## fn fileno_unlocked

```
fn fileno_unlocked(fileptr stream):i32
```

Get file descriptor without locking

## fn fgets_unlocked

```
fn fgets_unlocked(cstr s, i32 size, fileptr stream):cstr
```

Get string without locking

## fn fputs_unlocked

```
fn fputs_unlocked(cstr s, fileptr stream):i32
```

Put string without locking

## fn getdelim

```
fn getdelim(ptr<cstr> lineptr, ptr<u64> n, i32 delim, fileptr stream):i64
```

Read delimited string from stream

## fn getline

```
fn getline(ptr<cstr> lineptr, ptr<u64> n, fileptr stream):i64
```

Read line from stream

## fn renameat

```
fn renameat(i32 olddirfd, cstr oldpath, i32 newdirfd, cstr newpath):i32
```

Rename file relative to directory descriptors

## fn tempnam

```
fn tempnam(cstr dir, cstr pfx):cstr
```

Generate temporary filename with directory and prefix

## fn cuserid

```
fn cuserid(cstr s):cstr
```

Get user login name

## fn setlinebuf

```
fn setlinebuf(fileptr stream):void
```

Set line buffering

## fn setbuffer

```
fn setbuffer(fileptr stream, cstr buffer, u64 size):void
```

Set stream buffer with size

## fn getw

```
fn getw(fileptr stream):i32
```

Get word from stream

## fn putw

```
fn putw(i32 w, fileptr stream):i32
```

Put word to stream

## fn fgetln

```
fn fgetln(fileptr stream, ptr<u64> len):cstr
```

Get line from stream

## fn fopencookie

```
fn fopencookie(anyptr cookie, cstr mode, cookie_io_functions_t io_funcs):fileptr
```

Open custom stream with cookie

## fn memcpy

```
fn memcpy(anyptr dst, anyptr src, u64 n):anyptr
```

Copy memory block

## fn memmove

```
fn memmove(anyptr dst, anyptr src, u64 n):anyptr
```

Move memory block (handles overlap)

## fn memset

```
fn memset(anyptr s, i32 c, u64 n):anyptr
```

Fill memory with constant byte

## fn memcmp

```
fn memcmp(anyptr s1, anyptr s2, u64 n):i32
```

Compare memory blocks

## fn memchr

```
fn memchr(anyptr s, i32 c, u64 n):anyptr
```

Find byte in memory block

## fn strcpy

```
fn strcpy(cstr dst, cstr src):cstr
```

Copy string

## fn strncpy

```
fn strncpy(cstr dst, cstr src, u64 n):cstr
```

Copy string with length limit

## fn strcat

```
fn strcat(cstr dst, cstr src):cstr
```

Concatenate strings

## fn strncat

```
fn strncat(cstr dst, cstr src, u64 n):cstr
```

Concatenate strings with length limit

## fn strcmp

```
fn strcmp(cstr s1, cstr s2):i32
```

Compare strings

## fn strncmp

```
fn strncmp(cstr s1, cstr s2, u64 n):i32
```

Compare strings with length limit

## fn strcoll

```
fn strcoll(cstr s1, cstr s2):i32
```

Compare strings using locale

## fn strxfrm

```
fn strxfrm(cstr dst, cstr src, u64 n):u64
```

Transform string for locale comparison

## fn strchr

```
fn strchr(cstr s, i32 c):cstr
```

Find character in string

## fn strrchr

```
fn strrchr(cstr s, i32 c):cstr
```

Find last occurrence of character in string

## fn strcspn

```
fn strcspn(cstr s1, cstr s2):u64
```

Get length of complementary substring

## fn strspn

```
fn strspn(cstr s1, cstr s2):u64
```

Get length of substring

## fn strpbrk

```
fn strpbrk(cstr s1, cstr s2):cstr
```

Find first occurrence of characters

## fn strstr

```
fn strstr(cstr haystack, cstr needle):cstr
```

Find substring

## fn strtok

```
fn strtok(cstr str, cstr delim):cstr
```

Split string into tokens

## fn strlen

```
fn strlen(cstr s):u64
```

Get string length

## fn error_string

```
fn error_string():string
```

Get error string

## fn strerror

```
fn strerror(i32 errnum):cstr
```

Get error message string

## fn strtok_r

```
fn strtok_r(cstr str, cstr delim, ptr<cstr> saveptr):cstr
```

Split string into tokens (thread-safe)

## fn strerror_r

```
fn strerror_r(i32 errnum, cstr buf, u64 buflen):i32
```

Get error message string (thread-safe)

## fn stpcpy

```
fn stpcpy(cstr dst, cstr src):cstr
```

Copy string and return pointer to end

## fn stpncpy

```
fn stpncpy(cstr dst, cstr src, u64 n):cstr
```

Copy string with length limit and return pointer to end

## fn strnlen

```
fn strnlen(cstr s, u64 maxlen):u64
```

Get string length with maximum

## fn strdup

```
fn strdup(cstr s):cstr
```

Duplicate string

## fn strndup

```
fn strndup(cstr s, u64 n):cstr
```

Duplicate string with length limit

## fn strsignal

```
fn strsignal(i32 sig):cstr
```

Get signal description string

## fn strerror_l

```
fn strerror_l(i32 errnum, locale_t locale):cstr
```

Get error message string with locale

## fn strcoll_l

```
fn strcoll_l(cstr s1, cstr s2, locale_t locale):cstr
```

Compare strings using specified locale

## fn strxfrm_l

```
fn strxfrm_l(cstr dst, cstr src, u64 n, locale_t locale):u64
```

Transform string for locale comparison with specified locale

## fn memmem

```
fn memmem(anyptr haystack, u64 haystacklen, anyptr needle, u64 needlelen):anyptr
```

Find memory block in memory

## fn memccpy

```
fn memccpy(anyptr dst, anyptr src, i32 c, u64 n):anyptr
```

Copy memory until character found

## fn strsep

```
fn strsep(ptr<cstr> strp, cstr delim):cstr
```

Separate string

## fn strlcat

```
fn strlcat(cstr dst, cstr src, u64 size):u64
```

Concatenate strings safely

## fn strlcpy

```
fn strlcpy(cstr dst, cstr src, u64 size):u64
```

Copy string safely

## fn explicit_bzero

```
fn explicit_bzero(anyptr s, u64 n):void
```

Zero memory securely

## fn strverscmp

```
fn strverscmp(cstr s1, cstr s2):i32
```

Compare strings with version number semantics

## fn strchrnul

```
fn strchrnul(cstr s, i32 c):cstr
```

Find character in string or end

## fn strcasestr

```
fn strcasestr(cstr haystack, cstr needle):cstr
```

Find substring case-insensitively

## fn memrchr

```
fn memrchr(anyptr s, i32 c, u64 n):anyptr
```

Find last occurrence of byte in memory

## fn mempcpy

```
fn mempcpy(anyptr dst, anyptr src, u64 n):anyptr
```

Copy memory and return pointer to end

## fn acos

```
fn acos(f64 x):f64
```

Compute arc cosine

## fn acosf

```
fn acosf(f32 x):f32
```

Compute arc cosine (single precision)

## fn acosh

```
fn acosh(f64 x):f64
```

Compute inverse hyperbolic cosine

## fn acoshf

```
fn acoshf(f32 x):f32
```

Compute inverse hyperbolic cosine (single precision)

## fn asin

```
fn asin(f64 x):f64
```

Compute arc sine

## fn asinf

```
fn asinf(f32 x):f32
```

Compute arc sine (single precision)

## fn asinh

```
fn asinh(f64 x):f64
```

Compute inverse hyperbolic sine

## fn asinhf

```
fn asinhf(f32 x):f32
```

Compute inverse hyperbolic sine (single precision)

## fn atan

```
fn atan(f64 x):f64
```

Compute arc tangent

## fn atanf

```
fn atanf(f32 x):f32
```

Compute arc tangent (single precision)

## fn atan2

```
fn atan2(f64 y, f64 x):f64
```

Compute arc tangent of y/x

## fn atan2f

```
fn atan2f(f32 y, f32 x):f32
```

Compute arc tangent of y/x (single precision)

## fn cos

```
fn cos(f64 x):f64
```

Compute cosine

## fn cosf

```
fn cosf(f32 x):f32
```

Compute cosine (single precision)

## fn sin

```
fn sin(f64 x):f64
```

Compute sine

## fn sinf

```
fn sinf(f32 x):f32
```

Compute sine (single precision)

## fn tan

```
fn tan(f64 x):f64
```

Compute tangent

## fn tanf

```
fn tanf(f32 x):f32
```

Compute tangent (single precision)

## fn atanh

```
fn atanh(f64 x):f64
```

Compute inverse hyperbolic tangent

## fn atanhf

```
fn atanhf(f32 x):f32
```

Compute inverse hyperbolic tangent (single precision)

## fn cosh

```
fn cosh(f64 x):f64
```

Compute hyperbolic cosine

## fn coshf

```
fn coshf(f32 x):f32
```

Compute hyperbolic cosine (single precision)

## fn sinh

```
fn sinh(f64 x):f64
```

Compute hyperbolic sine

## fn sinhf

```
fn sinhf(f32 x):f32
```

Compute hyperbolic sine (single precision)

## fn sqrt

```
fn sqrt(f64 x):f64
```

Compute square root

## fn sqrtf

```
fn sqrtf(f32 x):f32
```

Compute square root (single precision)

## fn tanh

```
fn tanh(f64 x):f64
```

Compute hyperbolic tangent

## fn tanhf

```
fn tanhf(f32 x):f32
```

Compute hyperbolic tangent (single precision)

## fn exp

```
fn exp(f64 x):f64
```

Compute exponential function

## fn expf

```
fn expf(f32 x):f32
```

Compute exponential function (single precision)

## fn exp2

```
fn exp2(f64 x):f64
```

Compute base-2 exponential function

## fn exp2f

```
fn exp2f(f32 x):f32
```

Compute base-2 exponential function (single precision)

## fn expm1

```
fn expm1(f64 x):f64
```

Compute exp(x) - 1

## fn expm1f

```
fn expm1f(f32 x):f32
```

Compute exp(x) - 1 (single precision)

## fn fabs

```
fn fabs(f64 x):f64
```

Compute absolute value of floating-point number

## fn fabsf

```
fn fabsf(f32 x):f32
```

Compute absolute value of floating-point number (single precision)

## fn log

```
fn log(f64 x):f64
```

Compute natural logarithm

## fn logf

```
fn logf(f32 x):f32
```

Compute natural logarithm (single precision)

## fn log10

```
fn log10(f64 x):f64
```

Compute base-10 logarithm

## fn log10f

```
fn log10f(f32 x):f32
```

Compute base-10 logarithm (single precision)

## fn log1p

```
fn log1p(f64 x):f64
```

Compute log(1 + x)

## fn log1pf

```
fn log1pf(f32 x):f32
```

Compute log(1 + x) (single precision)

## fn log2

```
fn log2(f64 x):f64
```

Compute base-2 logarithm

## fn log2f

```
fn log2f(f32 x):f32
```

Compute base-2 logarithm (single precision)

## fn logb

```
fn logb(f64 x):f64
```

Extract exponent

## fn logbf

```
fn logbf(f32 x):f32
```

Extract exponent (single precision)

## fn pow

```
fn pow(f64 x, f64 y):f64
```

Compute x raised to the power of y

## fn powf

```
fn powf(f32 x, f32 y):f32
```

Compute x raised to the power of y (single precision)

## fn cbrt

```
fn cbrt(f64 x):f64
```

Compute cube root

## fn cbrtf

```
fn cbrtf(f32 x):f32
```

Compute cube root (single precision)

## fn hypot

```
fn hypot(f64 x, f64 y):f64
```

Compute hypotenuse

## fn hypotf

```
fn hypotf(f32 x, f32 y):f32
```

Compute hypotenuse (single precision)

## fn ceil

```
fn ceil(f64 x):f64
```

Compute ceiling function

## fn ceilf

```
fn ceilf(f32 x):f32
```

Compute ceiling function (single precision)

## fn floor

```
fn floor(f64 x):f64
```

Compute floor function

## fn floorf

```
fn floorf(f32 x):f32
```

Compute floor function (single precision)

## fn trunc

```
fn trunc(f64 x):f64
```

Truncate to integer

## fn truncf

```
fn truncf(f32 x):f32
```

Truncate to integer (single precision)

## fn rint

```
fn rint(f64 x):f64
```

Round to nearest integer

## fn rintf

```
fn rintf(f32 x):f32
```

Round to nearest integer (single precision)

## fn nearbyint

```
fn nearbyint(f64 x):f64
```

Round to nearest integer (no exception)

## fn nearbyintf

```
fn nearbyintf(f32 x):f32
```

Round to nearest integer (no exception, single precision)

## fn lrint

```
fn lrint(f64 x):i64
```

Round to nearest integer and convert to long

## fn lrintf

```
fn lrintf(f32 x):i64
```

Round to nearest integer and convert to long (single precision)

## fn llrint

```
fn llrint(f64 x):i64
```

Round to nearest integer and convert to long long

## fn llrintf

```
fn llrintf(f32 x):i64
```

Round to nearest integer and convert to long long (single precision)

## fn lround

```
fn lround(f64 x):i64
```

Round to nearest integer and convert to long

## fn lroundf

```
fn lroundf(f32 x):i64
```

Round to nearest integer and convert to long (single precision)

## fn llround

```
fn llround(f64 x):i64
```

Round to nearest integer and convert to long long

## fn llroundf

```
fn llroundf(f32 x):i64
```

Round to nearest integer and convert to long long (single precision)

## fn copysign

```
fn copysign(f64 x, f64 y):f64
```

Copy sign of floating-point value

## fn copysignf

```
fn copysignf(f32 x, f32 y):f32
```

Copy sign of floating-point value (single precision)

## fn frexp

```
fn frexp(f64 x, ptr<i32> exp):f64
```

Break floating-point number into fraction and exponent

## fn frexpf

```
fn frexpf(f32 x, ptr<i32> exp):f32
```

Break floating-point number into fraction and exponent (single precision)

## fn ldexp

```
fn ldexp(f64 x, i32 exp):f64
```

Generate floating-point number from fraction and exponent

## fn ldexpf

```
fn ldexpf(f32 x, i32 exp):f32
```

Generate floating-point number from fraction and exponent (single precision)

## fn modf

```
fn modf(f64 x, ptr<f64> iptr):f64
```

Break into integer and fractional parts

## fn modff

```
fn modff(f32 x, ptr<f32> iptr):f32
```

Break into integer and fractional parts (single precision)

## fn scalbn

```
fn scalbn(f64 x, i32 n):f64
```

Scale by power of radix

## fn scalbnf

```
fn scalbnf(f32 x, i32 n):f32
```

Scale by power of radix (single precision)

## fn scalbln

```
fn scalbln(f64 x, i64 n):f64
```

Scale by power of radix (long exponent)

## fn scalblnf

```
fn scalblnf(f32 x, i64 n):f32
```

Scale by power of radix (long exponent, single precision)

## fn round

```
fn round(f64 x):f64
```

Round to nearest integer

## fn roundf

```
fn roundf(f32 x):f32
```

Round to nearest integer (single precision)

## fn ilogb

```
fn ilogb(f64 x):i32
```

Extract exponent as integer

## fn ilogbf

```
fn ilogbf(f32 x):i32
```

Extract exponent as integer (single precision)

## fn fmod

```
fn fmod(f64 x, f64 y):f64
```

Compute floating-point remainder

## fn fmodf

```
fn fmodf(f32 x, f32 y):f32
```

Compute floating-point remainder (single precision)

## fn remainder

```
fn remainder(f64 x, f64 y):f64
```

Compute IEEE remainder

## fn remainderf

```
fn remainderf(f32 x, f32 y):f32
```

Compute IEEE remainder (single precision)

## fn remquo

```
fn remquo(f64 x, f64 y, ptr<i32> quo):f64
```

Compute remainder and quotient

## fn remquof

```
fn remquof(f32 x, f32 y, ptr<i32> quo):f32
```

Compute remainder and quotient (single precision)

## fn fmax

```
fn fmax(f64 x, f64 y):f64
```

Determine maximum of two floating-point values

## fn fmaxf

```
fn fmaxf(f32 x, f32 y):f32
```

Determine maximum of two floating-point values (single precision)

## fn fmin

```
fn fmin(f64 x, f64 y):f64
```

Determine minimum of two floating-point values

## fn fminf

```
fn fminf(f32 x, f32 y):f32
```

Determine minimum of two floating-point values (single precision)

## fn fdim

```
fn fdim(f64 x, f64 y):f64
```

Compute positive difference

## fn fdimf

```
fn fdimf(f32 x, f32 y):f32
```

Compute positive difference (single precision)

## fn fma

```
fn fma(f64 x, f64 y, f64 z):f64
```

Compute fused multiply-add

## fn fmaf

```
fn fmaf(f32 x, f32 y, f32 z):f32
```

Compute fused multiply-add (single precision)

## fn std_args

```
fn std_args():[string]
```

Get command line arguments

## fn htons

```
fn htons(u16 host):u16
```

Convert host byte order to network byte order (16-bit)

## fn waitpid

```
fn waitpid(int pid, ptr<int> status, int options):int
```

Wait for process to change state

## fn sigemptyset

```
fn sigemptyset(ref<sigset_t> sigset):i32
```

Initialize signal set to empty

## fn sigaddset

```
fn sigaddset(ref<sigset_t> sigset, i32 signo):i32
```

Add signal to signal set

## fn sigfillset

```
fn sigfillset(ref<sigset_t> sigset):i32
```

Initialize signal set to full

## fn sigprocmask

```
fn sigprocmask(i32 how, ref<sigset_t> sigset, ptr<sigset_t> oldset):i32
```

Examine and change blocked signals

## fn signalfd

```
fn signalfd(int fd, ref<sigset_t> mask, i32 flags):i32
```

Create file descriptor for accepting signals

## fn prctl

```
fn prctl(int option, u64 arg2, u64 arg3, u64 arg4, u64 arg5):int
```

Operations on a process

## fn uv_hrtime

```
fn uv_hrtime():u64
```

Get high-resolution time

## fn errno

```
fn errno():int
```

Get current errno value

## fn get_envs

```
fn get_envs():[string]
```

Get all environment variables

## fn fork

```
fn fork():int
```

Create child process

## fn getcwd

```
fn getcwd(cstr path, uint size):cstr
```

Get current working directory

## fn mmap

```
fn mmap(anyptr addr, int len, int prot, int flags, int fd, int off):anyptr
```

Map files or devices into memory

## fn munmap

```
fn munmap(anyptr addr, int len)
```

Unmap files or devices from memory

## fn isprint

```
fn isprint(u8 c):bool
```

Test for printable character

## fn isspace

```
fn isspace(u8 c):bool
```

Test for whitespace character

# import [libc.dirent](https://github.com/nature-lang/nature/tree/master/std/libc/dirent.linux.n)

Get information about the specified directory and traverse it recursively.

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

Open directory stream

## fn readdir

```
fn readdir(dir_t d):ptr<dirent_t>
```

Read directory entry from directory stream

## fn closedir

```
fn closedir(dir_t d):int
```

Close directory stream

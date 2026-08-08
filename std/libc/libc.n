mod libc

import libc.cross

pub type cstr = anyptr

#linkid rt_string_ref
pub fn string.to_cstr(*self):cstr

pub fn to_cfn(anyptr closure):anyptr {
    type fn_t = struct{
        anyptr envs
        anyptr addr
    }

    var c = closure as ptr<fn_t>
    return c.addr
}

pub const AF_INET = 2
pub const AF_INET6 = 10

#linkid rt_string_new
pub fn cstr.to_string(self):string

// stdlib.h
#linkid atoi
pub fn atoi(cstr str):i32

pub fn atol(cstr str):i64 {
    return cross.atol(str as anyptr)
}

#linkid atof
pub fn atof(cstr str):f64

#linkid strtof
pub fn strtof(cstr str, anyptr endptr):f32

#linkid strtod
pub fn strtod(cstr str, anyptr endptr):f64

pub fn strtol(cstr str, anyptr endptr, i32 base):i64 {
    return cross.strtol(str as anyptr, endptr, base)
}

pub fn strtoul(cstr str, anyptr endptr, i32 base):u64 {
    return cross.strtoul(str as anyptr, endptr, base)
}

#linkid rand
pub fn rand():i32

#linkid srand
pub fn srand(u32 seed):void

#linkid malloc
pub fn malloc(u64 size):anyptr

#linkid calloc
pub fn calloc(u64 nmemb, u64 size):anyptr

#linkid realloc
pub fn realloc(anyptr p, u64 size):anyptr

#linkid free
pub fn free(anyptr p):void

#linkid aligned_alloc
pub fn aligned_alloc(u64 alignment, u64 size):anyptr

#linkid abort
pub fn abort()

#linkid atexit
pub fn atexit(anyptr func):i32

#linkid exit
pub fn exit(i32 status)

#linkid _Exit
pub fn _exit(i32 status)

#linkid at_quick_exit
pub fn at_quick_exit(anyptr func):i32

#linkid quick_exit
pub fn quick_exit(i32 status)

#linkid getenv
pub fn getenv(cstr name):cstr

#linkid system
pub fn system(cstr command):i32

#linkid abs
pub fn abs(i32 x):i32

pub fn labs(i64 x):i64 {
    if x < 0 {
        return -x
    }
    return x
}

pub type div_t = struct {
    i32 quot
    i32 rem
}

pub type ldiv_t = struct {
    i64 quot
    i64 rem
}

#linkid div
pub fn div(i32 numer, i32 denom):div_t

pub fn ldiv(i64 numer, i64 denom):ldiv_t {
    return ldiv_t {
        quot: numer / denom,
        rem: numer % denom,
    }
}

#linkid mblen
pub fn mblen(cstr s, u64 n):i32

#linkid mbtowc
pub fn mbtowc(anyptr pwc, cstr s, u64 n):i32

#linkid wctomb
pub fn wctomb(cstr s, i32 wc):i32

#linkid mbstowcs
pub fn mbstowcs(anyptr pwcs, cstr s, u64 n):u64

#linkid wcstombs
pub fn wcstombs(cstr s, anyptr pwcs, u64 n):u64

#linkid posix_memalign
pub fn posix_memalign(anyptr memptr, u64 alignment, u64 size):i32

#linkid setenv
pub fn setenv(cstr name, cstr value, i32 overwrite):i32

#linkid unsetenv
pub fn unsetenv(cstr name):i32

#linkid putenv
pub fn putenv(cstr str):i32

#linkid mkstemp
pub fn mkstemp(cstr template):i32

#linkid mkostemp
pub fn mkostemp(cstr template, i32 flags):i32

#linkid mkdtemp
pub fn mkdtemp(cstr template):cstr

#linkid mktemp
pub fn mktemp(cstr template):cstr

#linkid mkstemps
pub fn mkstemps(cstr template, i32 suffixlen):i32

#linkid mkostemps
pub fn mkostemps(cstr template, i32 suffixlen, i32 flags):i32

#linkid getsubopt
pub fn getsubopt(anyptr optionp, anyptr tokens, anyptr valuep):i32

#linkid rand_r
pub fn rand_r(ptr<u32> seed):i32

#linkid realpath
pub fn realpath(cstr path, cstr resolved_path):cstr

#linkid random
pub fn random():i64

#linkid srandom
pub fn srandom(u32 seed):void

#linkid initstate
pub fn initstate(u32 seed, cstr state, u64 size):cstr

#linkid setstate
pub fn setstate(cstr state):cstr

#linkid posix_openpt
pub fn posix_openpt(i32 flags):i32

#linkid grantpt
pub fn grantpt(i32 fd):i32

#linkid unlockpt
pub fn unlockpt(i32 fd):i32

#linkid ptsname_r
pub fn ptsname_r(i32 fd, cstr buf, u64 buflen):i32

#linkid l64a
pub fn l64a(i64 value):cstr

#linkid a64l
pub fn a64l(cstr s):i64

#linkid setkey
pub fn setkey(cstr key):void

#linkid drand48
pub fn drand48():f64

#linkid erand48
pub fn erand48(anyptr xsubi):f64

#linkid lrand48
pub fn lrand48():i64

#linkid nrand48
pub fn nrand48(anyptr xsubi):i64

#linkid mrand48
pub fn mrand48():i64

#linkid jrand48
pub fn jrand48(anyptr xsubi):i64

#linkid srand48
pub fn srand48(i64 seedval):void

#linkid seed48
pub fn seed48(anyptr seed16v):ptr<u16>

#linkid lcong48
pub fn lcong48(anyptr param):void

#linkid valloc
pub fn valloc(u64 size):anyptr

#linkid memalign
pub fn memalign(u64 alignment, u64 size):anyptr

#linkid reallocarray
pub fn reallocarray(anyptr p, u64 nmemb, u64 size):anyptr

#linkid getloadavg
pub fn getloadavg(ptr<f64> loadavg, i32 nelem):i32

#linkid ecvt
pub fn ecvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr

#linkid fcvt
pub fn fcvt(f64 number, i32 ndigits, ptr<i32> decpt, ptr<i32> sign):cstr

#linkid gcvt
pub fn gcvt(f64 number, i32 ndigit, cstr buf):cstr

#linkid secure_getenv
pub fn secure_getenv(cstr name):cstr


// stdio.h
pub type fileptr = anyptr

// stdio.h constants
pub const EOF = -1
pub const SEEK_SET = 0
const SEEK_CUR = 1
pub const SEEK_END = 2
pub const _IOFBF = 0
pub const _IOLBF = 1
pub const _IONBF = 2
pub const BUFSIZ = 1024
pub const FILENAME_MAX = 4096
pub const FOPEN_MAX = 1000
pub const TMP_MAX = 10000
pub const L_tmpnam = 20
pub const L_ctermid = 20
pub const L_cuserid = 20

// fpos_t type
pub type fpos_t = struct {
    [u8;16] __opaque
    i64 __lldata
    f64 __align
}

#linkid fopen
pub fn fopen(cstr filename, cstr mode):fileptr

#linkid freopen
pub fn freopen(cstr filename, cstr mode, fileptr stream):fileptr

#linkid fclose
pub fn fclose(fileptr stream):i32

#linkid remove
pub fn remove(cstr filename):i32

#linkid rename
pub fn rename(cstr old_name, cstr new_name):i32

#linkid feof
pub fn feof(fileptr stream):i32

#linkid ferror
pub fn ferror(fileptr stream):i32

#linkid fflush
pub fn fflush(fileptr stream):i32

#linkid clearerr
pub fn clearerr(fileptr stream):void

#linkid fseek
pub fn fseek(fileptr stream, i64 offset, i32 whence):i32

#linkid ftell
pub fn ftell(fileptr stream):i64

#linkid rewind
pub fn rewind(fileptr stream):void

#linkid fgetpos
pub fn fgetpos(fileptr stream, ptr<fpos_t> pos):i32

#linkid fsetpos
pub fn fsetpos(fileptr stream, ptr<fpos_t> pos):i32

#linkid fread
pub fn fread(anyptr p, u64 size, u64 nmemb, fileptr stream):u64

#linkid fwrite
pub fn fwrite(anyptr p, u64 size, u64 nmemb, fileptr stream):u64

#linkid fgetc
pub fn fgetc(fileptr stream):i32

#linkid getc
pub fn getc(fileptr stream):i32

#linkid getchar
pub fn getchar():i32

#linkid ungetc
pub fn ungetc(i32 c, fileptr stream):i32

#linkid fputc
pub fn fputc(i32 c, fileptr stream):i32

#linkid putc
pub fn putc(i32 c, fileptr stream):i32

#linkid putchar
pub fn putchar(i32 c):i32

#linkid fgets
pub fn fgets(cstr s, i32 size, fileptr stream):cstr

#linkid fputs
pub fn fputs(cstr s, fileptr stream):i32

#linkid puts
pub fn puts(cstr s):i32

// Error handling
#linkid perror
pub fn perror(cstr s):void

// Buffer control
#linkid setvbuf
pub fn setvbuf(fileptr stream, cstr buffer, i32 mode, u64 size):i32

#linkid setbuf
pub fn setbuf(fileptr stream, cstr buffer):void

// Temporary files
#linkid tmpnam
pub fn tmpnam(cstr s):cstr

#linkid tmpfile
pub fn tmpfile():fileptr


// POSIX extensions
#linkid fmemopen
pub fn fmemopen(anyptr buffer, u64 size, cstr mode):fileptr

#linkid open_memstream
pub fn open_memstream(ptr<cstr> bufp, ptr<u64> sizep):fileptr

#linkid fdopen
pub fn fdopen(i32 fd, cstr mode):fileptr

#linkid popen
pub fn popen(cstr command, cstr t):fileptr

#linkid pclose
pub fn pclose(fileptr stream):i32

#linkid fileno
pub fn fileno(fileptr stream):i32

#linkid fseeko
pub fn fseeko(fileptr stream, i64 offset, i32 whence):i32

#linkid ftello
pub fn ftello(fileptr stream):i64

// File locking
#linkid flockfile
pub fn flockfile(fileptr stream):void

#linkid ftrylockfile
pub fn ftrylockfile(fileptr stream):i32

#linkid funlockfile
pub fn funlockfile(fileptr stream):void

// Unlocked I/O
#linkid getc_unlocked
pub fn getc_unlocked(fileptr stream):i32

#linkid getchar_unlocked
pub fn getchar_unlocked():i32

#linkid putc_unlocked
pub fn putc_unlocked(i32 c, fileptr stream):i32

#linkid putchar_unlocked
pub fn putchar_unlocked(i32 c):i32

#linkid fgetc_unlocked
pub fn fgetc_unlocked(fileptr stream):i32

#linkid fputc_unlocked
pub fn fputc_unlocked(i32 c, fileptr stream):i32

#linkid fflush_unlocked
pub fn fflush_unlocked(fileptr stream):i32

#linkid fread_unlocked
pub fn fread_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64

#linkid fwrite_unlocked
pub fn fwrite_unlocked(anyptr p, u64 size, u64 nmemb, fileptr stream):u64

#linkid clearerr_unlocked
pub fn clearerr_unlocked(fileptr stream):void

#linkid feof_unlocked
pub fn feof_unlocked(fileptr stream):i32

#linkid ferror_unlocked
pub fn ferror_unlocked(fileptr stream):i32

#linkid fileno_unlocked
pub fn fileno_unlocked(fileptr stream):i32

#linkid fgets_unlocked
pub fn fgets_unlocked(cstr s, i32 size, fileptr stream):cstr

#linkid fputs_unlocked
pub fn fputs_unlocked(cstr s, fileptr stream):i32

// Line-oriented I/O
#linkid getdelim
pub fn getdelim(ptr<cstr> lineptr, ptr<u64> n, i32 delim, fileptr stream):i64

#linkid getline
pub fn getline(ptr<cstr> lineptr, ptr<u64> n, fileptr stream):i64

// Additional functions
#linkid renameat
pub fn renameat(i32 olddirfd, cstr oldpath, i32 newdirfd, cstr newpath):i32

#linkid tempnam
pub fn tempnam(cstr dir, cstr pfx):cstr

#linkid cuserid
pub fn cuserid(cstr s):cstr

#linkid setlinebuf
pub fn setlinebuf(fileptr stream):void

#linkid setbuffer
pub fn setbuffer(fileptr stream, cstr buffer, u64 size):void

#linkid getw
pub fn getw(fileptr stream):i32

#linkid putw
pub fn putw(i32 w, fileptr stream):i32

#linkid fgetln
pub fn fgetln(fileptr stream, ptr<u64> len):cstr

// Cookie I/O function types
pub type cookie_read_function_t = fn(anyptr, cstr, u64):i64
pub type cookie_write_function_t = fn(anyptr, cstr, u64):i64
pub type cookie_seek_function_t = fn(anyptr, ptr<i64>, i32):i32
pub type cookie_close_function_t = fn(anyptr):i32

pub type cookie_io_functions_t = struct {
    cookie_read_function_t read
    cookie_write_function_t write
    cookie_seek_function_t seek
    cookie_close_function_t close
}

#linkid fopencookie
pub fn fopencookie(anyptr cookie, cstr mode, cookie_io_functions_t io_funcs):fileptr



// string.h
// Memory functions
#linkid memcpy
pub fn memcpy(anyptr dst, anyptr src, u64 n):anyptr

#linkid memmove
pub fn memmove(anyptr dst, anyptr src, u64 n):anyptr

#linkid memset
pub fn memset(anyptr s, i32 c, u64 n):anyptr

#linkid memcmp
pub fn memcmp(anyptr s1, anyptr s2, u64 n):i32

#linkid memchr
pub fn memchr(anyptr s, i32 c, u64 n):anyptr

// String copy functions
#linkid strcpy
pub fn strcpy(cstr dst, cstr src):cstr

#linkid strncpy
pub fn strncpy(cstr dst, cstr src, u64 n):cstr

// String concatenation functions
#linkid strcat
pub fn strcat(cstr dst, cstr src):cstr

#linkid strncat
pub fn strncat(cstr dst, cstr src, u64 n):cstr

// String comparison functions
#linkid strcmp
pub fn strcmp(cstr s1, cstr s2):i32

#linkid strncmp
pub fn strncmp(cstr s1, cstr s2, u64 n):i32

#linkid strcoll
pub fn strcoll(cstr s1, cstr s2):i32

#linkid strxfrm
pub fn strxfrm(cstr dst, cstr src, u64 n):u64

// String search functions
#linkid strchr
pub fn strchr(cstr s, i32 c):cstr

#linkid strrchr
pub fn strrchr(cstr s, i32 c):cstr

#linkid strcspn
pub fn strcspn(cstr s1, cstr s2):u64

#linkid strspn
pub fn strspn(cstr s1, cstr s2):u64

#linkid strpbrk
pub fn strpbrk(cstr s1, cstr s2):cstr

#linkid strstr
pub fn strstr(cstr haystack, cstr needle):cstr

#linkid strtok
pub fn strtok(cstr str, cstr delim):cstr

// String length function
#linkid strlen
pub fn strlen(cstr s):u64

#linkid rt_strerror
pub fn error_string():string

// Error functions
#linkid strerror
pub fn strerror(i32 errnum):cstr

// POSIX extensions
#linkid strtok_r
pub fn strtok_r(cstr str, cstr delim, ptr<cstr> saveptr):cstr

#linkid strerror_r
pub fn strerror_r(i32 errnum, cstr buf, u64 buflen):i32

#linkid stpcpy
pub fn stpcpy(cstr dst, cstr src):cstr

#linkid stpncpy
pub fn stpncpy(cstr dst, cstr src, u64 n):cstr

#linkid strnlen
pub fn strnlen(cstr s, u64 maxlen):u64

#linkid strdup
pub fn strdup(cstr s):cstr

#linkid strndup
pub fn strndup(cstr s, u64 n):cstr

#linkid strsignal
pub fn strsignal(i32 sig):cstr

// Locale-aware functions (requires locale_t type)
pub type locale_t = anyptr

#linkid strerror_l
pub fn strerror_l(i32 errnum, locale_t locale):cstr

#linkid strcoll_l
pub fn strcoll_l(cstr s1, cstr s2, locale_t locale):i32

#linkid strxfrm_l
pub fn strxfrm_l(cstr dst, cstr src, u64 n, locale_t locale):u64

#linkid memmem
pub fn memmem(anyptr haystack, u64 haystacklen, anyptr needle, u64 needlelen):anyptr

// X/Open extensions
#linkid memccpy
pub fn memccpy(anyptr dst, anyptr src, i32 c, u64 n):anyptr

// GNU/BSD extensions
#linkid strsep
pub fn strsep(ptr<cstr> strp, cstr delim):cstr

#linkid strlcat
pub fn strlcat(cstr dst, cstr src, u64 size):u64

#linkid strlcpy
pub fn strlcpy(cstr dst, cstr src, u64 size):u64

#linkid explicit_bzero
pub fn explicit_bzero(anyptr s, u64 n):void

// GNU extensions
#linkid strverscmp
pub fn strverscmp(cstr s1, cstr s2):i32

#linkid strchrnul
pub fn strchrnul(cstr s, i32 c):cstr

#linkid strcasestr
pub fn strcasestr(cstr haystack, cstr needle):cstr

#linkid memrchr
pub fn memrchr(anyptr s, i32 c, u64 n):anyptr

#linkid mempcpy
pub fn mempcpy(anyptr dst, anyptr src, u64 n):anyptr


// math.h
pub const M_E = 2.7182818284590452354
pub const M_LOG2E = 1.4426950408889634074
pub const M_LOG10E = 0.43429448190325182765
pub const M_LN2 = 0.69314718055994530942
pub const M_LN10 = 2.30258509299404568402
pub const M_PI = 3.14159265358979323846
pub const M_PI_2 = 1.57079632679489661923
pub const M_PI_4 = 0.78539816339744830962
pub const M_1_PI = 0.31830988618379067154
pub const M_2_PI = 0.63661977236758134308
pub const M_2_SQRTPI = 1.12837916709551257390
pub const M_SQRT2 = 1.41421356237309504880
pub const M_SQRT1_2 = 0.70710678118654752440

#linkid acos
pub fn acos(f64 x):f64

#linkid acosf
pub fn acosf(f32 x):f32

#linkid acosh
pub fn acosh(f64 x):f64

#linkid acoshf
pub fn acoshf(f32 x):f32

#linkid asin
pub fn asin(f64 x):f64

#linkid asinf
pub fn asinf(f32 x):f32

#linkid asinh
pub fn asinh(f64 x):f64

#linkid asinhf
pub fn asinhf(f32 x):f32

#linkid atan
pub fn atan(f64 x):f64

#linkid atanf
pub fn atanf(f32 x):f32

#linkid atan2
pub fn atan2(f64 y, f64 x):f64

#linkid atan2f
pub fn atan2f(f32 y, f32 x):f32

#linkid cos
pub fn cos(f64 x):f64

#linkid cosf
pub fn cosf(f32 x):f32

#linkid sin
pub fn sin(f64 x):f64

#linkid sinf
pub fn sinf(f32 x):f32

#linkid tan
pub fn tan(f64 x):f64

#linkid tanf
pub fn tanf(f32 x):f32

#linkid atanh
pub fn atanh(f64 x):f64

#linkid atanhf
pub fn atanhf(f32 x):f32

#linkid cosh
pub fn cosh(f64 x):f64

#linkid coshf
pub fn coshf(f32 x):f32

#linkid sinh
pub fn sinh(f64 x):f64

#linkid sinhf
pub fn sinhf(f32 x):f32

#linkid sqrt
pub fn sqrt(f64 x):f64

#linkid sqrtf
pub fn sqrtf(f32 x):f32

#linkid tanh
pub fn tanh(f64 x):f64

#linkid tanhf
pub fn tanhf(f32 x):f32

#linkid exp
pub fn exp(f64 x):f64

#linkid expf
pub fn expf(f32 x):f32

#linkid exp2
pub fn exp2(f64 x):f64

#linkid exp2f
pub fn exp2f(f32 x):f32

#linkid expm1
pub fn expm1(f64 x):f64

#linkid expm1f
pub fn expm1f(f32 x):f32

#linkid fabs
pub fn fabs(f64 x):f64

#linkid fabsf
pub fn fabsf(f32 x):f32

#linkid log
pub fn log(f64 x):f64

#linkid logf
pub fn logf(f32 x):f32

#linkid log10
pub fn log10(f64 x):f64

#linkid log10f
pub fn log10f(f32 x):f32

#linkid log1p
pub fn log1p(f64 x):f64

#linkid log1pf
pub fn log1pf(f32 x):f32

#linkid log2
pub fn log2(f64 x):f64

#linkid log2f
pub fn log2f(f32 x):f32

#linkid logb
pub fn logb(f64 x):f64

#linkid logbf
pub fn logbf(f32 x):f32

// 幂函数
#linkid pow
pub fn pow(f64 x, f64 y):f64

#linkid powf
pub fn powf(f32 x, f32 y):f32

#linkid cbrt
pub fn cbrt(f64 x):f64

#linkid cbrtf
pub fn cbrtf(f32 x):f32

#linkid hypot
pub fn hypot(f64 x, f64 y):f64

#linkid hypotf
pub fn hypotf(f32 x, f32 y):f32

#linkid ceil
pub fn ceil(f64 x):f64

#linkid ceilf
pub fn ceilf(f32 x):f32

#linkid floor
pub fn floor(f64 x):f64

#linkid floorf
pub fn floorf(f32 x):f32

#linkid trunc
pub fn trunc(f64 x):f64

#linkid truncf
pub fn truncf(f32 x):f32

#linkid rint
pub fn rint(f64 x):f64

#linkid rintf
pub fn rintf(f32 x):f32

#linkid nearbyint
pub fn nearbyint(f64 x):f64

#linkid nearbyintf
pub fn nearbyintf(f32 x):f32

#linkid lrint
pub fn lrint(f64 x):i64

#linkid lrintf
pub fn lrintf(f32 x):i64

#linkid llrint
pub fn llrint(f64 x):i64

#linkid llrintf
pub fn llrintf(f32 x):i64

#linkid lround
pub fn lround(f64 x):i64

#linkid lroundf
pub fn lroundf(f32 x):i64

#linkid llround
pub fn llround(f64 x):i64

#linkid llroundf
pub fn llroundf(f32 x):i64

#linkid copysign
pub fn copysign(f64 x, f64 y):f64

#linkid copysignf
pub fn copysignf(f32 x, f32 y):f32

#linkid frexp
pub fn frexp(f64 x, ptr<i32> exp):f64

#linkid frexpf
pub fn frexpf(f32 x, ptr<i32> exp):f32

#linkid ldexp
pub fn ldexp(f64 x, i32 exp):f64

#linkid ldexpf
pub fn ldexpf(f32 x, i32 exp):f32

#linkid modf
pub fn modf(f64 x, ptr<f64> iptr):f64

#linkid modff
pub fn modff(f32 x, ptr<f32> iptr):f32

#linkid scalbn
pub fn scalbn(f64 x, i32 n):f64

#linkid scalbnf
pub fn scalbnf(f32 x, i32 n):f32

#linkid scalbln
pub fn scalbln(f64 x, i64 n):f64

#linkid scalblnf
pub fn scalblnf(f32 x, i64 n):f32

#linkid round
pub fn round(f64 x):f64

#linkid roundf
pub fn roundf(f32 x):f32

#linkid ilogb
pub fn ilogb(f64 x):i32

#linkid ilogbf
pub fn ilogbf(f32 x):i32

// 浮点余数和商函数
#linkid fmod
pub fn fmod(f64 x, f64 y):f64

#linkid fmodf
pub fn fmodf(f32 x, f32 y):f32

#linkid remainder
pub fn remainder(f64 x, f64 y):f64

#linkid remainderf
pub fn remainderf(f32 x, f32 y):f32

#linkid remquo
pub fn remquo(f64 x, f64 y, ptr<i32> quo):f64

#linkid remquof
pub fn remquof(f32 x, f32 y, ptr<i32> quo):f32

#linkid fmax
pub fn fmax(f64 x, f64 y):f64

#linkid fmaxf
pub fn fmaxf(f32 x, f32 y):f32

#linkid fmin
pub fn fmin(f64 x, f64 y):f64

#linkid fminf
pub fn fminf(f32 x, f32 y):f32

#linkid fdim
pub fn fdim(f64 x, f64 y):f64

#linkid fdimf
pub fn fdimf(f32 x, f32 y):f32

// 融合乘加函数
#linkid fma
pub fn fma(f64 x, f64 y, f64 z):f64

#linkid fmaf
pub fn fmaf(f32 x, f32 y, f32 z):f32

// 特殊函数
#linkid erf
pub fn erf(f64 x):f64

#linkid erff
pub fn erff(f32 x):f32

#linkid erfc
pub fn erfc(f64 x):f64

#linkid erfcf
pub fn erfcf(f32 x):f32

#linkid lgamma
pub fn lgamma(f64 x):f64

#linkid lgammaf
pub fn lgammaf(f32 x):f32

#linkid tgamma
pub fn tgamma(f64 x):f64

#linkid tgammaf
pub fn tgammaf(f32 x):f32

// NaN 函数
#linkid nan
pub fn nan(cstr tagp):f64

#linkid nanf
pub fn nanf(cstr tagp):f32

// 下一个可表示值函数
#linkid nextafter
pub fn nextafter(f64 x, f64 y):f64

#linkid nextafterf
pub fn nextafterf(f32 x, f32 y):f32

#linkid nexttoward
pub fn nexttoward(f64 x, f64 y):f64

#linkid nexttowardf
pub fn nexttowardf(f32 x, f64 y):f32

// Bessel 函数 (POSIX 扩展)
#linkid j0
pub fn j0(f64 x):f64

#linkid j0f
pub fn j0f(f32 x):f32

#linkid j1
pub fn j1(f64 x):f64

#linkid j1f
pub fn j1f(f32 x):f32

#linkid jn
pub fn jn(i32 n, f64 x):f64

#linkid jnf
pub fn jnf(i32 n, f32 x):f32

#linkid y0
pub fn y0(f64 x):f64

#linkid y0f
pub fn y0f(f32 x):f32

#linkid y1
pub fn y1(f64 x):f64

#linkid y1f
pub fn y1f(f32 x):f32

#linkid yn
pub fn yn(i32 n, f64 x):f64

#linkid ynf
pub fn ynf(i32 n, f32 x):f32

// GNU/BSD 扩展
#linkid drem
pub fn drem(f64 x, f64 y):f64

#linkid dremf
pub fn dremf(f32 x, f32 y):f32

#linkid finite
pub fn finite(f64 x):i32

#linkid finitef
pub fn finitef(f32 x):i32

#linkid scalb
pub fn scalb(f64 x, f64 n):f64

#linkid scalbf
pub fn scalbf(f32 x, f32 n):f32

#linkid significand
pub fn significand(f64 x):f64

#linkid significandf
pub fn significandf(f32 x):f32

#linkid lgamma_r
pub fn lgamma_r(f64 x, ptr<i32> signgamp):f64

#linkid lgammaf_r
pub fn lgammaf_r(f32 x, ptr<i32> signgamp):f32

#linkid sincos
pub fn sincos(f64 x, ptr<f64> sin, ptr<f64> cos):void

#linkid sincosf
pub fn sincosf(f32 x, ptr<f32> sin, ptr<f32> cos):void

#linkid exp10
pub fn exp10(f64 x):f64

#linkid exp10f
pub fn exp10f(f32 x):f32

#linkid pow10
pub fn pow10(f64 x):f64

#linkid pow10f
pub fn pow10f(f32 x):f32


// time.h
// Time types
pub type time_t = i64
type clock_t = i64
pub type clockid_t = i32
pub type timer_t = anyptr

// Time constants
pub const CLOCKS_PER_SEC = 1000000
pub const TIME_UTC = 1

// Clock types
pub const CLOCK_REALTIME = 0
pub const CLOCK_MONOTONIC = 1
pub const CLOCK_PROCESS_CPUTIME_ID = 2
pub const CLOCK_THREAD_CPUTIME_ID = 3
pub const CLOCK_MONOTONIC_RAW = 4
pub const CLOCK_REALTIME_COARSE = 5
pub const CLOCK_MONOTONIC_COARSE = 6
pub const CLOCK_BOOTTIME = 7
const CLOCK_REALTIME_ALARM = 8
pub const CLOCK_BOOTTIME_ALARM = 9
const CLOCK_SGI_CYCLE = 10
pub const CLOCK_TAI = 11

// Timer constants
pub const TIMER_ABSTIME = 1

// Time structures
pub type timespec = struct {
    i64 tv_sec
    i64 tv_nsec
}

pub type itimerspec = struct {
    timespec it_interval
    timespec it_value
}

/* ISO C `broken-down time' structure.  */
pub type tm = struct {
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

// Basic time functions
#linkid time
pub fn time(ptr<time_t> t):time_t

#linkid clock
pub fn clock():clock_t

#linkid difftime
pub fn difftime(time_t time1, time_t time0):f64

#linkid mktime
pub fn mktime(ptr<tm> time_info):time_t

// Time formatting
#linkid strftime
pub fn strftime(cstr s, u64 size, cstr format, ptr<tm> time_info):u64

#linkid strftime_l
pub fn strftime_l(cstr s, u64 size, cstr format, ptr<tm> time_info, locale_t locale):u64

// Time conversion functions
pub fn localtime(ptr<time_t> timestamp):ptr<tm> {
    return cross.localtime(timestamp as anyptr) as ptr<tm>
}

pub fn gmtime(ptr<time_t> timestamp):ptr<tm> {
    return cross.gmtime(timestamp as anyptr) as ptr<tm>
}

#linkid asctime
pub fn asctime(ptr<tm> tm):cstr

#linkid ctime
pub fn ctime(ptr<time_t> timestamp):cstr

// Thread-safe versions
#linkid localtime_r
pub fn localtime_r(ptr<time_t> timestamp, ptr<tm> result):ptr<tm>

#linkid gmtime_r
pub fn gmtime_r(ptr<time_t> timestamp, ptr<tm> result):ptr<tm>

#linkid asctime_r
pub fn asctime_r(ptr<tm> tm, cstr buf):cstr

#linkid ctime_r
pub fn ctime_r(ptr<time_t> timestamp, cstr buf):cstr

// Time parsing
#linkid strptime
pub fn strptime(cstr s, cstr format, ptr<tm> tm):cstr

// High-resolution time functions
#linkid timespec_get
pub fn timespec_get(ptr<timespec> ts, i32 base):i32

#linkid nanosleep
pub fn nanosleep(ptr<timespec> req, ptr<timespec> rem):i32

// Clock functions
#linkid clock_getres
pub fn clock_getres(clockid_t clk_id, ptr<timespec> res):i32

#linkid clock_gettime
pub fn clock_gettime(clockid_t clk_id, ptr<timespec> tp):i32

#linkid clock_settime
pub fn clock_settime(clockid_t clk_id, ptr<timespec> tp):i32

#linkid clock_nanosleep
pub fn clock_nanosleep(clockid_t clk_id, i32 flags, ptr<timespec> req, ptr<timespec> rem):i32

#linkid clock_getcpuclockid
pub fn clock_getcpuclockid(i32 pid, ptr<clockid_t> clk_id):i32

// Timer functions
#linkid timer_create
pub fn timer_create(clockid_t clk_id, anyptr sevp, ptr<timer_t> timerid):i32

#linkid timer_delete
pub fn timer_delete(timer_t timerid):i32

#linkid timer_settime
pub fn timer_settime(timer_t timerid, i32 flags, ptr<itimerspec> new_value, ptr<itimerspec> old_value):i32

#linkid timer_gettime
pub fn timer_gettime(timer_t timerid, ptr<itimerspec> curr_value):i32

#linkid timer_getoverrun
pub fn timer_getoverrun(timer_t timerid):i32

// Timezone functions
#linkid tzset
pub fn tzset():void

// GNU/BSD extensions
#linkid stime
pub fn stime(ptr<time_t> t):i32

pub fn timegm(ptr<tm> value):time_t {
    return cross.timegm(value as anyptr) as time_t
}

#linkid getdate
pub fn getdate(cstr str):ptr<tm>

// unistd.h - File operations
#linkid pipe
pub fn pipe(anyptr pipefd):i32

#linkid pipe2
pub fn pipe2(anyptr pipefd, i32 flags):i32

#linkid close
pub fn close(i32 fd):i32

#linkid posix_close
pub fn posix_close(i32 fd, i32 flags):i32

#linkid dup
pub fn dup(i32 oldfd):i32

#linkid dup2
pub fn dup2(i32 oldfd, i32 newfd):i32

#linkid dup3
pub fn dup3(i32 oldfd, i32 newfd, i32 flags):i32

#linkid lseek
pub fn lseek(i32 fd, i64 offset, i32 whence):i64

#linkid fsync
pub fn fsync(i32 fd):i32

#linkid fdatasync
pub fn fdatasync(i32 fd):i32

#linkid read
pub fn read(i32 fd, anyptr buf, u64 count):i64

#linkid write
pub fn write(i32 fd, anyptr buf, u64 count):i64

#linkid pread
pub fn pread(i32 fd, anyptr buf, u64 count, i64 offset):i64

#linkid pwrite
pub fn pwrite(i32 fd, anyptr buf, u64 count, i64 offset):i64

// unistd.h - File ownership and permissions
#linkid chown
pub fn chown(cstr path, u32 owner, u32 group):i32

#linkid fchown
pub fn fchown(i32 fd, u32 owner, u32 group):i32

#linkid lchown
pub fn lchown(cstr path, u32 owner, u32 group):i32

#linkid fchownat
pub fn fchownat(i32 dirfd, cstr path, u32 owner, u32 group, i32 flags):i32

// unistd.h - File linking
#linkid link
pub fn link(cstr oldpath, cstr newpath):i32

#linkid linkat
pub fn linkat(i32 olddirfd, cstr oldpath, i32 newdirfd, cstr newpath, i32 flags):i32

#linkid symlink
pub fn symlink(cstr target, cstr linkpath):i32

#linkid symlinkat
pub fn symlinkat(cstr target, i32 newdirfd, cstr linkpath):i32

#linkid readlink
pub fn readlink(cstr path, cstr buf, u64 bufsiz):i64

#linkid readlinkat
pub fn readlinkat(i32 dirfd, cstr path, cstr buf, u64 bufsiz):i64

#linkid unlink
pub fn unlink(cstr path):i32

#linkid unlinkat
pub fn unlinkat(i32 dirfd, cstr path, i32 flags):i32

#linkid rmdir
pub fn rmdir(cstr path):i32

#linkid truncate
pub fn truncate(cstr path, i64 length):i32

#linkid ftruncate
pub fn ftruncate(i32 fd, i64 length):i32

// unistd.h - File access
#linkid access
pub fn access(cstr path, i32 mode):i32

#linkid faccessat
pub fn faccessat(i32 dirfd, cstr path, i32 mode, i32 flags):i32

// unistd.h - Directory operations
#linkid chdir
pub fn chdir(cstr path):i32

#linkid fchdir
pub fn fchdir(i32 fd):i32

// unistd.h - Process control
#linkid alarm
pub fn alarm(u32 seconds):u32

#linkid sleep
pub fn sleep(int second)

#linkid usleep
pub fn usleep(u32 usec):i32

#linkid pause
pub fn pause():i32

#linkid _Fork
pub fn _fork():i32

#linkid execve
pub fn execve(cstr filename, ptr<cstr> argv, ptr<cstr> envp):i32

#linkid execv
pub fn execv(cstr path, ptr<cstr> argv):i32

// not support
// #linkid execle
// fn execle(cstr path, cstr arg, ...):i32

// #linkid execl
// fn execl(cstr path, cstr arg, ...):i32

// #linkid execvp
// fn execvp(cstr file, ptr<cstr> argv):i32

// #linkid execlp
// fn execlp(cstr file, cstr arg, ...):i32

// #linkid fexecve
// fn fexecve(i32 fd, ptr<cstr> argv, ptr<cstr> envp):i32

// unistd.h - Process identification
#linkid getpid
pub fn getpid():i32

#linkid getppid
pub fn getppid():i32

#linkid getpgrp
pub fn getpgrp():i32

#linkid getpgid
pub fn getpgid(i32 pid):i32

#linkid setpgid
pub fn setpgid(i32 pid, i32 pgid):i32

#linkid setsid
pub fn setsid():i32

#linkid getsid
pub fn getsid(i32 pid):i32

#linkid ttyname
pub fn ttyname(i32 fd):cstr

#linkid ttyname_r
pub fn ttyname_r(i32 fd, cstr buf, u64 buflen):i32

#linkid isatty
pub fn isatty(i32 fd):i32

#linkid tcgetpgrp
pub fn tcgetpgrp(i32 fd):i32

#linkid tcsetpgrp
pub fn tcsetpgrp(i32 fd, i32 pgrp):i32

// unistd.h - User and group identification
#linkid getuid
pub fn getuid():u32

#linkid geteuid
pub fn geteuid():u32

#linkid getgid
pub fn getgid():u32

#linkid getegid
pub fn getegid():u32

#linkid getgroups
pub fn getgroups(i32 size, ptr<u32> list):i32

#linkid setuid
pub fn setuid(u32 uid):i32

#linkid seteuid
pub fn seteuid(u32 euid):i32

#linkid setgid
pub fn setgid(u32 gid):i32

#linkid setegid
pub fn setegid(u32 egid):i32

// unistd.h - Login and hostname
#linkid getlogin
pub fn getlogin():cstr

#linkid getlogin_r
pub fn getlogin_r(cstr buf, u64 bufsize):i32

#linkid gethostname
pub fn gethostname(cstr name, u64 len):i32

#linkid ctermid
pub fn ctermid(cstr s):cstr

// unistd.h - Command line options
#linkid getopt
pub fn getopt(i32 argc, ptr<cstr> argv, cstr optstring):i32

// unistd.h - Configuration
#linkid pathconf
pub fn pathconf(cstr path, i32 name):i64

#linkid fpathconf
pub fn fpathconf(i32 fd, i32 name):i64

#linkid sysconf
pub fn sysconf(i32 name):i64

#linkid confstr
pub fn confstr(i32 name, cstr buf, u64 len):u64

// unistd.h - POSIX extensions
#linkid setreuid
pub fn setreuid(u32 ruid, u32 euid):i32

#linkid setregid
pub fn setregid(u32 rgid, u32 egid):i32

#linkid lockf
pub fn lockf(i32 fd, i32 cmd, i64 len):i32

#linkid gethostid
pub fn gethostid():i64

#linkid nice
pub fn nice(i32 inc):i32

#linkid sync
pub fn sync():void

#linkid setpgrp
pub fn setpgrp():i32

#linkid crypt
pub fn crypt(cstr key, cstr salt):cstr

#linkid encrypt
pub fn encrypt(cstr block, i32 edflag):void

#linkid swab
pub fn swab(anyptr from, anyptr to, i64 n):void

#linkid ualarm
pub fn ualarm(u32 value, u32 interval):u32

// unistd.h - BSD/GNU extensions
#linkid brk
pub fn brk(anyptr addr):i32

#linkid sbrk
pub fn sbrk(i64 increment):anyptr

#linkid vfork
pub fn vfork():i32

#linkid vhangup
pub fn vhangup():i32

#linkid chroot
pub fn chroot(cstr path):i32

#linkid getpagesize
pub fn getpagesize():i32

#linkid getdtablesize
pub fn getdtablesize():i32

#linkid sethostname
pub fn sethostname(cstr name, u64 len):i32

#linkid getdomainname
pub fn getdomainname(cstr name, u64 len):i32

#linkid setdomainname
pub fn setdomainname(cstr name, u64 len):i32

#linkid setgroups
pub fn setgroups(u64 size, ptr<u32> list):i32

#linkid getpass
pub fn getpass(cstr prompt):cstr

#linkid daemon
pub fn daemon(i32 nochdir, i32 noclose):i32

#linkid setusershell
pub fn setusershell():void

#linkid endusershell
pub fn endusershell():void

#linkid getusershell
pub fn getusershell():cstr

#linkid acct
pub fn acct(cstr filename):i32

// #linkid syscall
// fn syscall(i64 number, ...):i64

#linkid execvpe
pub fn execvpe(cstr file, ptr<cstr> argv, ptr<cstr> envp):i32

#linkid issetugid
pub fn issetugid():i32

#linkid getentropy
pub fn getentropy(anyptr buffer, u64 length):i32

// unistd.h - GNU extensions
#os linux #linkid setresuid
pub fn setresuid(u32 ruid, u32 euid, u32 suid):i32

#os linux #linkid setresgid
pub fn setresgid(u32 rgid, u32 egid, u32 sgid):i32

#linkid getresuid
pub fn getresuid(ptr<u32> ruid, ptr<u32> euid, ptr<u32> suid):i32

#os linux #linkid getresgid
pub fn getresgid(ptr<u32> rgid, ptr<u32> egid, ptr<u32> sgid):i32

#linkid get_current_dir_name
pub fn get_current_dir_name():cstr

#linkid syncfs
pub fn syncfs(i32 fd):i32

#linkid euidaccess
pub fn euidaccess(cstr pathname, i32 mode):i32

#linkid eaccess
pub fn eaccess(cstr pathname, i32 mode):i32

#linkid copy_file_range
pub fn copy_file_range(i32 fd_in, ptr<i64> off_in, i32 fd_out, ptr<i64> off_out, u64 len, u32 flags):i64

#linkid gettid
pub fn gettid():i32

// fcntl.h
// Type definitions
pub type off_t = i64
pub type pid_t = i32
pub type mode_t = u32

// File lock structure
pub type flock = struct {
    i16 l_type
    i16 l_whence
    off_t l_start
    off_t l_len
    pid_t l_pid
}

// File access modes
pub const O_ACCMODE = 0o3
pub const O_RDONLY = 0o0
pub const O_WRONLY = 0o1
pub const O_RDWR = 0o2
const O_SEARCH = 0o200000000
pub const O_EXEC = 0o200000000
pub const O_PATH = 0o200000000
pub const O_TTY_INIT = 0

// File creation flags
pub const O_CREAT = 0o100
pub const O_EXCL = 0o200
const O_NOCTTY = 0o400
pub const O_TRUNC = 0o1000

// File status flags
pub const O_APPEND = 0o2000
const O_ASYNC = 0o20000
pub const O_DSYNC = 0o10000
pub const O_NONBLOCK = 0o4000
const O_NDELAY = 0o4000
pub const O_SYNC = 0o4010000

// fcntl commands
pub const F_DUPFD = 0
pub const F_GETFD = 1
pub const F_SETFD = 2
pub const F_GETFL = 3
pub const F_SETFL = 4
pub const F_GETLK = 5
pub const F_SETLK = 6
pub const F_SETLKW = 7
pub const F_GETOWN = 9
pub const F_SETOWN = 8

// OFD locks
pub const F_OFD_GETLK = 36
pub const F_OFD_SETLK = 37
pub const F_OFD_SETLKW = 38

// Additional fcntl commands
pub const F_DUPFD_CLOEXEC = 1030

// Lock types
pub const F_RDLCK = 0
pub const F_WRLCK = 1
pub const F_UNLCK = 2

// File descriptor flags
pub const FD_CLOEXEC = 1

// AT constants
pub const AT_FDCWD = -100
pub const AT_SYMLINK_NOFOLLOW = 0x100
pub const AT_REMOVEDIR = 0x200
pub const AT_SYMLINK_FOLLOW = 0x400
pub const AT_EACCESS = 0x200
pub const AT_NO_AUTOMOUNT = 0x800
pub const AT_EMPTY_PATH = 0x1000
pub const AT_STATX_SYNC_TYPE = 0x6000
pub const AT_STATX_SYNC_AS_STAT = 0x0000
pub const AT_STATX_FORCE_SYNC = 0x2000
pub const AT_STATX_DONT_SYNC = 0x4000
pub const AT_RECURSIVE = 0x8000

// POSIX advisory information constants
pub const POSIX_FADV_NORMAL = 0
pub const POSIX_FADV_RANDOM = 1
pub const POSIX_FADV_SEQUENTIAL = 2
pub const POSIX_FADV_WILLNEED = 3
pub const POSIX_FADV_DONTNEED = 4
pub const POSIX_FADV_NOREUSE = 5


// File mode constants
pub const S_ISUID = 0o4000
pub const S_ISGID = 0o2000
pub const S_ISVTX = 0o1000
pub const S_IRUSR = 0o400
pub const S_IWUSR = 0o200
pub const S_IXUSR = 0o100
pub const S_IRWXU = 0o700
pub const S_IRGRP = 0o40
pub const S_IWGRP = 0o20
pub const S_IXGRP = 0o10
pub const S_IRWXG = 0o70
pub const S_IROTH = 0o4
pub const S_IWOTH = 0o2
pub const S_IXOTH = 0o1
pub const S_IRWXO = 0o7

// Access mode constants
pub const F_OK = 0
pub const R_OK = 4
pub const W_OK = 2
pub const X_OK = 1

// File locking constants
pub const F_ULOCK = 0
pub const F_LOCK = 1
pub const F_TLOCK = 2
pub const F_TEST = 3

// Additional fcntl commands (GNU/Linux extensions)
pub const F_SETLEASE = 1024
pub const F_GETLEASE = 1025
pub const F_NOTIFY = 1026
pub const F_CANCELLK = 1029
pub const F_SETPIPE_SZ = 1031
pub const F_GETPIPE_SZ = 1032
pub const F_ADD_SEALS = 1033
pub const F_GET_SEALS = 1034

// File sealing constants
pub const F_SEAL_SEAL = 0x0001
pub const F_SEAL_SHRINK = 0x0002
pub const F_SEAL_GROW = 0x0004
pub const F_SEAL_WRITE = 0x0008
pub const F_SEAL_FUTURE_WRITE = 0x0010

// Read/write hint constants
pub const F_GET_RW_HINT = 1035
pub const F_SET_RW_HINT = 1036
pub const F_GET_FILE_RW_HINT = 1037
pub const F_SET_FILE_RW_HINT = 1038

pub const RWF_WRITE_LIFE_NOT_SET = 0
pub const RWH_WRITE_LIFE_NONE = 1
pub const RWH_WRITE_LIFE_SHORT = 2
pub const RWH_WRITE_LIFE_MEDIUM = 3
pub const RWH_WRITE_LIFE_LONG = 4
pub const RWH_WRITE_LIFE_EXTREME = 5

// Directory notification constants
pub const DN_ACCESS = 0x00000001
pub const DN_MODIFY = 0x00000002
pub const DN_CREATE = 0x00000004
pub const DN_DELETE = 0x00000008
pub const DN_RENAME = 0x00000010
pub const DN_ATTRIB = 0x00000020
pub const DN_MULTISHOT = 0x80000000

// File owner types
pub const F_OWNER_TID = 0
pub const F_OWNER_PID = 1
pub const F_OWNER_PGRP = 2
pub const F_OWNER_GID = 2


// Fallocate flags
pub const FALLOC_FL_KEEP_SIZE = 1
pub const FALLOC_FL_PUNCH_HOLE = 2
pub const MAX_HANDLE_SZ = 128

// Sync file range flags
pub const SYNC_FILE_RANGE_WAIT_BEFORE = 1
pub const SYNC_FILE_RANGE_WRITE = 2
pub const SYNC_FILE_RANGE_WAIT_AFTER = 4

// Splice flags
pub const SPLICE_F_MOVE = 1
pub const SPLICE_F_NONBLOCK = 2
pub const SPLICE_F_MORE = 4
pub const SPLICE_F_GIFT = 8

// Basic file operations
#linkid creat
pub fn creat(cstr pathname, mode_t mode):i32

#linkid fcntl
pub fn fcntl(i32 fd, i32 cmd, anyptr flag):i32

#linkid open
pub fn open(cstr pathname, i32 flags, u32 mode):i32

#linkid openat
pub fn openat(i32 dirfd, cstr pathname, i32 flags, u32 mode):i32

// POSIX advisory functions
#linkid posix_fadvise
pub fn posix_fadvise(i32 fd, off_t offset, off_t len, i32 advice):i32

#linkid posix_fallocate
pub fn posix_fallocate(i32 fd, off_t offset, off_t len):i32

// GNU/Linux extensions
#linkid fallocate
pub fn fallocate(i32 fd, i32 mode, off_t offset, off_t len):i32

#linkid name_to_handle_at
pub fn name_to_handle_at(i32 dirfd, cstr pathname, anyptr handle, ptr<i32> mount_id, i32 flags):i32

#linkid open_by_handle_at
pub fn open_by_handle_at(i32 mount_fd, anyptr handle, i32 flags):i32

#linkid readahead
pub fn readahead(i32 fd, off_t offset, u64 count):i64

#linkid sync_file_range
pub fn sync_file_range(i32 fd, off_t offset, off_t nbytes, u32 flags):i32

// Splice operations
#linkid vmsplice
pub fn vmsplice(i32 fd, anyptr iov, u64 nr_segs, u32 flags):i64

#linkid splice
pub fn splice(i32 fd_in, ptr<off_t> off_in, i32 fd_out, ptr<off_t> off_out, u64 len, u32 flags):i64

#linkid tee
pub fn tee(i32 fd_in, i32 fd_out, u64 len, u32 flags):i64



// signal.h
#linkid std_args
pub fn std_args():[string]

// 主机顺序转网络顺序
#linkid htons
pub fn htons(u16 host):u16

#linkid htonl
pub fn htonl(u32 host):u32

// 网络顺序转主机顺序
#linkid ntohs
pub fn ntohs(u16 x):u16

#linkid ntohl
pub fn ntohl(u32 x):u32

#linkid inet_pton
pub fn inet_pton(i32 family, anyptr src_str, anyptr dst_buf):i32

#linkid inet_ntop
pub fn inet_ntop(i32 family, anyptr src_buf, anyptr dst_str, i32 len):i32

/*
 * Protections are chosen from these bits, or-ed together
 */
int PROT_NONE = 0x00    /* [MC2] no permissions */
int PROT_READ = 0x01    /* [MC2] pages can be read */
int PROT_WRITE = 0x02    /* [MC2] pages can be written */
int PROT_EXEC = 0x04    /* [MC2] pages can be executed */

int MAP_ANON = 0x1000
int MAP_COPY = 0x2
int MAP_FILE = 0x0
int MAP_FIXED = 0x10
int MAP_HASSEMAPHORE = 0x200
int MAP_JIT = 0x800
int MAP_NOCACHE = 0x400
int MAP_NOEXTEND = 0x100
int MAP_NORESERVE = 0x40
int MAP_PRIVATE = 0x2
int MAP_RENAME = 0x20

// 通过空值 options 实现阻塞和非阻塞模式
#linkid waitpid
pub fn waitpid(int pid, ptr<int> status, int options):int

// --- signal 相关 <sys/signalfd.h> 和 <signal.h>
pub type sigset_t = struct {
    [u64;16] __val
}

pub type signalfd_siginfo_t = struct {
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

#linkid sigemptyset
pub fn sigemptyset(ref<sigset_t> sigset):i32

#linkid sigaddset
pub fn sigaddset(ref<sigset_t> sigset, i32 signo):i32

#linkid sigfillset
pub fn sigfillset(ref<sigset_t> sigset):i32

#linkid sigprocmask
pub fn sigprocmask(i32 how, ref<sigset_t> sigset, ptr<sigset_t> oldset):i32

#linkid signalfd
pub fn signalfd(int fd, ref<sigset_t> mask, i32 flags):i32

#linkid prctl
pub fn prctl(int option, u64 arg2, u64 arg3, u64 arg4, u64 arg5):int

#linkid uv_hrtime
pub fn uv_hrtime():u64

// 读取当前全局的 errno 编码
#linkid rt_errno
pub fn errno():int

#linkid rt_get_envs
pub fn get_envs():[string]

#linkid fork
pub fn fork():int

#linkid getcwd
pub fn getcwd(cstr path, uint size):cstr

#linkid mmap
pub fn mmap(anyptr addr, int len, int prot, int flags, int fd, int off):anyptr

#linkid munmap
pub fn munmap(anyptr addr, int len)

#linkid isprint
pub fn isprint(u8 c):bool

#linkid isspace
pub fn isspace(u8 c):bool

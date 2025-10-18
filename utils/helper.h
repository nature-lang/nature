#ifndef NATURE_SRC_LIB_HELPER_H_
#define NATURE_SRC_LIB_HELPER_H_

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "assertf.h"
#include "errno.h"
#include "log.h"

#define LAMBDA(return_type, body) \
    ({ return_type __fn__ body __fn__; })

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define string char *
#define STRING_EOF '\0'

#define VOID (void) !

#define v_addr_t uint64_t
#define addr_t uint64_t

static char tlsprintf_buf[1024];

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))


static inline void *reallocator(void *ptr, uint64_t size) {
    void *ptr1;
    if (size == 0) {
        free(ptr);
        ptr1 = NULL;
    } else {
        ptr1 = realloc(ptr, size);
        if (!ptr1) {
            assertf(false, "memory full");
            exit(1);
        }
    }
    return ptr1;
}

static inline void *mallocz(uint64_t size) {
    assert(size > 0);

    void *ptr;
    ptr = reallocator(NULL, size);
    assertf(ptr != NULL, "memory full");
    if (size) {
        memset(ptr, 0, size);
    }
    return ptr;
}

#define CONTAINER_OF(ptr, type, member) \
    ((type *) ((char *) (ptr) - offsetof(type, member)))


#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

#define NEW(type) mallocz(sizeof(type))

#define FLAG(value) (1 << value)

#define COPY_NEW(_type, src)                 \
    ({                                       \
        _type *dst = mallocz(sizeof(_type)); \
        memcpy(dst, src, sizeof(_type));     \
        dst;                                 \
    })

// 抢占式调度与 coroutine dispatch 使用该 debug 函数
#ifdef RUNTIME_DEBUG_LOG

#define RDEBUGF(format, ...)                                                                                                  \
    fprintf(stderr, "[%lu] RDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stderr);

#define MDEBUGF(format, ...)                                                                                                        \
    fprintf(stdout, "[%lu] MEMORY DEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#define DEBUGF(format, ...)                                                                                                   \
    fprintf(stdout, "[%lu] DDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#define TRACEF(format, ...)                                                                                                   \
    fprintf(stdout, "[%lu] TTRACE-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#define TDEBUGF(format, ...)                                                                                                  \
    fprintf(stdout, "[%lu] TDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#define TESTDUMP(format, ...)                                                                                                 \
    fprintf(stdout, "[%lu] TDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#else
#define RDEBUGF(...)
#define MDEBUGF(...)
#define DEBUGF(...)
#define TRACEF(...)

#define TDEBUGF(format, ...)                                                                                                  \
    fprintf(stdout, "[%lu] TDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#define TRNDEBUGF(scale, format, ...)                                                                                                   \
    do {                                                                                                                                \
        if ((uv_hrtime() % 1000) < (scale)) {                                                                                           \
            fprintf(stdout, "[%lu] TRNDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
            fflush(stdout);                                                                                                             \
        }                                                                                                                               \
    } while (0)

#define TESTDUMP(format, ...)                                                                                                 \
    fprintf(stdout, "[%lu] TDEBUG-%lu: " format "\n", uv_hrtime() / 1000 / 1000, (uint64_t) uv_thread_self(), ##__VA_ARGS__); \
    fflush(stdout);

#endif

static inline addr_t fetch_addr_value(addr_t addr) {
    // addr 中存储的依旧是 addr，现在需要取出 addr 中存储的值
    addr_t *p = (addr_t *) addr;
    return *p;
}

static inline uint32_t hash_data(uint8_t *data, uint64_t size) {
    assert(size > 0);
    assert(data);

    uint32_t hash = 2166136261u;
    for (int i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}

static inline uint32_t hash_string(char *str) {
    assert(str);
    if (strlen(str) == 0) {
        return 0;
    }
    return hash_data((uint8_t *) str, strlen(str));
}

static inline bool memory_empty(uint8_t *base, uint64_t size) {
    for (int i = 0; i < size; ++i) {
        if (*(base + i) != 0) {
            return false;
        }
    }
    return true;
}

static inline uint16_t read16le(unsigned char *p) {
    return p[0] | (uint16_t) p[1] << 8;
}

static inline void write16le(unsigned char *p, uint16_t x) {
    p[0] = x & 255;
    p[1] = x >> 8 & 255;
}

static inline uint32_t read32le(unsigned char *p) {
    return read16le(p) | (uint32_t) read16le(p + 2) << 16;
}

static inline void write32le(unsigned char *p, uint32_t x) {
    write16le(p, x);
    write16le(p + 2, x >> 16);
}

static inline void add32le(unsigned char *p, int32_t x) {
    write32le(p, read32le(p) + x);
}

static inline uint64_t read64le(unsigned char *p) {
    return read32le(p) | (uint64_t) read32le(p + 4) << 32;
}

static inline void write64le(unsigned char *p, uint64_t x) {
    write32le(p, x);
    write32le(p + 4, x >> 32);
}

static inline void add64le(unsigned char *p, int64_t x) {
    write64le(p, read64le(p) + x);
}

static inline char *dsprintf(char *format, ...) {
    char *buf = mallocz(1024);
    va_list args;
    va_start(args, format);
    int count = vsprintf(buf, format, args);
    va_end(args);

    return realloc(buf, count + 1);
}

static inline char *tlsprintf(char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = vsnprintf(tlsprintf_buf, sizeof(tlsprintf_buf), format, args);
    va_end(args);

    return tlsprintf_buf;
}

static inline char *itoa(int64_t n) {
    // 计算长度
    int length = snprintf(NULL, 0, "%ld", n);

    // 初始化 buf
    char *str = malloc(length + 1);

    // 转换
    snprintf(str, length + 1, "%ld", n);

    return str;
}

static inline char *utoa(uint64_t n) {
    int length = snprintf(NULL, 0, "%lu", n);
    char *str = mallocz(length + 1);
    snprintf(str, length + 1, "%lu", n);

    return str;
}

static inline bool str_equal(char *a, char *b) {
    return strcmp(a, b) == 0;
}

static int inline check_open(char *filepath, int flag) {
    int fd = open(filepath, flag);
    assertf(fd != -1, "cannot open/found file=%s", filepath);
    return fd;
}

static char *str_connect3(const char *a, const char *b, const char *c) {
    // 检查参数是否为 NULL，如果是则视为空字符串
    a = a ? a : "";
    b = b ? b : "";
    c = c ? c : "";

    // 计算总长度
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    size_t len_c = strlen(c);
    size_t total_len = len_a + len_b + len_c;

    // 分配内存
    char *result = mallocz(total_len + 1); // +1 for null terminator
    // 拼接字符串
    memcpy(result, a, len_a);
    memcpy(result + len_a, b, len_b);
    memcpy(result + len_a + len_b, c, len_c);
    result[total_len] = '\0'; // 确保字符串以 null 结尾

    return result;
}

static inline char *str_connect(char *a, char *b) {
    size_t dst_len = strlen(a);
    size_t src_len = strlen(b);
    char *buf = mallocz(dst_len + src_len + 1);
    sprintf(buf, "%s%s", a, b);
    return buf;
}

static inline char *str_connect_free(char *a, char *b) {
    size_t dst_len = strlen(a);
    size_t src_len = strlen(b);
    char *buf = malloc(dst_len + src_len + 1);
    sprintf(buf, "%s%s", a, b);
    free(a);
    free(b);

    return buf;
}

static inline char *str_connect_by(char *a, char *b, char *separator) {
    char *result = str_connect(a, separator);
    result = str_connect(result, b);
    return result;
}

static inline char *path_join(char *dst, char *src) {
    dst = str_connect(dst, "/");
    dst = str_connect(dst, src);
    return dst;
}

static inline void str_replace_char(char *str, char from, char to) {
    size_t len = strlen(str);
    for (int i = 0; i < len; ++i) {
        if (str[i] == from) {
            str[i] = to;
        }
    }
}

static inline char *file_read(char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *) mallocz(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static inline int64_t align_up(int64_t n, int64_t align) {
    if (align == 0) {
        return n;
    }

    return (n + align - 1) & (~(align - 1));
}

char *path_clean(const char *path);

static inline char *path_dir(char *path) {
    assert(path != NULL);
    assert(strlen(path) > 0);
    char *result = strdup(path);

    char *ptr = strrchr(result, '/'); // 查找最后一个斜杠
    if (ptr == NULL) {
        return result;
    }

    *ptr = '\0';
    return result;
}

static inline char *file_name(char *path) {
    char *ptr = strrchr(path, '/');
    if (ptr == NULL) {
        return path; // path 本身就是 file name
    }

    if (*(ptr + 1) == '\0') {
        // 斜杠结尾是文件
        return "";
    }

    return ptr + 1;
}

static inline bool dir_exists(char *dir) {
    struct stat info;

    if (stat(dir, &info) != 0) {
        return false;
    }
    if (info.st_mode & S_IFDIR) {
        return true;
    }

    // 文件存在，但不是一个目录
    return false;
}

static inline bool file_exists(char *path) {
    if (path == NULL) {
        return false;
    }
    return (access(path, R_OK) == 0);
}

static inline char *rtrim(char *str, char *sub) {
    size_t len = strlen(str); // +1 表示 \0 部分
    len = len - strlen(sub) + 1;

    char *res = mallocz(len);

    // strncpy(res, str, len);
    memcpy(res, str, len);
    res[len - 1] = '\0';

    return res;
}

static inline char *ltrim(char *str, char *sub) {
    size_t len = strlen(str);
    size_t sub_len = strlen(sub);

    // Count the number of leading occurrences of the substring
    size_t count = 0;
    while (strncmp(str + count * sub_len, sub, sub_len) == 0) {
        count++;
    }

    // Calculate the length of the resulting string after trimming
    size_t res_len = len - count * sub_len + 1;

    // Allocate memory for the resulting trimmed string
    char *res = mallocz(res_len);

    // Copy the characters after the leading occurrences of the substring to the result
    memcpy(res, str + count * sub_len, res_len - 1);

    // Null-terminate the result
    res[res_len - 1] = '\0';

    return res;
}

static inline char *get_workdir() {
    int size = 256;
    char *buf = mallocz(size);
    VOID getcwd(buf, size);
    return buf;
}

static inline void *copy(char *dst, char *src, int mode) {
    FILE *src_fd, *dst_fd;
    src_fd = fopen(src, "rb");
    assert(src_fd && "open src file failed");
    dst_fd = fopen(dst, "wb");
    assert(dst_fd && "open dst file failed");

    fchmod(fileno(dst_fd), mode);

    char buf[1024] = {0};
    while (!feof(src_fd)) {
        size_t size = fread(buf, 1, 1024, src_fd);
        fwrite(buf, 1, size, dst_fd);
    }

    fclose(src_fd);
    fclose(dst_fd);
    return 0;
}

static inline bool starts_with(char *str, char *prefix) {
    if (!str || !prefix) return false;

    while (*prefix) {
        if (*prefix != *str)
            return false;
        prefix++;
        str++;
    }
    return true;
}

static inline bool ends_with(char *str, char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static inline bool remove_prefix(char **s, char *prefix) {
    if (starts_with(*s, prefix)) {
        *s += strlen(prefix);
        return true;
    }
    return false;
}

static inline ssize_t full_read(int fd, void *buf, size_t count) {
    char *cursor = buf;
    size_t read_count = 0;
    while (true) {
        // read 从文件中读取 n 个字符
        // 成功时返回读取到的字节数(为零表示读到文件描述符),  此返回值受文件剩余字节数限制.当返回值小
        // 于指定的字节数时 并不意味着错误;这可能是因为当前可读取的字节数小于指定的 字节数(比如已经接
        // 近文件结尾,或者正在从管道或者终端读取数 据,或者 read()被信号中断).   发生错误时返回-1,并置
        // errno 为相应值.在这种情况下无法得知文件偏移位置是否有变化.
        ssize_t num = read(fd, cursor, count - read_count);
        // 错误处理
        if (num == -1) {
            return num;
        }

        // 读到了 eof
        if (num == 0) {
            return read_count;
        }

        // num 已经读到的字节数
        read_count += num;
        cursor += num;
    }
}

static inline char *str_replace(char *str, char *old, char *new) {
    char *result; // the return string
    char *ins; // the next insert pointer
    char *tmp; // varies
    int len_rep; // length of old (the string to remove)
    int len_with; // length of new (the string to replace old new)
    int len_front; // distance between old and end of last old
    int count; // number of replacements

    // sanity checks and initialization
    if (!str || !old) return NULL;
    len_rep = strlen(old);
    if (len_rep == 0) return NULL; // empty old causes infinite loop during count
    if (!new) new = "";
    len_with = strlen(new);

    // count the number of replacements needed
    ins = str;
    for (count = 0; (tmp = strstr(ins, old)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = mallocz(strlen(str) + (len_with - len_rep) * count + 1);

    if (!result) return NULL;

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of old in str
    //    str points to the remainder of str after "end of old"
    while (count--) {
        ins = strstr(str, old);
        len_front = ins - str;
        tmp = strncpy(tmp, str, len_front) + len_front;
        tmp = strcpy(tmp, new) + len_with;
        str += len_front + len_rep; // move to next "end of old"
    }
    strcpy(tmp, str);
    return result;
}

static inline void *sys_memory_reserve(void *hint, uint64_t size) {
    void *ptr;
#if defined(__DARWIN) && defined(__ARM64)
    ptr = mmap(hint, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, 0, 0);
#else
    ptr = mmap(hint, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
#endif

    assertf(ptr, "runtime mmap failed: %s", strerror(errno));
    return ptr;
}

static inline void *sys_memory_map(void *hint, uint64_t size) {
    void *ptr;
#if defined(__DARWIN) && defined(__ARM64)
    ptr = hint;
    if (mprotect((void *) hint, size, PROT_READ | PROT_WRITE) == -1) {
        assertf(false, "mprotect failed");
    }
//    ptr = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_JIT, 0, 0);
#else
    ptr = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, 0, 0);
    assertf(ptr != MAP_FAILED, "runtime mmap failed: %s", strerror(errno));
    assertf(ptr, "runtime mmap failed: %s", strerror(errno));
#endif

    return ptr;
}

static inline void sys_memory_used_exec(void *addr, uint64_t size) {
    // 为 darwin/arm64 设计
    // 数据清空, 必须在 mprotect exec 之前，exec 之后就不允许写入数据了
    memset(addr, 0, size);

    // 增加 EXEC
    if (mprotect(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
        assertf(false, "mprotect failed, page_start=%p, size=%lu, err=%s", (void *) addr,
                size,
                strerror(errno));
    }
}

static inline void *sys_memory_alloc(uint64_t size) {
    void *ptr;
    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    assertf(ptr, "runtime mmap failed: %s", strerror(errno));
    return ptr;
}

static inline void *mallocz_big(size_t size) {
    int64_t page_size = sysconf(_SC_PAGESIZE);
    //    int page_size = getpagesize();
    size = align_up(size, page_size);
    void *ptr = sys_memory_alloc(size);
    return ptr;
}

static inline void sys_memory_unmap(void *base, uint64_t size) {
    munmap(base, size);
}

#ifdef __LINUX
static inline void sys_memory_unused(void *addr, uint64_t size) {
    madvise(addr, size, MADV_DONTNEED);
}
#elif __DARWIN
static inline void sys_memory_unused(void *addr, uint64_t size) {
    // On Darwin, use MADV_FREE which is similar to MADV_DONTNEED
    if (madvise(addr, size, MADV_FREE_REUSABLE) == -1) {
        assertf(false, "madvise failed: ", strerror(errno));
    }
}

#else
#error "not support arch"
#endif


#ifdef __LINUX
static inline void sys_memory_used(void *addr, uint64_t size) {
    void *ptr = mmap(addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, 0, 0);
    assertf(ptr, "runtime: out of memory");
}
#elif __DARWIN
static inline void sys_memory_used(void *addr, uint64_t size) {
    // On Darwin, use MADV_FREE which is similar to MADV_DONTNEED
    if (madvise(addr, size, MADV_FREE_REUSE) == -1) {
        assertf(false, "madvise failed: ", strerror(errno));
    }
}
#else
#error "not support arch"
#endif

static inline int64_t *take_numbers(char *str, uint64_t count) {
    int64_t *numbers = mallocz(count * sizeof(int64_t));
    int i = 0;
    char *token;
    token = strtok(str, "\n");
    while (token != NULL && i < count) {
        // 使用 atoi 函数将字符串转换为整数，并存入数组中
        numbers[i] = atoll(token);
        i++;
        token = strtok(NULL, "\n"); // 继续提取下一个数字
    }
    return numbers;
}

static inline char *homedir() {
    return getenv("HOME");
}

static inline char *fullpath(char *rel) {
    char *path = (char *) mallocz(PATH_MAX * sizeof(char));
    if (!realpath(rel, path)) {
        return NULL;
    }

    return path;
}

static inline bool str_char(char *str, char c) {
    char *result = strchr(str, c);
    return result != NULL;
}

/**
 * Recursively remove a directory and all its contents
 * This function provides safe cleanup of temporary directories
 * @param path Directory path to remove
 * @return 0 on success, -1 on failure
 */
static inline int rmdir_recursive(const char *path) {
    if (!path || strlen(path) == 0) {
        return -1;
    }

    // Safety check: don't remove system directories
    if (strcmp(path, "/") == 0 || strcmp(path, "/tmp") == 0 ||
        strcmp(path, "/var") == 0 || strcmp(path, "/usr") == 0 ||
        strcmp(path, "/home") == 0 || strcmp(path, "/Users") == 0) {
        log_error("Refusing to remove system directory: %s", path);
        return -1;
    }

    // Check if path exists and is a directory
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0; // Directory doesn't exist, consider it success
    }

    if (!S_ISDIR(st.st_mode)) {
        // It's a file, remove it
        return remove(path);
    }

    // Open directory
    DIR *dir = opendir(path);
    if (!dir) {
        log_error("Failed to open directory: %s", path);
        return -1;
    }

    struct dirent *entry;
    int result = 0;

    // Remove all entries in the directory
    while ((entry = readdir(dir)) != NULL && result == 0) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Build full path
        size_t path_len = strlen(path) + strlen(entry->d_name) + 2;
        char *full_path = malloc(path_len);
        if (!full_path) {
            result = -1;
            break;
        }

        snprintf(full_path, path_len, "%s/%s", path, entry->d_name);

        // Recursively remove
        result = rmdir_recursive(full_path);
        free(full_path);
    }

    closedir(dir);

    // Remove the directory itself if all contents were removed successfully
    if (result == 0) {
        result = rmdir(path);
        if (result != 0) {
            log_error("Failed to remove directory: %s", path);
        }
    }

    return result;
}

static inline void str_rcpy(char *dest, const char *src, size_t n) {
    if (!src || !dest || n == 0) return;

    size_t src_len = strlen(src);
    const char *start = src;

    // 如果源字符串长度大于目标长度，从右侧截取
    if (src_len > n - 1) {
        // 预留 1 字节给 null 终止符
        start = src + (src_len - (n - 1));
    }

    size_t copy_len = (src_len <= n - 1) ? src_len : (n - 1);
    memmove(dest, start, copy_len);
    dest[copy_len] = '\0';
}

#endif // NATURE_SRC_LIB_HELPER_H_

#include "syscall.h"
#include "fcntl.h"
#include "string.h"
#include "runtime/processor.h"
#include "errno.h"
#include "list.h"
#include "basic.h"

n_int_t syscall_open(n_string_t *filename, n_int_t flags, n_u32_t perm) {
    DEBUGF("[syscall_open] filename: %s, %ld, %d\n", string_to_c_string_ref(filename), flags, perm);
    char *f_str = (char *) string_to_c_string_ref(filename);
    n_int_t fd = open(f_str, (int) flags, perm);
    if (fd == -1) {
        rt_processor_attach_errort(strerror(errno));
        return 0;
    }

    return fd;
}

n_list_t *syscall_read(n_int_t fd, n_int_t len) {
    // 创建一个 buf, 大小为 len
    uint8_t *buffer = malloc(len);

    ssize_t bytes = read((int) fd, buffer, len);
    if (bytes == -1) {
        rt_processor_attach_errort(strerror(errno));
        return NULL;
    }
    DEBUGF("[syscall_read] read %ld bytes", bytes);


    n_list_t *result = list_u8_new(bytes);

    for (int i = 0; i < bytes; ++i) {
        uint8_t b = buffer[i];
        list_push(result, &b);

        uint8_t temp;
        list_access(result, i, &temp);
        DEBUGF("[syscall_read] read index %d, except: %d, list_access: %d", i, b, temp)
    }

    DEBUGF("[syscall_read] result len: %lu, cap: %lu", result->length, result->capacity);
    free(buffer);
    return result;
}

/**
 * 返回实际写入的字节数
 * @param fd
 * @param buf
 * @return
 */
n_int_t syscall_write(n_int_t fd, n_list_t *buf) {
    DEBUGF("[syscall_write] fd: %ld, buf: %p", fd, buf)
    // Check if the buffer pointer is valid
    if (buf == NULL) {
        rt_processor_attach_errort("Buffer pointer is NULL");
        return -1;
    }

    // Check if the file descriptor is valid (greater than or equal to 0)
    if (fd < 0) {
        rt_processor_attach_errort("Invalid file descriptor");
        return -1;
    }


    // Write the data to the file
    ssize_t bytes_written = write((int) fd, buf->data, buf->length);
    if (bytes_written == -1) {
        rt_processor_attach_errort(strerror(errno));
        return -1;
    }

    // Return the actual number of bytes written
    return (n_int_t) bytes_written;
}

void syscall_close(n_int_t fd) {
    // Check if the file descriptor is valid (greater than or equal to 0)
    if (fd < 0) {
        rt_processor_attach_errort("Invalid file descriptor");
        return;
    }

    // Close the file
    int result = close((int) fd);

    if (result == -1) {
        rt_processor_attach_errort(strerror(errno));
        return;
    }
}

void syscall_unlink(n_string_t *path) {
    char *f_str = (char *) string_to_c_string_ref(path);

    int result = unlink(f_str);
    if (result == -1) {
        rt_processor_attach_errort(strerror(errno));
        return;
    }
}

n_int_t syscall_lseek(n_int_t fd, n_int_t offset, n_int_t whence) {
    off_t off = lseek((int) fd, offset, (int) whence);
    if (off == -1) {
        rt_processor_attach_errort(strerror(errno));
        return 0;
    }
   
    return off;
}
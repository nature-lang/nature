#include "syscall.h"
#include "fcntl.h"
#include "type/string.h"
#include "processor.h"
#include "errno.h"
#include "type/list.h"

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

    // 基于返回值创建一个 list
    rtype_t *list_rtype = gc_rtype(TYPE_LIST, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);

    n_list_t *result = list_new(list_rtype->hash, element_rtype->hash, bytes);


    for (int i = 0; i < bytes; ++i) {
        uint8_t b = buffer[i];
        list_push(result, &b);

        // 计算 offset
        uint64_t offset = element_rtype->size * i;
        uint8_t temp;
        list_access(result, i, &temp);
        DEBUGF("[syscall_read] read index %d, except: %d, list_access: %d", i, b, temp)
    }

    DEBUGF("[syscall_read] result len: %lu, cap: %lu", result->length, result->capacity);
    free(buffer);
    return result;
}

n_int_t syscall_write(n_int_t fd, n_list_t *buf) {
    return 0;
}

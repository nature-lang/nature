#include "string.h"
#include "array.h"

/**
 * na: string foo = "hello world"
 * 此时 foo 在 elf data section 中, raw_string 就是 section 中的一个地址
 * @param raw_string
 * @param length
 * @return
 */
memory_string_t *string_new(void *raw_string, uint64_t length) {
    // byte 数组，先手动创建一个简单类型
    typeuse_t element_type = type_basic_new(TYPE_BYTE);
    rtype_t element_rtype = reflect_type(element_type);
    memory_array_t *array_data = array_new(&element_rtype, length);

    // 创建 memory_string_t 类型，并转换成 rtype 进行 堆内存申请
    typeuse_t string_type = type_basic_new(TYPE_STRING);
    rtype_t string_rtype = reflect_type(string_type);
    memory_string_t *string_data = runtime_malloc(string_rtype.size, &string_rtype);

    string_data->length = length;
    string_data->array_data = array_data;
    memmove(string_data->array_data, raw_string, length);

    return string_data;
}

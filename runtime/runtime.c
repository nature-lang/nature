#include "runtime.h"

/**
 * ref 可能是栈上，数组中，全局变量中存储的 rtype 中的值
 * 需要感觉 rtype 存放的具体位置综合判断
 * @param rtype
 * @param ref
 * @return
 */
char *rtype_value_str(rtype_t *rtype, void *data_ref) {
    uint64_t data_size = rtype_heap_out_size(rtype);
    if (rtype->kind == TYPE_INT ||
        rtype->kind == TYPE_INT8) {
        int64_t temp = 0;
        memmove(&temp, data_ref, data_size);
        return itoa(temp);
    }

    if (rtype->index == TYPE_STRING) {
        memory_string_t *memory_string = (void *) fetch_addr_value((addr_t) data_ref); // 读取栈中存储的值
        char *str = runtime_malloc(memory_string->length + 1, NULL);
        str[memory_string->length] = '\0';

        memmove(str, memory_string->array_data, memory_string->length);
        return str;
    }

    assertf(false, "not support");

    return NULL;
}


extern void main();

/**
 * crt1.o _start -> main  -> user_main
 */
void runtime_main() {
    // - processor 初始化(包括当前执行栈记录,用户栈生成)
    processor_init();

    // - 堆内存管理初始化
    memory_init();

    // - 初始化 stack return addr 为 main
    processor_t *p = processor_get();

    // 切换到用户栈并执行目标函数(寄存器等旧数据会存到 p->system_mode)
    MODE_CALL(p->user_mode, p->system_mode, main);

    printf("user code run completed,will exit");
}

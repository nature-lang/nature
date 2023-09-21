#include "runtime.h"
#include "runtime/nutils/basic.h"

/**
 * ref 可能是栈上，数组中，全局变量中存储的 rtype 中的值
 * 需要感觉 rtype 存放的具体位置综合判断
 * @param rtype
 * @param ref
 * @return
 */
char *rtype_value_str(rtype_t *rtype, void *data_ref) {
    assertf(rtype, "rtype is null");
    assertf(data_ref, "data_ref is null");
    uint64_t data_size = rtype_out_size(rtype, POINTER_SIZE);

    DEBUGF("[rtype_value_str] rtype_kind=%s, data_ref=%p, data_size=%lu",
           type_kind_str[rtype->kind], data_ref, data_size);

    if (is_number(rtype->kind)) {
        assertf(data_size <= 8, "not support number size > 8, but %lu", data_size);
        int64_t temp = 0;
        memmove(&temp, data_ref, data_size);
        return itoa(temp);
    }

    if (rtype->kind == TYPE_STRING) {
        n_string_t *n_str = (void *) fetch_addr_value((addr_t) data_ref); // 读取栈中存储的值
        assertf(n_str && n_str->length > 0, "fetch addr by data ref '%p' err", data_ref);

//        return strdup(string_ref(n_str));
        // 进行 data copy, 避免被 free
        char *str = mallocz(n_str->length + 1);
        memmove(str, n_str->data, n_str->length);
        str[n_str->length] = '\0';
        return str;
    }

    assertf(false, "not support kind=%s", type_kind_str[rtype->kind]);

    return NULL;
}

// 这里直接引用了 main 符号进行调整，ct 不需要在寻找 main 对应到函数位置了
extern void main();

/**
 * crt1.o _start -> main  -> user_main
 */
void runtime_main(int argc, char *argv[]) {
    DEBUGF("[runtime_main] start, argc=%d, argv=%p", argc, argv);

    command_argc = argc;
    command_argv = argv;

    // - 堆内存管理初始化
    memory_init();
    DEBUGF("[runtime_main] memory init success")

    // - processor 初始化(包括当前执行栈记录,用户栈生成)
    DEBUGF("[runtime_main] rt_rtype_table base=%p", rt_rtype_table);
    processor_init();
    DEBUGF("[runtime_main] processor init success")

    // - 初始化 stack return addr 为 main
    processor_t *p = processor_get();

    DEBUGF("[runtime_main] current processor %p, will switch to user main call, user_stack=%lx", p,
           p->user_mode.stack_base);
    // 切换到用户栈并执行目标函数(寄存器等旧数据会存到 p->system_mode)
    MODE_CALL(p->user_mode, p->system_mode, main);

    // 检查错误
    DEBUGF("[runtime_main] has errort? %d", p->errort->has);
    if (p->errort->has) {
        processor_dump_errort(p->errort);
        exit(1);
    }

    DEBUGF("[runtime_main] user code run completed,will exit");
}

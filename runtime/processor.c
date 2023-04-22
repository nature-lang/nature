#include "processor.h"
#include "type/string.h"

/**
 * 从 current rsp 中截取大概 64KB 作为 process main 的 system_stack, 起点直接从 rsp 开始算
 */
static void processor_main_init(processor_t *p) {
    // 初始化系统栈与上下文
    mmode_t *mode = &p->system_mode;
    mode->stack_base = (addr_t) mode->ctx.uc_stack.ss_sp;
    mode->stack_size = (addr_t) mode->ctx.uc_stack.ss_size; // getcontext 给的 0

    mode = &p->user_mode;
    mode->stack_size = MSTACK_SIZE;
    mode->stack_base = (addr_t) sys_memory_map((void *) 0x4000000000, MSTACK_SIZE);
//    mode->ctx.uc_stack.ss_size = MSTACK_SIZE;
//    mode->ctx.uc_stack.ss_sp = (void *) mode->stack_base;

    // 临时栈
    mode = &p->temp_mode;
    mode->stack_size = MSTACK_SIZE;
    mode->stack_base = (addr_t) sys_memory_map((void *) 0x5000000000, MSTACK_SIZE);
//    mode->ctx.uc_stack.ss_size = MSTACK_SIZE;
//    mode->ctx.uc_stack.ss_sp = (void *) mode->stack_base;
}

void processor_init() {
    processor_list = mallocz(1 * sizeof(processor_t));
    processor_count = 1;
    processor_main_init(&processor_list[0]);
}

processor_t *processor_get() {
//    assertf(processor_count > 0, "processor not init");
    processor_t *result = &processor_list[0];
    return result;
}

// 基于字符串到快速设置不太需要考虑内存泄漏的问题， raw_string 都是 .data 段中的字符串
void processor_attach_errort(memory_struct_t *errort) {
    DEBUGF("[runtime.processor_attach_errort] errort base=%p", errort)
    processor_t *p = processor_get();
    p->errort = errort;
}

memory_struct_t *processor_remove_errort() {
    processor_t *p = processor_get();
    memory_struct_t *errort = p->errort; // 可能本身就为 null
    // 垃圾回收会清理 p->errort
    p->errort = NULL;

    DEBUGF("[runtime.processor_remove_errort] remove errort: %p", errort);
    return errort;
}

bool processor_has_errort() {
    processor_t *p = processor_get();
    DEBUGF("[runtime.processor_has_errort] errort?  %p", p->errort)

    return p->errort ? true : false;
}

void processor_dump_errort(memory_struct_t *errort) {
    DEBUGF("[runtime.processor_dump_errort] errort base=%p", errort)
    mock_errort *error = (mock_errort *) errort;
    memory_string_t *msg = error->msg;
    DEBUGF("[runtime.processor_dump_errort] memory_string len: %lu, base: %p",
           msg->length, msg->array_data);

    char *error_prefix = "runtime catch error: ";
    write(STDOUT_FILENO, error_prefix, strlen(error_prefix));
    write(STDOUT_FILENO, msg->array_data, msg->length);
}

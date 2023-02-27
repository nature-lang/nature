#include "collector.h"

#include "memory.h"

stack_t

static void scan_stack() {
    stack_t *stack = memory->user_stack;

    // 感觉 top 确定一下当前所在函数

    // 一直 return addr 在栈中的位置，现在需要取出其中存储的值
    addr_t return_addr = *stack->frame;
    // 这个 return_addr 是上一个
}

/**
 * TODO 在所有结构体都保存在堆的情况下来看，目前来看最大的对象就是 8bit?
 * @param symdef
 * @return
 */
static void scan_symbol(symdef_t symdef) {
    // symdef->addr 指向的是 data section, 该 section 不需要参与垃圾回收。
    // 如果 data section 中存储的是指针数据，则将该指针加入到 grep list 中
}

/**
 * 1. mark gcroot，虚拟栈扫描找到所有的 ptr, 全局对象扫描找到所有 ptr 类型的全局对象(依赖 linker 中的 symtab 数据，
 *    所以该数据需要传递到运行时),(扫描的 obj 都包含在 mcache 和 mcentral 的 span 中，一个 span 要么在 mcache, 要
 *    么在 mcentral), 不会同时存在。root obj 是不需要参与到三色标记的，所以不需要加入到全局队列中
 *
 * 1.1. 每扫描到一个 obj，应该按三色标记将其放到 gc 队列中，从 gc 队列中取出的 obj,需要判断该 obj 是否有 sub obj,
 *    根据 heap arena bit 判断是否有指针，如果有子对象则全部置灰(放到队列中)，然后将其对应的 mspan gcmark bits 标记为1(黑色)。
 *    当 gc 队列为空时，表示所有的 obj 已经标记完成。可以进行 sweep。
 *
 * 2. sweep 本质上是对 mcentral 中的 mspan 队列扫描的过程，此时需要开启 STW,将所有 mcache 中的 span flush 到 mcentral 中，
 *    然后对 mcentral 中的 span 根据 gcmark bits 进行清扫。
 *
 * @return
 */
void *runtime_gc() {

    // 0. STW(在多线程 or 协程没有设计出来之前，目前先以单线程的模式处理，单线程就不需要考虑阻塞的问题了)

    // 1. 切换到 system stack
    system_stack();

    // 2. 遍历 gc roots
    // get roots 是一组 ptr, 需要简单识别一下，如果是 root ptr, 其虽然能够进入到 grey list, 但是离开 grey list 时不需要标灰
    // - 遍历 global symbol list,如果是 ptr 则将 ptr 指向的内存块进行分析，然后加入到 grey 队列
    for (int i = 0; i < symdef_count; ++i) {
        symdef_t s = symdef_list[i];
        if (s.last_ptr_count == 0) {
            continue;
        }
        // 指针数据,需要根据其 gc data 进行对象解析,解析到的 ptr 对象丢到 grey list 中
        scan_symbol(s);
    }
    // - 遍历 user stack
    scan_stack();



    // 3. handle grey list until empty


    // 4. flush mcache to mcentral

    // 5. sweep all span

    // 6. user stack
}
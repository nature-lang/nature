#include "collector.h"

#include "memory.h"
#include "allocator.h"
#include "processor.h"

static fndef_t find_fn(addr_t addr) {}

static void scan_stack(memory_t *m) {
    processor_t current = processor_get();
    stack_t stack = current.user_stack;

    // 根据 top 确定一下当前所在函数(由于进入到 runtime, 所以 top 可能同样进入到了 runtime fn)
    // 所以第一个需要清理的 fn 应该是 frame 位置对应的 位置是 previous rbp, 再往上一个位置就是目标的位置
    addr_t frame_base = stack.frame_base;
    while (true) {
        addr_t return_addr = (addr_t) fetch_addr_value(frame_base + PTR_SIZE);
        fndef_t fn = find_fn(return_addr);
        uint64_t stack_size = fn.stack_offset;

        // PTR_SIZE * 2 表示跳过 previous rbp 和 return addr
        // 由于栈向下增长，所以此处 top 小于 base, 且取值则是想上增长
        addr_t frame_top = frame_base + PTR_SIZE * 2;
        frame_base = frame_top + fn.stack_offset;

        // 根据 gc data 判断栈内存中存储的值是否为 ptr, 如果是的话，该 ptr 指向的对象必定是 heap。
        // 栈内存本身的数据属于 root obj, 不需要参与三色标记, 首先按 8byte 遍历整个 free
        addr_t cursor = frame_top;
        int i = 0;
        while (cursor < frame_base) {
            bool is_ptr = bitmap_test(fn.gc_bits, i);
            if (is_ptr) {
                // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
                list_push(m->grey_list, fetch_addr_value(cursor));
            }

            i += 1;
            cursor += 8;
        }

        // 已经到达了栈底
        if (frame_base == 0) {
            break;
        }
    }
}

static void scan_symbols(memory_t *m) {
    for (int i = 0; i < symdef_count; ++i) {
        symdef_t s = symdef_list[i];
        if (s.last_ptr_count == 0) {
            continue;
        }
        // s 本身是存储在 .data section 的一段数据，但是其存储在 data 在数据是一个 heap ptr，该 ptr 需要加入到 grep list 中
        // 基于 s 对应的 gc bits 按 8bit 判断
        addr_t cursor = s.addr;
        i = 0;
        while (cursor < (s.addr + s.size)) {
            bool is_ptr = bitmap_test(s.gc_bits, i);
            if (is_ptr) {
                list_push(m->grey_list, fetch_addr_value(cursor));
            }

            cursor += PTR_SIZE;
            i += 1;
        }
    }
}

static void grey_list_work(memory_t *m) {
    list *temp_grep_list = list_new();
    while (m->grey_list->count > 0) {
        // 1. traverse all ptr
        // - pop ptr
        // - get mspan by ptr
        // - get size by mspan
        // - search ptr ~ ptr+size sub ptrs by heap bits then push to temp grep list
        // - The gc bits corresponding to obj are marked 1

        // 2. grey_list replace with temp_grep_list
    }
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
    // 获取当前线程, 其中保存了当前线程使用的虚拟栈
    processor_t current = processor_get();

    // 0. STW
    // 1. 切换到 system stack
    system_stack(current);

    // 2. 遍历 gc roots
    // get roots 是一组 ptr, 需要简单识别一下，如果是 root ptr, 其虽然能够进入到 grey list, 但是离开 grey list 时不需要标灰
    // - 初始化 gc 状态
    assertf(list_is_empty(memory->grey_list), "grey list not cleanup");

    // - 遍历 global symbol list,如果是 ptr 则将 ptr 指向的内存块进行分析，然后加入到 grey 队列
    scan_symbols(memory);
    // - 遍历 user stack
    scan_stack(memory);


    // 3. handle grey list until empty, all mspan gc_bits marked
    grey_list_work(memory);

    // 4. flush mcache to mcentral
    flush_mcache();

    // 5. sweep all span (iterate mcentral list)
    sweep_mcentral(&memory->mheap);

    // 6. 切换回 user stack TODO 切换回哪个 user stack
    user_stack(current);
}
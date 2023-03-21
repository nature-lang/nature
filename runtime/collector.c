#include "collector.h"

#include "memory.h"
#include "allocator.h"
#include "processor.h"

/**
 * addr 是 .text 中的地址
 * @param addr
 * @return
 */
static fndef_t *find_fn(addr_t addr) {
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *fn = &rt_fndef_data[i];
        if (fn->base < addr && fn->end > addr) {
            return fn;
        }
    }
    return NULL;
}

static addr_t fetch_addr_value(addr_t addr) {
    // addr 中存储的依旧是 addr，现在需要取出 addr 中存储的值
    addr_t *p = (addr_t *) addr;
    return *p;
}

static void scan_stack(memory_t *m) {
    processor_t current = processor_get();
    mstack_t stack = current.user_stack;

    // 根据 top 确定一下当前所在函数(由于进入到 runtime, 所以 top 可能同样进入到了 runtime fn)
    // 所以第一个需要清理的 fn 应该是 frame 位置对应的 位置是 previous rbp, 再往上一个位置就是目标的位置
    addr_t frame_base = stack.frame_base;
    while (true) {
        addr_t return_addr = (addr_t) fetch_addr_value(frame_base + POINTER_SIZE);
        fndef_t *fn = find_fn(return_addr);

        // PTR_SIZE * 2 表示跳过 previous rbp 和 return addr
        // 由于栈向下增长，所以此处 top 小于 base, 且取值则是想上增长
        addr_t frame_top = frame_base + POINTER_SIZE * 2;
        frame_base = frame_top + fn->stack_size;

        // 根据 gc data 判断栈内存中存储的值是否为 ptr, 如果是的话，该 ptr 指向的对象必定是 heap。
        // 栈内存本身的数据属于 root obj, 不需要参与三色标记, 首先按 8byte 遍历整个 free
        addr_t cursor = frame_top;
        int i = 0;
        while (cursor < frame_base) {
            bool is_ptr = bitmap_test(fn->gc_bits, i);
            if (is_ptr) {
                // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
                linked_push(m->grey_list, (void *) fetch_addr_value(cursor));
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

static void scan_symdefs(memory_t *m) {
    uint64_t count = rt_symdef_size / sizeof(symdef_t);
    for (int i = 0; i < count; ++i) {
        symdef_t s = rt_symdef_data[i];
        if (!s.need_gc) {
            continue;
        }
        // TODO 如果 size 超过了 8byte？就不兼容了, 虽然目前不会超过 8byte
        assertf(s.size <= 8, "temp do not support symbol size > 8byte");
        // s.base 是 data 段中的地址， fetch_addr_value 则是取出该地址中存储的数据
        linked_push(m->grey_list, (void *) fetch_addr_value(s.base));
    }
}

/**
 * @param mheap
 * 遍历 processor_list 将所有的 mcache 中持有的 mspan 都 push 到对应的 mcentral 中
 */
static void flush_mcache() {
    for (int i = 0; i < processor_count; ++i) {
        processor_t p = processor_list[i];
        for (int j = 0; j < SPANCLASS_COUNT; ++j) {
            mspan_t *span = p.mcache.alloc[j];
            p.mcache.alloc[j] = 0;
            mcentral_t mcentral = memory->mheap->centrals[span->spanclass];
            uncache_span(mcentral, span);
        }
    }
    DEBUGF("gc flush mcache successful")
}


static void mark_obj_black(mspan_t *span, int index) {
    bitmap_set(span->gcmark_bits->bits, index);
}

/**
 * grey list 一旦处理完成，就表明所有正在使用的对象都已经变成了黑色
 * 此时所有的白色对象就是垃圾对象，需要被清理
 * @param m
 */
static void grey_list_work(memory_t *m) {
    linked_t *temp_grey_list = linked_new();
    uint64_t obj_count = 0;
    while (m->grey_list->count > 0) {
        // 1. traverse all ptr

        LINKED_FOR(temp_grey_list) {
            obj_count++;
            // - pop ptr, 该 ptr 是堆中的内存，首先找到其 mspan, 确定其大小以及
            addr_t addr = (addr_t) LINKED_VALUE();
            arena_t *arena = memory->mheap->arenas[arena_index(addr)];

            // get mspan by ptr
            mspan_t *span = span_of(addr);
            //  get span index
            uint obj_index = (addr - span->base) / span->obj_size;

            // 判断当前 span obj 是否已经被 gc bits mark,如果已经 mark 则不需要重复扫描
            if (bitmap_test(span->gcmark_bits->bits, obj_index)) {
                // already marks black
                continue;
            }

            // - black: The gc bits corresponding to obj are marked 1
            mark_obj_black(span, obj_index);

            // 判断 span 是否需要扫描(如果标量的话就不需要扫描直接标记即可)
            if (!spanclass_has_ptr(span->spanclass)) {
                // addr ~ addr+size 空间内存储的是一个标量，不需要向下扫描了
                continue;
            }

            // - obj 中包含指针，需要进一步扫描
            // - locate to the arena bits start
            uint8_t *bits_base = &arena->bits[(addr - arena->base) / (4 * POINTER_SIZE)];

            // scan object
            // - search ptr ~ ptr+size sub ptrs by heap bits then push to temp grep list
            // ++i 此时按指针跨度增加
            int index = 0;
            for (addr_t temp_addr = addr; temp_addr < addr + span->obj_size; temp_addr += POINTER_SIZE) {
                int bit_index = (index / 4) * 8 + (index % 4);
                index++;
                // TODO arena_t bits 高地位标记了后续是否还有更多的指针，如果没有了，就可以直接停止了
                bool is_ptr = bitmap_test(bits_base, bit_index);
                if (is_ptr) {
                    // 如果是 ptr 则将其加入到 grey list 中
                    linked_push(temp_grey_list, (void *) temp_addr);
                }
            }

        }

        // 2. grey_list replace with temp_grep_list
        m->grey_list = temp_grey_list;
    }
    DEBUGF("gc grey list scan successful, scan obj count=%d", obj_count)
}

/**
 * 如果 span 清理完成后 alloc_count == 0 则将其归还给 heap
 * @param span span 是否被释放，如果释放了需要将其从 list 中抹除
 */
static bool sweep_span(mcentral_t *central, mspan_t *span) {
    bitmap_free(span->alloc_bits);
    span->alloc_bits = span->gcmark_bits;
    span->gcmark_bits = bitmap_new(span->obj_count);

    // 重新计算 alloc_count
    span->alloc_count = bitmap_set_count(span->alloc_bits);

    if (span->alloc_count == 0) {
        mheap_free_span(&memory->mheap, span);
    } else if (span->alloc_count == span->obj_count) {
        // full used
        linked_push(central->full_swept, span);
    } else {
        linked_push(central->partial_swept, span);
    }
}

static void free_mspan_meta(mspan_t *span) {
    bitmap_free(span->alloc_bits);
    bitmap_free(span->gcmark_bits);
    free(span);
}

/**
 *
 *
 * 遍历 所有 mcentral 的 full 和 partial 进行清理
 * - 只有是被 span 持有的 page， 在 page_alloc 眼里就是被分配了出去，所以不需要对 chunk 进行修改什么的
 * - 并不需要真的清理 obj, 只需要将 gc_bits 和 alloc_bits 调换一下位置，然后从新计算 alloc_count 即可
 * - 当 gc 完成后 alloc_count = 0, 就需要考虑是否需要将该 span 归还到 mheap 中了
 * - sweep 时 arena_t 的 bits 是否需要更新？
 *   空闲的 obj 进行 alloc 时一定会进行 set bits, 所以所有忙碌的 obj 的 bits 一定是有效的。
 *   空闲的 obj 的 bits 即使是脏的，三色标记时也一定无法标记到该 obj, 因为其不在引用链中
 * @param mheap
 */
void mcentral_sweep(mheap_t *mheap) {
    mcentral_t *centrals = mheap->centrals;
    for (int i = 0; i < SPANCLASS_COUNT; ++i) {
        mcentral_t *central = &mheap->centrals[i];
        // 遍历 list 中的所有 span 进行清理, 如果 span 已经清理干净则其规划到 mehap 中
        LINKED_FOR(central->partial_swept) {
            mspan_t *span = LINKED_VALUE();
            if (sweep_span(central, span)) {
                linked_remove(central->partial_swept, LINKED_NODE());
                free_mspan_meta(span);
            }
        }

        LINKED_FOR(central->full_swept) {
            mspan_t *span = LINKED_VALUE();
            if (sweep_span(central, span)) {
                linked_remove(central->full_swept, LINKED_NODE());
                free_mspan_meta(span);
            }
        }
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
void runtime_gc() {
    // 获取当前线程, 其中保存了当前线程使用的虚拟栈
    processor_t current = processor_get();

    // 0. STW
    // 1. 切换到 system stack
    system_stack(current);

    // 2. 遍历 gc roots
    // get roots 是一组 ptr, 需要简单识别一下，如果是 root ptr, 其虽然能够进入到 grey list, 但是离开 grey list 时不需要标灰
    // - 初始化 gc 状态
    assertf(linked_empty(memory->grey_list), "grey list not cleanup");

    // - 遍历 global symbol list,如果是 ptr 则将 ptr 指向的内存块进行分析，然后加入到 grey 队列
    scan_symdefs(memory);
    // - 遍历 user stack
    scan_stack(memory);

    // 3. handle grey list until empty, all mspan gc_bits marked
    grey_list_work(memory);

    // 4. flush mcache to mcentral
    flush_mcache();

    // 5. sweep all span (iterate mcentral list)
    mcentral_sweep(&memory->mheap);

    // 6. 切换回 user stack
    user_stack(current);
}
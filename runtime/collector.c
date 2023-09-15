#include "memory.h"
#include "processor.h"

/**
 * TODO current is AMD64
 * amd64 下，5 表示 rbp 寄存器的值，这里表示最后一次切换后，rbp 寄存器中的值
 * @param mode
 * @return
 */
static addr_t extract_frame_base(mmode_t mode) {
    return mode.ctx.uc_mcontext.gregs[10];
}

/**
 * addr 是 .text 中的地址
 * @param addr
 * @return
 */
fndef_t *find_fn(addr_t addr) {
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *fn = &rt_fndef_ptr[i];
        if (fn->base <= addr && addr < (fn->base + fn->size)) {
            return fn;
        }
    }
    return NULL;
}

/**
 * runtime 中也申请了一些内存，为了避免被清理，需要将其加入到 root grey list 中
 * @param m
 */
static void scan_runtime(memory_t *m) {
    processor_t *p = processor_get();
    assertf(in_heap((addr_t) p->errort), "[scan_runtime] processor.errort not in heap");
    DEBUGF("[runtime_gc.scan_runtime] add errort=%p to grey list", p->errort);
    linked_push(m->grey_list, p->errort);
}

static void scan_stack(memory_t *m) {
    processor_t *p = processor_get();
    mmode_t mode = p->user_mode;

    // 根据 top 确定一下当前所在函数(由于进入到 runtime, 所以 top 可能同样进入到了 runtime fn)
    // 所以第一个需要清理的 fn 应该是 frame 位置对应的 位置是 previous rbp, 再往上一个位置就是目标的位置

    addr_t frame_base = extract_frame_base(mode); // 从大向小增长。
    uint64_t stack_top = mode.stack_base + mode.stack_size;

    DEBUGF("[runtime.scan_stack] frame_base(BP)=0x%lx, stack_top(max)=0x%lx, stack_base(min)=0x%lx", frame_base,
           stack_top, mode.stack_base);
    assertf(frame_base >= mode.stack_base && frame_base < stack_top, "stack overflow");

    DEBUGF("[runtime_gc.scan_stack] start, base=0x%lx, stack_top=0x%lx, frame_base=0x%lx",
           mode.stack_base,
           stack_top,
           frame_base);

    assert(frame_base % 8 == 0);
    while (frame_base + POINTER_SIZE < stack_top) {
        addr_t return_addr = (addr_t) fetch_addr_value(frame_base + POINTER_SIZE);
        fndef_t *fn = find_fn(return_addr);
        if (!fn) {
            DEBUGF("[runtime_gc.scan_stack] fn not found by return addr, frame_base=0x%lx, return_addr=0x%lx, will + 8byte test next",
                   frame_base,
                   return_addr);
            frame_base += POINTER_SIZE;
            continue;
        } else {
            DEBUGF("[runtime_gc.scan_stack] fn found by return addr, frame_base=0x%lx, return_addr=0x%lx, fn_name=%s",
                   frame_base,
                   return_addr,
                   fn->name);
        }

        // PTR_SIZE * 2 表示跳过 previous rbp 和 return addr
        // 由于栈向下增长，所以此处 top 小于 base, 且取值则是向上增加
        addr_t frame_top = frame_base + POINTER_SIZE * 2; // frame_base -> prev frame_base ->  return_addr > stack
        frame_base = frame_top + fn->stack_size; // frame_base

        DEBUGF("[runtime_gc.scan_stack] fn_name=%s, fn=0x%lx, stack_size=%lu, fn_size=%lu, gc_bits=%s, "
               "(min)frame_top=0x%lx,(big)frame_base=0x%lx",
               fn->name,
               fn->base,
               fn->stack_size,
               fn->size,
               bitmap_to_str(fn->gc_bits, fn->stack_size / POINTER_SIZE),
               frame_top,
               frame_base);

        // 根据 gc data 判断栈内存中存储的值是否为 ptr, 如果是的话，该 ptr 指向的对象必定是 heap。
        // 栈内存本身的数据属于 root obj, 不需要参与三色标记, 首先按 8byte 遍历整个 free
        // frame_base = rbp-0. bit=0 存储的是(rbp-8 ~ rbp-0) 处的值
        addr_t cursor = frame_base - POINTER_SIZE;
        int i = 0;
        while (cursor >= frame_top) {
            bool is_ptr = bitmap_test(fn->gc_bits, i);
            DEBUGF("[runtime_gc.scan_stack] fn_name=%s, fn_gc_bits i=%d, cursor_stack_addr=0x%lx, is_ptr=%d ,stack_value(to_int64)=0x%lx",
                   fn->name,
                   i, cursor, is_ptr,
                   fetch_int_value(cursor, 8))
            if (is_ptr) {
                // var 即使 spill 了, 但是可能由于还没有运行到函数结尾，所以可能此时这里是空值或者无效值
                // 错误也没关系，只是引用了导致不会清空，但是至少不会错误清理
                addr_t addr = fetch_addr_value(cursor);
                if (in_heap(addr)) {
                    // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
                    linked_push(m->grey_list, (void *) addr);
                } else {
//                    DEBUGF("[runtime_gc.scan_stack] fn_name=%s, fn_gc_bits i=%d, addr=%p not heap addr, will skip",
//                           fn->name,
//                           i, (void *) addr)
                }
            }

            i += 1;
            cursor -= POINTER_SIZE;
        }
    }

    DEBUGF("[runtime_gc.scan_stack] completed, frame_base=0x%lx", frame_base);
}

static void scan_symdefs(memory_t *m) {
    DEBUGF("[runtime_gc.scan_symdefs] start")

    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        if (!s.need_gc) {
            continue;
        }

        DEBUGF("[runtime.scan_symdefs] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx",
               s.name, s.base, s.size, s.need_gc, fetch_int_value(s.base, s.size));

        assertf(s.size <= 8, "temp do not support symbol size > 8byte");
        assertf(s.base > 0, "s.base is zero,cannot fetch value by base");
        // 触发 gc 时全局变量可能还没有进行初始化, 所以这里使用 in_heap 进行一下地址可用对判断
        addr_t addr = fetch_addr_value(s.base);
        if (in_heap(addr)) {
            // s.base 是 data 段中的地址， fetch_addr_value 则是取出该地址中存储的数据
            // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
            linked_push(m->grey_list, (void *) addr);
        }
    }
}

/**
 * @param mheap
 * 遍历 processor_list 将所有的 mcache 中持有的 mspan 都 push 到对应的 mcentral 中
 */
static void flush_mcache() {
    for (int i = 0; i < processor_count; ++i) {
        processor_t *p = &processor_list[i];
        for (int j = 0; j < SPANCLASS_COUNT; ++j) {
            mspan_t *span = p->mcache.alloc[j];
            if (!span) {
                continue;
            }
            p->mcache.alloc[j] = NULL;
            mcentral_t *mcentral = &memory->mheap->centrals[span->spanclass];
            uncache_span(mcentral, span);
        }
    }
    DEBUGF("gc flush mcache successful")
}


static void mark_obj_black(mspan_t *span, uint64_t index) {
    bitmap_set(span->gcmark_bits->bits, index);
}

/**
 * grey list 一旦处理完成，就表明所有正在使用的对象都已经变成了黑色
 * 此时所有的白色对象就是垃圾对象，需要被清理
 * @param m
 */
static void grey_list_work(memory_t *m) {
    uint64_t obj_count = 0;
    while (!linked_empty(m->grey_list)) {
        // 1. traverse all ptr
        linked_t *temp_grey_list = linked_new();
        LINKED_FOR(m->grey_list) {
            obj_count++;
            // - pop ptr, 该 ptr 是堆中的内存，首先找到其 mspan, 确定其大小以及
            addr_t addr = (addr_t) LINKED_VALUE();

            // get mspan by ptr
            mspan_t *span = span_of(addr);

            //  get span index
            uint64_t obj_index = (addr - span->base) / span->obj_size;

            // 如果 addr 不是 span obj 的起始地点，也就是需要和 obj_size 前向对齐
            // 计算 addr 所在的 obj 的起始地址
            addr = span->base + (obj_index * span->obj_size);

            DEBUGF("[runtime_gc.grey_list_work] addr=0x%lx, spanclass_has_ptr=%d, span=0x%lx, "
                   "spanclass=%d, obj_index=%lu, span->obj_size=%lu",
                   addr, spanclass_has_ptr(span->spanclass), span->base, span->spanclass, obj_index, span->obj_size);

            // 判断当前 span obj 是否已经被 gc bits mark,如果已经 mark 则不需要重复扫描
            if (bitmap_test(span->gcmark_bits->bits, obj_index)) {
                // already marks black
                DEBUGF("[runtime_gc.grey_list_work] addr=0x%lx, span=0x%lx, obj_index=%lu marked, will continue",
                       addr, span->base, obj_index);
                continue;
            }


            // - black: The gc bits corresponding to obj are marked 1
            mark_obj_black(span, obj_index);


            // 判断 span 是否需要进一步扫描, 可以根据 obj 所在的 spanclass 直接判断 (如果标量的话, 直接标记就行了，不需要进一步扫描)
            if (!spanclass_has_ptr(span->spanclass)) {
                // addr ~ addr+size 空间内存储的是一个标量，不需要向下扫描了
                continue;
            }

            // scan object
            // - search ptr ~ ptr+size sub ptrs by heap bits then push to temp grep list
            // ++i 此时按指针跨度增加
            int index = 0;
            for (addr_t temp_addr = addr; temp_addr < addr + span->obj_size; temp_addr += POINTER_SIZE) {
                arena_t *arena = take_arena(addr);
                uint64_t bit_index = arena_bits_index(arena, temp_addr);
                index++;
                bool is_ptr = bitmap_test(arena->bits, bit_index);
                if (is_ptr) {
                    // 同理，即使某个 ptr 需要 gc, 但是也可能存在 gc 时，还没有赋值的清空
                    addr_t heap_addr = fetch_addr_value(temp_addr);

                    DEBUGF("[runtime_gc.grey_list_work] addr is ptr,scan_base=0x%lx cursor_addr=0x%lx fetch_cursor_value=0x%lx,"
                           "obj_size=%ld, bit_index=%lu, in_heap=%d",
                           addr, temp_addr, heap_addr, span->obj_size, bit_index, in_heap(heap_addr));
                    if (in_heap(heap_addr)) {
                        linked_push(temp_grey_list, (void *) heap_addr);
                    }
                }
            }

        }

        // 2. grey_list replace with temp_grep_list
        linked_free(m->grey_list);
        m->grey_list = temp_grey_list;
    }
    DEBUGF("gc grey list scan successful, scan obj count=%lu", obj_count)
}

static void free_mspan_meta(mspan_t *span) {
    bitmap_free(span->alloc_bits);
    bitmap_free(span->gcmark_bits);
    free(span);
}

/**
 * 如果 span 清理完成后 alloc_count == 0 则将其归还给 heap
 * @param span span 是否被释放，如果释放了需要将其从 list 中抹除
 */
static void sweep_span(linked_t *full, linked_t *partial, mspan_t *span) {
    // 但是此时 span 其实并没有真的被释放,只有 alloc_count = 0 时才会触发真正的释放操作
    for (int i = 0; i < span->obj_count; ++i) {
        // 如果 gcmark_bits = 0, alloc_bits = 1, 则表明内存被释放，可以进行释放的
        if (bitmap_test(span->alloc_bits->bits, i) && !bitmap_test(span->gcmark_bits->bits, i)) {
            // 内存回收(未返回到堆)
            allocated_bytes -= span->obj_size;

#ifdef DEBUG
            DEBUGF("[runtime.sweep_span] success, span->class=%d, span->base=0x%lx, span->obj_size=%ld, obj_addr=0x%lx, allocator_bytes=%ld",
                   span->spanclass, span->base, span->obj_size, span->base + i * span->obj_size, allocated_bytes);

            // 将对应位置的内存设置为 0
            memset((void *) (span->base + i * span->obj_size), 0, span->obj_size);
#endif

        }
    }


    bitmap_free(span->alloc_bits);
    span->alloc_bits = span->gcmark_bits;
    span->gcmark_bits = bitmap_new(span->obj_count); // 清空

    // 重新计算 alloc_count
    span->alloc_count = bitmap_set_count(span->alloc_bits);

    if (span->alloc_count == 0) {
        DEBUGF("span free to heap, base=0x%lx, class=%d", span->base, span->spanclass);
        mheap_free_span(memory->mheap, span);
        free_mspan_meta(span);
        return;
    }

    if (span->alloc_count == span->obj_count) {
        linked_push(full, span);
    } else {
        linked_push(partial, span);
    }
}

/**
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
        mcentral_t *central = &centrals[i];
        if (linked_empty(central->partial_swept) && linked_empty(central->full_swept)) {
            continue;
        }

        // new full and partial
        linked_t *full_new = linked_new();
        linked_t *partial_new = linked_new();

        // 经过 sweep full -> part，或者直接清零规划给 mheap, 但是绝对不会从 part 到 full
        LINKED_FOR(central->full_swept) {
            mspan_t *span = LINKED_VALUE();
            sweep_span(full_new, partial_new, span);
        }

        // 遍历 list 中的所有 span 进行清理, 如果 span 已经清理干净则其规划到 mehap 中
        LINKED_FOR(central->partial_swept) {
            mspan_t *span = LINKED_VALUE();
            sweep_span(full_new, partial_new, span);
        }

        linked_free(central->full_swept);
        linked_free(central->partial_swept);
        central->full_swept = full_new;
        central->partial_swept = partial_new;
    }
}

/**
 * @stack system
 */
static void _runtime_gc() {
    DEBUGF("[_runtime_gc] stack switched, current is temp mode");
    DEBUG_STACK();
    processor_t *_p = processor_get();

    // 遍历 gc roots
    // get roots 是一组 ptr, 需要简单识别一下，如果是 root ptr, 其虽然能够进入到 grey list, 但是离开 grey list 时不需要标灰
    // - 初始化 gc 状态
    assertf(linked_empty(memory->grey_list), "grey list not cleanup");

    // - 遍历 global symbol list,如果是 ptr 则将 ptr 指向的内存块进行分析，然后加入到 grey 队列
    scan_symdefs(memory);

    // - 遍历 user stack
    scan_stack(memory);

    // - runtime grey
    scan_runtime(memory);

    // 3. handle grey list until empty, all mspan gc_bits marked
    grey_list_work(memory);

    // 4. flush mcache to mcentral
    flush_mcache();

    // 5. sweep all span (iterate mcentral list)
    mcentral_sweep(memory->mheap);
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
 * @return
 */
void __attribute__ ((optimize(0))) runtime_gc() {
    DEBUGF("[runtime_gc] start")
    DEBUG_STACK();
    // 获取当前线程, 其中保存了当前线程使用的虚拟栈
    processor_t *p = processor_get();
    MODE_CALL(p->temp_mode, p->user_mode, _runtime_gc)
    DEBUGF("[runtime_gc] completed")
}

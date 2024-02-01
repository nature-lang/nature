#include "fixalloc.h"
#include "gcbits.h"
#include "memory.h"
#include "processor.h"

static void insert_gc_worklist(rt_linked_t *gc_worklist, void *ptr) {
    assert(span_of((addr_t)ptr) && "ptr not found in active span");
    rt_linked_push(gc_worklist, ptr);
}

/**
 * grey 的本质就是等待处理，此时需要将 obj 放到 grey list 中
 * span gcmark_bits + grep_list 共同组成了三种颜色
 * 白色: gc_mark_bits 为 0
 * 黑色: 不在 grey_list 里面，并且 gc_mark_bits 为 1
 * 灰色: 在 grey_list 中, gc_mark_bits 为 0
 *
 * 处理 obj 之前会检测 gcmark_bits 是否标记，所以 grey 之前必须将 gcmark_bits 标记为 0
 * @param obj
 */
void shade_obj_grey(void *obj) {
    addr_t addr = (addr_t)obj;
    // 不在堆内存中
    if (!in_heap(addr)) {
        DEBUGF("[runtime_gc.shade_obj_grey] addr=%p not in heap", obj);
        return;
    }

    // get mspan by ptr
    mspan_t *span = span_of(addr);

    // get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;

    mutex_lock(&span->gcmark_locker);
    bitmap_clear(span->gcmark_bits, obj_index);
    mutex_unlock(&span->gcmark_locker);

    processor_t *p = processor_get();

    // TODO 如果当前 processor 的 gc_work 已经完成，则需要将 obj 插入到全局工作队列中
    assert(p->gc_work_finished < memory->gc_count && "gc work finished, cannot insert to gc worklist");

    insert_gc_worklist(&p->gc_worklist, obj);
}

/**
 * addr 是 .text 中的地址
 * @param addr
 * @return
 */
fndef_t *find_fn(addr_t addr) {
    if (addr <= 0) {
        return NULL;
    }
    for (int i = 0; i < rt_fndef_count; ++i) {
        fndef_t *fn = &rt_fndef_ptr[i];
        assert(fn);

        if (fn->base <= addr && addr < (fn->base + fn->size)) {
            return fn;
        }
    }

    return NULL;
}

/**
 * @param mheap
 * 遍历 processor_list 将所有的 mcache 中持有的 mspan 都 push 到对应的 mcentral 中
 */
static void flush_mcache() {
    PROCESSOR_FOR(share_processor_list) {
        for (int j = 0; j < SPANCLASS_COUNT; ++j) {
            mspan_t *span = p->mcache.alloc[j];
            if (!span) {
                continue;
            }

            p->mcache.alloc[j] = NULL; // uncache
            mcentral_t *mcentral = &memory->mheap->centrals[span->spanclass];
            uncache_span(mcentral, span);
        }
    }

    mutex_lock(&solo_processor_locker);
    PROCESSOR_FOR(solo_processor_list) {
        for (int j = 0; j < SPANCLASS_COUNT; ++j) {
            mspan_t *span = p->mcache.alloc[j];
            if (!span) {
                continue;
            }
            p->mcache.alloc[j] = NULL; // uncache
            mcentral_t *mcentral = &memory->mheap->centrals[span->spanclass];
            uncache_span(mcentral, span);
        }
    }
    mutex_unlock(&solo_processor_locker);

    DEBUGF("gc flush mcache successful");
}

void mark_obj_black_with_lock(mspan_t *span, uint64_t index) {
    mutex_lock(&span->gcmark_locker);
    bitmap_set(span->gcmark_bits, index);
    mutex_unlock(&span->gcmark_locker);
}

// safe
static void free_mspan_meta(mspan_t *span) {
    span->next = NULL;
    span->base = 0;
    mutex_lock(memory->locker);
    fixalloc_free(&memory->mheap->spanalloc, span);
    mutex_unlock(memory->locker);
}

/**
 * 如果 span 清理完成后 alloc_count == 0 则将其归还给 heap
 * @param span span 是否被释放，如果释放了需要将其从 list 中抹除
 */
static bool sweep_span(mcentral_t *central, mspan_t *span) {
    // 但是此时 span 其实并没有真的被释放,只有 alloc_count = 0 时才会触发真正的释放操作, 这里记录更新一下分配的内存值
    assert(span);
    assert(span->obj_count > 0 && span->obj_count <= 1024);
    assert(span->base > 0);

    RDEBUGF("[sweep_span] start, span=%p, span_base=%p, spc=%d", span, (void *)span->base, span->spanclass)
    int alloc_count = 0;
    for (int i = 0; i < span->obj_count; ++i) {
        if (bitmap_test(span->gcmark_bits, i)) {
            alloc_count++;
            continue;
        }

        // 如果 gcmark_bits = 0, alloc_bits = 1, 则表明内存被释放，可以进行释放的
        if (bitmap_test(span->alloc_bits, i) && !bitmap_test(span->gcmark_bits, i)) {
            // 内存回收(未返回到堆)
            allocated_bytes -= span->obj_size;

            DEBUGF("[sweep_span] will seep, spc=%d, base=%p, obj_size=%ld, obj_addr=0x%lx, allocator_bytes=%ld", span->spanclass,
                   (void *)span->base, span->obj_size, span->base + i * span->obj_size, allocated_bytes);
        }
    }

    RDEBUGF("[sweep_span] alloc_count=%d, obj_count=%lu, span=%p, span_base=%p, spc=%d", alloc_count, span->obj_count, span,
            (void *)span->base, span->spanclass)
    span->alloc_bits = span->gcmark_bits;
    span->gcmark_bits = gcbits_new(span->obj_count);
    span->alloc_count = alloc_count;

    RDEBUGF("[sweep_span] reset gcmark_bits success, span=%p, spc=%d", span, span->spanclass)

    // span 所有的 obj 都被释放，归还内存给操作系统
    if (span->alloc_count == 0) {
        RDEBUGF("[sweep_span] span will free to heap, span=%p, base=0x%lx, class=%d", span, span->base, span->spanclass);
        mheap_free_span(memory->mheap, span);
        RDEBUGF("[sweep_span] span success free to heap, span=%p, base=0x%lx, class=%d", span, span->base, span->spanclass);
        free_mspan_meta(span);
        RDEBUGF("[sweep_span] span success free meta, span=%p, base=0x%lx, class=%d", span, span->base, span->spanclass);

        return true;
    }

    if (span->alloc_count == span->obj_count) {
        RT_LIST_PUSH_HEAD(central->full_list, span);
    } else {
        RT_LIST_PUSH_HEAD(central->partial_list, span);
    }

    return false;
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
    RDEBUGF("[mcentral_sweep] start");
    mcentral_t *centrals = mheap->centrals;
    for (int i = 0; i < SPANCLASS_COUNT; ++i) {
        mcentral_t *central = &centrals[i];
        if (!central->partial_list && !central->full_list) {
            continue;
        }

        RDEBUGF("[mcentral_sweep] will handle central spc=%hhu", central->spanclass);
        // full and partial
        mspan_t *full_list = central->full_list;
        mspan_t *partial_list = central->partial_list;

        // 清空链表进行重新注册
        central->partial_list = NULL;
        central->full_list = NULL;

        // #ifdef DEBUG
        //         mspan_t *p = full_list;
        //         while (p) {
        //             RDEBUGF("[mcentral_sweep] spc=%d full current=%p, next=%p", i, p, p->next);
        //             p = p->next;
        //         }
        //         p = partial_list;
        //         while (p) {
        //             RDEBUGF("[mcentral_sweep] spc=%d partial current=%p, next=%p", i, p, p->next);
        //             p = p->next;
        //         }
        // #endif

        // 经过 sweep full -> part，或者直接清零规划给 mheap, 但是绝对不会从 part 到 full
        mspan_t *current = full_list;
        mspan_t *next = NULL;
        while (current) {
            next = current->next;
            current->next = NULL;
            RDEBUGF("[mcentral_sweep] will sweep full span, span=%p, span_base=%p, next=%p", (void *)current, (void *)current->base, next);
            bool swept = sweep_span(central, current);
            RDEBUGF("[mcentral_sweep] success sweep full span, span=%p, swept=%d", (void *)current, swept);

            current = next;
        }

        current = partial_list;
        while (current) {
            next = current->next;

            current->next = NULL;
            RDEBUGF("[mcentral_sweep] will sweep partial span, span=%p, next=%p", (void *)current, next);
            bool swept = sweep_span(central, current);
            RDEBUGF("[mcentral_sweep] success sweep partial span swept=%d", swept);

            current = next;
        }
    }

    RDEBUGF("[mcentral_sweep] start");
}

/**
 * save_stack 中保存着 coroutine 栈数组，其中 stack->ptr 指向了原始栈的栈顶
 * 整个 ptr 申请的空间是 sz, 实际占用的空间是 valid_sz，valid_sz 是经过 align 的空间
 */
static void scan_stack(processor_t *p, coroutine_t *co) {
    DEBUGF("[runtime_gc.scan_stack] start, p_index_%d=%d, co=%p, co_stack_size=%zu, save_stack=%p(%zu)", p->share, p->index, co,
           co->aco.save_stack.valid_sz, co->aco.save_stack.ptr, co->aco.save_stack.sz);

    if (!p->share) {
        assert(co->aco.share_stack);
        assert(co->aco.save_stack.ptr);

        // solo processor 需要扫描时主动进行溢出，bp_offset 在 pre_hook 中则已经记录
        // TODO 如果在 syscall 中的话就还没有溢出。此时无法得到准确的 rsp 来进行栈 resume。
        aco_share_spill(&co->aco);
    }

    // save_stack 也是通过 gc 申请，所以需要标记一下
    assert(in_heap((addr_t)co->aco.save_stack.ptr) && "coroutine save stack not in heap");
    insert_gc_worklist(&p->gc_worklist, co->aco.save_stack.ptr);

    aco_save_stack_t stack = co->aco.save_stack;
    if (stack.valid_sz == 0) {
        DEBUGF("[runtime_gc.scan_stack] valid_sz=0, return")
        return;
    }

    assert(p->share);

    if (co->gc_work) {
        DEBUGF("[runtime_gc.scan_stack] co=%p is gc_work=true, return", co);
        return;
    }

    // 首个 sp 对应的 fn 的 addr 直接从寄存器中读取，其余函数可以从栈中读取
    size_t stack_size = stack.valid_sz; // valid 标识 8byte 对齐

#ifdef DEBUG
    DEBUGF("[runtime_gc.scan_stack] traverse stack, start");
    addr_t temp_cursor = (addr_t)stack.ptr;
    size_t temp_i = 0;
    size_t max_i = stack_size / POINTER_SIZE;
    while (temp_i < max_i) {
        addr_t v = fetch_addr_value((addr_t)temp_cursor);
        fndef_t *fn = find_fn(v);
        TRACEF("[runtime_gc.scan_stack] traverse i=%zu, stack.ptr=0x%lx, value=0x%lx, fn=%s, fn.size=%ld", temp_i, temp_cursor, v,
               fn ? fn->name : "", fn ? fn->stack_size : 0);
        temp_cursor += POINTER_SIZE;
        temp_i += 1;
    }
    DEBUGF("[runtime_gc.scan_stack] traverse stack, end");
#endif

    addr_t cursor = 0;
    addr_t ret_addr = 0;

    // 如果 bp_offset == 0 一定是被抢占式调度了, 此时进行保守的 cursor 定位
    if (co->aco.bp_offset == 0) {
        DEBUGF("[runtime_gc.scan_stack] preempt co=%p, bp_offset=0, maybe preempt", co);
        // 找到的第一个 fn 作为 cursor = stack.ptr + stack_size - bp_offset
        cursor = (addr_t)stack.ptr;
        while (cursor < ((addr_t)stack.ptr + stack_size)) {
            addr_t v = fetch_addr_value(cursor);
            fndef_t *fn = find_fn(v);
            if (fn) {
                DEBUGF("[runtime_gc.scan_stack] preempt co=%p, find fn, fn_name=%s, fn_base=0x%lx, fn_size=%ld", co, fn->name, fn->base,
                       fn->size);
                ret_addr = v;
                cursor = cursor + POINTER_SIZE;
                break;
            }
            cursor += POINTER_SIZE;
        }

        // 被抢占的点可能是 coroutine_wrapper 中 usercode exit, 且没来得及配置 co->status = CO_STATUS_DEAD;
        // 此时的典型特征就是 ret_addr = 0
        if (ret_addr == 0) {
            DEBUGF("[runtime_gc.scan_stack] not found any fn, maybe in coroutine_wrapper will exit,  p_index_%d=%d, co=%p", p->share,
                   p->index, co);
        }
    } else {
        DEBUGF("[runtime_gc.scan_stack] co=%p, bp_offset=%lu, gt 0", co, co->aco.bp_offset);
        addr_t bp_offset = co->aco.bp_offset;
        assert(bp_offset > 0);
        assert(stack_size > bp_offset);

        cursor = ((addr_t)stack.ptr + (stack_size - bp_offset));
        ret_addr = (addr_t)fetch_addr_value(cursor + POINTER_SIZE);
        cursor = cursor + POINTER_SIZE + POINTER_SIZE; // 指向栈帧顶部

        assert(ret_addr > 0);
    }

    addr_t max = (addr_t)stack.ptr + stack_size;

    int scan_fn_count = 0;
    // coroutine_wrapper 也使用了该协程栈，如果遇到的 return_addr 无法找到对应的 fn 直接退出当前循环即可
    while (cursor < max) {
        fndef_t *fn = find_fn(ret_addr);
        // assertf(fn, "fn not found by ret_addr, return_addr=0x%lx", ret_addr);
        if (!fn) {
            DEBUGF("fn not found by ret_addr, return_addr=%p, break", (void *)ret_addr);
            break;
        }

        scan_fn_count++;

        DEBUGF("[runtime_gc.scan_stack] fn_name=%s, ret_addr=%p", fn->name, (void *)ret_addr);

        addr_t frame_cursor = cursor;
        uint64_t ptr_count = fn->stack_size / POINTER_SIZE;
        for (int i = 0; i < ptr_count; ++i) {
            bool is_ptr = bitmap_test(fn->gc_bits, i);
            DEBUGF("[runtime.scan_stack] fn_name=%s, fn_gc_bits i=%d, is_ptr=%d, value=0x%lx", fn->name, i, is_ptr,
                   fetch_int_value(frame_cursor, 8));

            // 即使当前 slot 的类型是 ptr 但是可能存在还没有存储值或者
            if (is_ptr) {
                addr_t ptr = fetch_addr_value(frame_cursor);
                if (in_heap(ptr)) {
                    insert_gc_worklist(&p->gc_worklist, (void *)ptr);
                }
            }

            frame_cursor += POINTER_SIZE;
        }

        // 找到下一个 return_addr
        ret_addr = (addr_t)fetch_addr_value(frame_cursor + POINTER_SIZE);

        // 跳过 prev_rbp + return addr 进行下一次循环
        cursor = frame_cursor + POINTER_SIZE + POINTER_SIZE;
    }

    DEBUGF("[runtime_gc.scan_stack] completed, p_index=%d, co=%p, scan_fn_count=%d", p->index, co, scan_fn_count);
}

static void handle_gc_ptr(rt_linked_t *worklist, addr_t addr) {
    RDEBUGF("[runtime_gc.handle_gc_ptr] start, addr=%p", (void *)addr);

    // get mspan by ptr
    mspan_t *span = span_of(addr);
    assert(span);
    assert(span->obj_size > 0);

    // get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;

    // 如果 addr 不是 span obj 的起始地点，也就是需要和 obj_size 前向对齐
    // 计算 addr 所在的 obj 的起始地址
    addr = span->base + (obj_index * span->obj_size);

    RDEBUGF("[runtime_gc.handle_gc_ptr] addr=%p, has_ptr=%d, span_base=%p, spc=%d, obj_index=%lu, span->obj_size=%lu", (void *)addr,
            spanclass_has_ptr(span->spanclass), (void *)span->base, span->spanclass, obj_index, span->obj_size);

    mutex_lock(&span->gcmark_locker);

    // 判断当前 span obj 是否已经被 gc bits mark,如果已经 mark 则不需要重复扫描
    // 其他线程可能已经标记了该 obj
    if (bitmap_test(span->gcmark_bits, obj_index)) {
        // already marks black
        RDEBUGF("[runtime_gc.handle_gc_ptr] addr=%p, span_base=%p, obj_index=%lu marked, will continue", (void *)addr, (void *)span->base,
                obj_index);
        mutex_unlock(&span->gcmark_locker);
        return;
    }

    bitmap_set(span->gcmark_bits, obj_index);
    RDEBUGF("[runtime_gc.handle_gc_ptr] addr=%p, span=%p, span_base=%p, obj_index=%lu mark, test=%d", (void *)addr, span,
            (void *)span->base, obj_index, bitmap_test(span->gcmark_bits, obj_index));

    mutex_unlock(&span->gcmark_locker);

    // - 判断 span 是否需要进一步扫描, 可以根据 obj of spanclass 直接判断 (如果不含指针, 上面直接标记过就不会被 gc 了，不需要进一步扫描)
    if (!spanclass_has_ptr(span->spanclass)) {
        // addr ~ addr+size 空间内存储的是一个标量，不需要向下扫描了
        return;
    }

    // scan object field
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

            RDEBUGF("[runtime_gc.handle_gc_ptr] addr is ptr,base=%p cursor=%p cursor_value=%p, obj_size=%ld, bit_index=%lu, in_heap=%d",
                    (void *)addr, (void *)temp_addr, (void *)heap_addr, span->obj_size, bit_index, in_heap(heap_addr));

            if (in_heap(heap_addr)) {
                assert(span_of(heap_addr) && "heap_addr not belong active span");
                insert_gc_worklist(worklist, (void *)heap_addr);
            }
        }
    }
}

static void handle_gc_worklist(processor_t *p) {
    coroutine_t *co = coroutine_get();
    RDEBUGF("[runtime_gc.handle_gc_worklist] start, p_index_%d=%d, gc_worklist.count=%d, gc_work_co=%p", p->share, p->index,
            p->gc_worklist.count, co);

    if (p->gc_worklist.count == 0) {
        p->gc_work_finished = memory->gc_count;
        return;
    }

    // 每处理 N 个 ptr 就进行 yield
    int limit_count = 0;
    while (true) {
        if (limit_count >= GC_WORKLIST_LIMIT) {
            DEBUGF("[runtime_gc.handle_gc_worklist] p_index_%d=%d, handle_count=%d, will yield", p->share, p->index, limit_count);
            limit_count = 0;
            co_yield_runnable(p, p->coroutine);
            disable_preempt(p); // coroutine resume 后总是切换为不可抢占状态
        }

        if (p->gc_worklist.count == 0) {
            break;
        }

        addr_t addr = (addr_t)rt_linked_pop(&p->gc_worklist);

        handle_gc_ptr(&p->gc_worklist, addr);

        limit_count++;
    }

    DEBUGF("[runtime_gc.handle_gc_worklist] completed,  p_index_%d=%d", p->share, p->index);
}

/**
 * 由于不经过 pre/post_tpl_hook 所以需要手动管理一下 gc 状态
 */
static void gc_work() {
    processor_t *p = processor_get();
    disable_preempt(p);

    assert(p);
    coroutine_t *co = coroutine_get();
    assert(co);

    DEBUGF("[runtime_gc.gc_work] start p_index_%d=%d, co=%p, co_count=%d", p->share, p->index, co, p->co_list.count);

    // - share goroutine root and change color black
    RT_LINKED_FOR(p->co_list) {
        coroutine_t *wait_co = RT_LINKED_VALUE();

        DEBUGF("[runtime_gc.gc_work] will scan_stack p_index_%d=%d, co=%p, status=%d, gc_black=%lu, aco=%p", p->share, p->index, wait_co,
               wait_co->status, wait_co->gc_black, &wait_co->aco);

        if (wait_co->status == CO_STATUS_DEAD) {
            continue;
        }

        // 只有第一次 resume 时才会初始化 co, 并且绑定对应的 p
        if (!wait_co->aco.inited) {
            continue;
        }

        // gc black 标识 root 已经标记完成不需要进行处理
        if (wait_co->gc_black == memory->gc_count) {
            continue;
        }

        // scan stack
        // 不用扫描 reg 了，只要 coroutine 调用了 co yield, 就已经了寄存器溢出到 stack 的操作
        // wait_co->aco->save_stack
        scan_stack(p, wait_co);

        wait_co->gc_black = memory->gc_count;
    }

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, share processor scan stack completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);
    disable_preempt(p);

    // - solo goroutine root and change color black, 读取当前 share processor index
    // solo processor 进入 block call 之前需要进行一次 save stack 保存相关的栈信息状态用于正确 GC
    // TODO 这里 need stw, 那什么时候又会解除这个 STW 呢？
    mutex_lock(&solo_processor_locker);
    processor_t *solo_p = solo_processor_list;
    while (solo_p) {
        int solo_index = solo_p->index;

        if (solo_p->exit) {
            goto SOLO_NEXT;
        }

        // cpu_count 就是 share_processor_count
        int rem = solo_index % cpu_count;
        if (rem != p->index) {
            goto SOLO_NEXT;
        }

        DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, solo processor index=%d will scan stack", p->share, p->index, solo_index);

        coroutine_t *solo_co = rt_linked_first(&solo_p->co_list)->value;
        if (solo_co->status == CO_STATUS_DEAD) {
            goto SOLO_NEXT;
        }

        if (!solo_co->aco.inited) {
            goto SOLO_NEXT;
        }

        if (solo_co->gc_black == memory->gc_count) {
            goto SOLO_NEXT;
        }

        // 和 stw 难道冲突了， solo 在等 stw 接触？
        mutex_lock(&solo_p->gc_stw_locker);
        scan_stack(solo_p, solo_co);
        mutex_unlock(&solo_p->gc_stw_locker);

        solo_co->gc_black = memory->gc_count;
    SOLO_NEXT:
        solo_p = solo_p->next;
    }
    mutex_unlock(&solo_processor_locker);

    // TODO 这里批量解除 STW 状态， STW 又需要等？等就是冲突呀。难道要直接等 STW 完成？
    // 还是应该继续抢占? 不等待的抢占？直接抢？但是 processor_run 在不停的活动中，太难了。
    // 能不能直接通过 p->coroutines->first 读状态？ 不然就要 wait.
    // 不如在 runtime_gc stw 期间直接扫描所有的  solo p 的 stack?
    // 这样就是 STW 的时间有点长？

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, solo processor scan stack completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);
    disable_preempt(p);

    // - handle share processor work list
    handle_gc_worklist(p);

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, handle gc work list completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);
    disable_preempt(p);

    // yield 期间可能新创建了一批 solo processor
    // - handle match solo gc worklist
    mutex_lock(&solo_processor_locker);
    solo_p = solo_processor_list;
    while (solo_p) {
        int solo_index = solo_p->index;
        if (solo_p->exit) {
            goto SOLO_NEXT2;
        }

        // solo 匹配对应的 share 进行 gc 处理
        int rem = solo_index % cpu_count;
        if (rem != p->index) {
            goto SOLO_NEXT2;
        }

        // 现有了 solo 没有 co?
        coroutine_t *solo_co = rt_linked_first(&solo_p->co_list)->value;
        if (solo_co->status == CO_STATUS_DEAD) {
            goto SOLO_NEXT2;
        }

        if (!solo_co->aco.inited) {
            goto SOLO_NEXT2;
        }

        // 可能是新建的，默认会等于 gc_count
        if (solo_p->gc_work_finished == memory->gc_count) {
            goto SOLO_NEXT2;
        }

        mutex_lock(&solo_p->gc_stw_locker);
        handle_gc_worklist(solo_p);
        solo_p->gc_work_finished = memory->gc_count;
        mutex_unlock(&solo_p->gc_stw_locker);
    SOLO_NEXT2:
        solo_p = solo_p->next;
    }
    mutex_unlock(&solo_processor_locker);

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, handle solo processor gc work list completed, will exit", p->share, p->index);
    p->gc_work_finished = memory->gc_count;
}

/**
 * gc work 会主动 yield，所以整个 coroutine 不允许被抢占。抢占时需要根据 co->gc_work 进行判断
 */
static void inject_gc_work_coroutine() {
    // 遍历 share processor 插入 gc coroutine
    PROCESSOR_FOR(share_processor_list) {
        coroutine_t *gc_co = coroutine_new((void *)gc_work, NULL, false, false);
        gc_co->gc_work = true;

        mutex_lock(&p->co_locker);
        rt_linked_push(&p->co_list, gc_co);
        rt_linked_push(&p->runnable_list, gc_co);
        mutex_unlock(&p->co_locker);
    }

    RDEBUGF("[runtime_gc.inject_gc_work_coroutine] inject gc work coroutine completed");
}

/**
 * 除了 coroutine stack 以外的全局变量以及 runtime 中申请的内存
 */
static void scan_global() {
    RDEBUGF("[runtime_gc.scan_global] start");

    // TODO 暂时放在第一个 share processor 中，后续可以考虑放在 global worklist 中
    processor_t *p = share_processor_list;
    assert(p);

    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        if (!s.need_gc) {
            continue;
        }

        RDEBUGF("[runtime.scan_global] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx", s.name, s.base, s.size,
                s.need_gc, fetch_int_value(s.base, s.size));

        assert(s.size <= 8 && "temp do not support symbol size > 8byte");
        assert(s.base > 0 && "s.base is zero,cannot fetch value by base");

        // 触发 gc 时全局变量可能还没有进行初始化, 所以这里使用 in_heap 进行一下地址可用对判断
        addr_t addr = fetch_addr_value(s.base);
        if (in_heap(addr)) {
            // s.base 是 data 段中的地址， fetch_addr_value 则是取出该地址中存储的数据
            // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
            rt_linked_push(&p->gc_worklist, (void *)addr);
        }
    }

    RDEBUGF("[runtime_gc.scan_global] scan global completed");
}

/**
 * 处理剩余的 global gc worklist, 当前已经在 STW 了
 */
static void gc_mark_done() {
    RDEBUGF("[runtime_gc.gc_mark_done] start");

    // - handle work list
    while (true) {
        addr_t addr = (addr_t)global_gc_worklist_pop();
        if (!addr) {
            break;
        }
        RDEBUGF("[runtime_gc.gc_mark_done] item addr=0x%lx", addr);
        handle_gc_ptr(&global_gc_worklist, addr);
    }

    RDEBUGF("[runtime_gc.gc_mark_done] handle processor gc work list completed, will yield");
}

/**
 * @stack system
 */
void runtime_gc() {
    uint64_t before = allocated_bytes;
    // - gc stage: GC_START
    gc_stage = GC_STAGE_START;
    RDEBUGF("[runtime_gc] start, gc stage: GC_START");

    memory->gc_count += 1;

    // 等待所有的 processor 进入安全点
    processor_all_stop_the_world();
    processor_all_wait_safe();

    RDEBUGF("[runtime_gc] wait all processor safe");

    // 开启写屏障
    gc_barrier_start();

    RDEBUGF("[runtime_gc] barrier start");

    // 注入 GC 工作协程, gc_worklist 用来检测状态判断 gc work 是否全部完成
    inject_gc_work_coroutine();

    RDEBUGF("[runtime_gc] gc work coroutine injected, will start the world");
    processor_all_start_the_world();

    // - gc stage: GC_MARK
    gc_stage = GC_STAGE_MARK;
    RDEBUGF("[runtime_gc] gc stage: GC_MARK");

    // 扫描全局变量与 runtime 中使用的 heap 内存，存放到 share_processor_list[0] 中
    scan_global();

    // 等待所有的 processor 都 mark 完成
    wait_all_gc_work_finished();

    // STW 之后再更改 GC 阶段
    RDEBUGF("[runtime_gc] wait all processor gc work completed, will stop the world and get solo stw locker");
    processor_all_stop_the_world();
    RDEBUGF("[runtime_gc] all share processor safe");
    // -------------- STW start ----------------------------

    // - gc stage: GC_MARK_DONE
    gc_stage = GC_STAGE_MARK_DONE;
    RDEBUGF("[runtime_gc] gc stage: GC_MARK_DONE");

    // mark 完成期间还会存在新的 mutator barrier 产生的指针推送到 worklist 中
    // 所以必须等 STW 后进行最后的收尾
    gc_mark_done();

    // - gc stage: GC_SWEEP
    gc_stage = GC_STAGE_SWEEP;
    RDEBUGF("[runtime_gc] gc stage: GC_SWEEP");

    // gc 清理
    flush_mcache();
    RDEBUGF("[runtime_gc] gc flush mcache completed");

    mcentral_sweep(memory->mheap);
    RDEBUGF("[runtime_gc]mcentral_sweep completed");
    // 更新 gcbits
    gcbits_arenas_epoch();
    RDEBUGF("[runtime_gc] gcbits_arenas_epoch completed");

    gc_barrier_stop();
    processor_all_start_the_world();
    // -------------- STW end ----------------------------

    gc_stage = GC_STAGE_OFF;
    RDEBUGF("[runtime_gc] gc stage: GC_OFF, cleanup=%ldKB", (before - allocated_bytes) / 1000);
}
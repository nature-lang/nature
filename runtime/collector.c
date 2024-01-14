#include "memory.h"
#include "processor.h"

static void insert_gc_worklist(linked_t *gc_worklist, void *ptr) {
    linked_push(gc_worklist, ptr);
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
        DEBUGF("[runtime_gc.shade_obj_grey] addr=%p not in heap", obj)
        return;
    }

    // get mspan by ptr
    mspan_t *span = span_of(addr);

    //  get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;

    mutex_lock(span->gcmark_locker);

    bitmap_clear(span->gcmark_bits->bits, obj_index);

    mutex_unlock(span->gcmark_locker);

    processor_t *p = processor_get();

    insert_gc_worklist(p->gc_worklist, obj);
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
 * @param mheap
 * 遍历 processor_list 将所有的 mcache 中持有的 mspan 都 push 到对应的 mcentral 中
 */
static void flush_mcache() {
    SLICE_FOR(share_processor_list) {
        processor_t *p = SLICE_VALUE(share_processor_list);
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

void mark_obj_black(mspan_t *span, uint64_t index) {
    bitmap_locker_set(span->gcmark_bits, index);
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

            DEBUGF(
                "[runtime.sweep_span] success, span->class=%d, span->base=0x%lx, span->obj_size=%ld, obj_addr=0x%lx, allocator_bytes=%ld",
                span->spanclass, span->base, span->obj_size, span->base + i * span->obj_size, allocated_bytes);
        }
    }

    bitmap_free(span->alloc_bits);
    span->alloc_bits = span->gcmark_bits;
    span->gcmark_bits = bitmap_new(span->obj_count); // 清空

    // 重新计算 alloc_count
    span->alloc_count = bitmap_set_count(span->alloc_bits);

    if (span->alloc_count == 0) {
        DEBUGF("[sweep_span] span free to heap, base=0x%lx, class=%d", span->base, span->spanclass);
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
 * save_stack 中保存着 coroutine 栈数组，其中 stack->ptr 指向了原始栈的栈顶
 * 整个 ptr 申请的空间是 sz, 实际占用的空间是 valid_sz，valid_sz 是经过 align 的空间
 */
static void scan_stack(processor_t *p, coroutine_t *co) {
    DEBUGF("[runtime_gc.scan_stack] start, p_index=%d, share=%d, co=%p, co_stack_size=%zu, is_preempt=%d", p->index, p->share, co,
           co->aco->save_stack.valid_sz, co->is_preempt);
    aco_t *aco = co->aco;
    assert(aco);
    aco_save_stack_t stack = aco->save_stack;
    if (stack.valid_sz == 0) {
        return;
    }

    // 首个 sp 对应的 fn 的 addr 直接从寄存器中读取，其余函数可以从栈中读取
    addr_t stack_size = stack.valid_sz; // valid 标识 8byte 对齐

#ifdef DEBUG
    DEBUGF("[runtime.scan_stack] traverse stack, start")
    addr_t temp_cursor = (addr_t)stack.ptr;
    int temp_i = 0;
    int max_i = stack_size / POINTER_SIZE;
    while (temp_i < max_i) {
        addr_t v = fetch_addr_value((addr_t)temp_cursor);
        fndef_t *fn = find_fn(v);
        DEBUGF("[runtime.scan_stack] traverse i=%d, stack.ptr=0x%lx, value=0x%lx, fn=%s, fn.size=%ld", temp_i, temp_cursor, v,
               fn ? fn->name : "", fn ? fn->stack_size : 0);
        temp_cursor += POINTER_SIZE;
        temp_i += 1;
    }
    DEBUGF("[runtime.scan_stack] traverse stack, end")
#endif

    addr_t cursor = 0;
    addr_t ret_addr = 0;
    if (co->is_preempt && co->aco->bp_offset == 0) {
        // 找到的第一个 fn 作为 cursor = stack.ptr + stack_size - bp_offset
        cursor = (addr_t)stack.ptr;
        while (cursor < ((addr_t)stack.ptr + stack_size)) {
            addr_t v = fetch_addr_value(cursor);
            fndef_t *fn = find_fn(v);
            if (fn) {
                DEBUGF("[runtime.scan_stack] preempt co=%p, find fn, fn_name=%s, fn_base=0x%lx, fn_size=%ld", co, fn->name, fn->base,
                       fn->size);
                ret_addr = v;
                cursor = cursor + POINTER_SIZE;
                break;
            }
            cursor += POINTER_SIZE;
        }
        assert(ret_addr > 0);
    } else {
        assert(aco->bp_offset > 0); // SIGURG 产生的 yield 会导致没有经过 save
        addr_t bp_offset = aco->bp_offset;
        assert(stack_size > bp_offset);

        cursor = ((addr_t)stack.ptr + (stack_size - bp_offset));
        ret_addr = (addr_t)fetch_addr_value(cursor + POINTER_SIZE);
        cursor = cursor + POINTER_SIZE + POINTER_SIZE; // 指向栈帧顶部
    }

    addr_t max = (addr_t)stack.ptr + stack_size;

    int scan_fn_count = 0;
    // coroutine_wrapper 也使用了该协程栈，如果遇到的 return_addr 无法找到对应的 fn 直接退出当前循环即可
    while (cursor < max) {
        fndef_t *fn = find_fn(ret_addr);
        //        assertf(fn, "fn not found by ret_addr, return_addr=0x%lx", ret_addr);
        if (!fn) {
            DEBUGF("fn not found by ret_addr, return_addr=%p, break", (void *)ret_addr);
            break;
        }

        scan_fn_count++;

        DEBUGF("[runtime.scan_stack] fn_name=%s, ret_addr=%p", fn->name, (void *)ret_addr);

        addr_t frame_cursor = cursor;
        int ptr_count = fn->stack_size / POINTER_SIZE;
        for (int i = 0; i < ptr_count; ++i) {
            bool is_ptr = bitmap_test(fn->gc_bits, i);
            DEBUGF("[runtime.scan_stack] fn_name=%s, fn_gc_bits i=%d, is_ptr=%d, value=0x%lx", fn->name, i, is_ptr,
                   fetch_int_value(frame_cursor, 8))

            // 即使当前 slot 的类型是 ptr 但是可能存在还没有存储值或者
            if (is_ptr) {
                addr_t ptr = fetch_addr_value(frame_cursor);
                if (in_heap(ptr)) {
                    insert_gc_worklist(p->gc_worklist, (void *)ptr);
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

static void handle_gc_ptr(linked_t *worklist, addr_t addr) {
    // get mspan by ptr
    mspan_t *span = span_of(addr);

    //  get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;

    // 如果 addr 不是 span obj 的起始地点，也就是需要和 obj_size 前向对齐
    // 计算 addr 所在的 obj 的起始地址
    addr = span->base + (obj_index * span->obj_size);

    DEBUGF(
        "[runtime_gc.grey_list_work] addr=0x%lx, spanclass_has_ptr=%d, span=0x%lx, "
        "spanclass=%d, obj_index=%lu, span->obj_size=%lu",
        addr, spanclass_has_ptr(span->spanclass), span->base, span->spanclass, obj_index, span->obj_size);

    mutex_lock(span->gcmark_locker);
    // 判断当前 span obj 是否已经被 gc bits mark,如果已经 mark 则不需要重复扫描
    // 其他线程可能已经标记了该 obj
    if (bitmap_test(span->gcmark_bits->bits, obj_index)) {
        // already marks black
        DEBUGF("[runtime_gc.grey_list_work] addr=0x%lx, span=0x%lx, obj_index=%lu marked, will continue", addr, span->base, obj_index);
        mutex_unlock(span->gcmark_locker);
        return;
    }

    mark_obj_black(span, obj_index);
    mutex_unlock(span->gcmark_locker);

    // 判断 span 是否需要进一步扫描, 可以根据 obj 所在的 spanclass 直接判断 (如果标量的话, 直接标记就行了，不需要进一步扫描)
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

            DEBUGF(
                "[runtime_gc.grey_list_work] addr is ptr,scan_base=0x%lx cursor_addr=0x%lx fetch_cursor_value=0x%lx,"
                "obj_size=%ld, bit_index=%lu, in_heap=%d",
                addr, temp_addr, heap_addr, span->obj_size, bit_index, in_heap(heap_addr));

            // TODO 由于没有加锁必须单线程进入
            if (in_heap(heap_addr)) {
                insert_gc_worklist(worklist, (void *)heap_addr);
            }
        }
    }
}

static void handle_gc_worklist(processor_t *p) {
    DEBUGF("[runtime_gc.handle_gc_worklist] start, p_index_%d=%d", p->share, p->index);
    if (p->gc_work_finished) {
        return;
    }

    if (p->gc_worklist->count == 0) {
        p->gc_work_finished = true;
        return;
    }
    // 每处理 N 个 ptr 就进行 yield
    int limit_count = 0;
    while (true) {
        if (limit_count >= GC_WORKLIST_LIMIT) {
            DEBUGF("[runtime_gc.handle_gc_worklist] p_index_%d=%d, handle_count=%d, will yield", p->share, p->index, limit_count);
            limit_count = 0;
            co_yield_runnable(p, p->coroutine);
        }

        if (linked_empty(p->gc_worklist)) {
            break;
        }

        addr_t addr = (addr_t)linked_pop(p->gc_worklist);

        handle_gc_ptr(p->gc_worklist, addr);

        limit_count++;
    }

    p->gc_work_finished = true;
    DEBUGF("[runtime_gc.handle_gc_worklist] completed, processor=%p", p)
}

static void gc_work() {
    processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    DEBUGF("[runtime_gc.gc_work] start p_index_%d=%d, co_id=%p, co_count=%d", p->share, p->index, co, p->co_list->count);

    // - share goroutine root and change color black
    LINKED_FOR(p->co_list) {
        coroutine_t *wait_co = LINKED_VALUE();
        DEBUGF("[runtime_gc.gc_work] wait scan_stack p_index_%d=%d, co=%p, status=%d, gc_black=%d", p->share, p->index, wait_co,
               wait_co->status, wait_co->gc_black);

        // 跳过自身
        if (wait_co == co) {
            continue;
        }

        if (wait_co->status == CO_STATUS_DEAD) {
            continue;
        }

        // gc black 标识 root 已经标记完成不需要进行处理
        if (wait_co->gc_black) {
            continue;
        }

        // scan stack
        // 不用扫描 reg 了，只要 coroutine 调用了 co yield, 就已经了寄存器溢出到 stack 的操作
        // wait_co->aco->save_stack
        scan_stack(p, wait_co);

        wait_co->gc_black = true;
    }

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, share processor scan stack completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);

    // - solo goroutine root and change color black, 读取当前 share processor index
    // solo processor 进入 block call 之前需要进行一次 save stack 保存相关的栈信息状态用于正确 GC
    LINKED_FOR(solo_processor_list) {
        processor_t *solo_p = LINKED_VALUE();
        int solo_index = solo_p->index;

        if (solo_p->exit) {
            continue;
        }

        int rem = solo_index % share_processor_list->count;
        if (rem != p->index) {
            continue;
        }

        DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, solo processor index=%d will scan stack", p->share, p->index, solo_index);

        coroutine_t *solo_co = solo_p->coroutine;

        // co routine 还没有开始调度
        if (!solo_co) {
            continue;
        }

        if (solo_co->status == CO_STATUS_DEAD) {
            continue;
        }

        if (solo_co->gc_black) {
            continue;
        }

        mutex_lock(solo_p->gc_locker);

        // 尝试获取锁，获取不到就阻塞
        scan_stack(solo_p, solo_co);

        mutex_unlock(solo_p->gc_locker);

        solo_co->gc_black = true;
    }

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, solo processor scan stack completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);

    // - handle work list
    handle_gc_worklist(p);

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, handle gc work list completed, will yield", p->share, p->index);
    co_yield_runnable(p, co);

    // - grey list work
    LINKED_FOR(solo_processor_list) {
        processor_t *solo_p = LINKED_VALUE();
        int solo_index = solo_p->index;

        if (solo_p->exit) {
            continue;
        }

        int rem = solo_index % share_processor_list->count;
        if (rem != p->index) {
            continue;
        }

        coroutine_t *solo_co = linked_first(solo_p->co_list)->value;
        if (solo_co->status == CO_STATUS_DEAD) {
            continue;
        }

        if (solo_p->gc_work_finished) {
            continue;
        }

        mutex_lock(solo_p->gc_locker);

        handle_gc_worklist(solo_p);

        mutex_unlock(solo_p->gc_locker);
    }

    DEBUGF("[runtime_gc.gc_work] p_index_%d=%d, handle solo processor gc work list completed, will exit", p->share, p->index);
}

static void set_gc_work_coroutine() {
    // 遍历 share processor 插入 gc coroutine
    SLICE_FOR(share_processor_list) {
        processor_t *p = SLICE_VALUE(share_processor_list);

        coroutine_t *gc_co = coroutine_new((void *)gc_work, NULL, false, false);
        gc_co->gc_work = true;

        linked_push(p->co_list, gc_co);
        linked_push(p->runnable_list, gc_co);
    }
}

/**
 * 除了 coroutine stack 以外的全局变量以及 runtime 中申请的内存
 */
static void scan_global() {
    DEBUGF("[runtime_gc.scan_global] start")

    // TODO 暂时放在第一个 share processor 中，后续可以考虑放在 global worklist 中
    processor_t *processor = share_processor_list->take[0];
    assert(processor);

    for (int i = 0; i < rt_symdef_count; ++i) {
        symdef_t s = rt_symdef_ptr[i];
        if (!s.need_gc) {
            continue;
        }

        DEBUGF("[runtime.scan_global] name=%s, .data_base=0x%lx, size=%ld, need_gc=%d, base_int_value=0x%lx", s.name, s.base, s.size,
               s.need_gc, fetch_int_value(s.base, s.size));

        assertf(s.size <= 8, "temp do not support symbol size > 8byte");
        assertf(s.base > 0, "s.base is zero,cannot fetch value by base");
        // 触发 gc 时全局变量可能还没有进行初始化, 所以这里使用 in_heap 进行一下地址可用对判断
        addr_t addr = fetch_addr_value(s.base);
        if (in_heap(addr)) {
            // s.base 是 data 段中的地址， fetch_addr_value 则是取出该地址中存储的数据
            // 从栈中取出指针数据值(并将该值加入到工作队列中)(这是一个堆内存的地址,该地址需要参与三色标记)
            linked_push(processor->gc_worklist, (void *)addr);
        }
    }

    DEBUGF("[runtime_gc.scan_global] scan global completed")
}

/**
 * 处理剩余的 global gc worklist, 当前已经在 STW 了
 */
static void gc_mark_done() {
    DEBUGF("[runtime_gc.gc_mark_done] start")

    // - handle work list
    while (true) {
        addr_t addr = (addr_t)global_gc_worklist_pop();
        if (!addr) {
            break;
        }
        DEBUGF("[runtime_gc.gc_mark_done] item addr=0x%lx", addr);
        handle_gc_ptr(global_gc_worklist, addr);
    }

    DEBUGF("[runtime_gc.gc_mark_done] handle processor gc work list completed, will yield")
}

/**
 * @stack system
 */
void runtime_gc() {
    uint64_t before = allocated_bytes;
    // - gc stage: GC_START
    gc_stage = GC_STAGE_START;
    DEBUGF("[runtime_gc] start, gc stage: GC_START");

    // 等待所有的 processor 进入安全点
    processor_stop_the_world();
    processor_wait_all_safe();

    DEBUGF("[runtime_gc] wait all processor safe")

    // 开启写屏障
    gc_barrier_start();

    DEBUGF("[runtime_gc] barrier start")

    // 注入 GC 工作协程
    set_gc_work_coroutine();

    DEBUGF("[runtime_gc] gc work coroutine injected, will start the world")
    processor_start_the_world();

    // - gc stage: GC_MARK
    gc_stage = GC_STAGE_MARK;
    DEBUGF("[runtime_gc] gc stage: GC_MARK")

    // 扫描全局变量与 runtime 中使用的 heap 内存，存放到 share_processor_list[0] 中
    scan_global();

    // 等待所有的 processor 都 mark 完成
    wait_all_gc_work_finished();

    // STW 之后再更改 GC 阶段
    processor_stop_the_world();
    processor_wait_all_safe();

    // - gc stage: GC_MARK_DONE
    gc_stage = GC_STAGE_MARK_DONE;
    DEBUGF("[runtime_gc] gc stage: GC_MARK_DONE")

    gc_mark_done();

    // - gc stage: GC_SWEEP
    gc_stage = GC_STAGE_SWEEP;
    DEBUGF("[runtime_gc] gc stage: GC_SWEEP")

    gc_barrier_stop();

    // gc 清理
    flush_mcache();
    mcentral_sweep(memory->mheap);

    processor_start_the_world();

    gc_stage = GC_STAGE_OFF;
    DEBUGF("[runtime_gc] gc stage: GC_OFF, cleanup=%ld", before - allocated_bytes)
}
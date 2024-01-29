#include "gcbits.h"
#include "memory.h"
#include "processor.h"

static uint8_t calc_sizeclass(uint64_t size) {
    for (int i = 0; i < SIZECLASS_COUNT; ++i) {
        uint64_t obj_size = class_obj_size[i];
        if (size > obj_size) {
            continue;
        }
        // obj 能够刚好装下 size
        return i;
    }
    assert(false && "notfound fit class");
    return 0;
}

// 7位sizeclass + 1位是否包含指针
static uint8_t make_spanclass(uint8_t sizeclass, uint8_t no_ptr) {
    return (sizeclass << 1) | no_ptr;
}

static uint8_t take_sizeclass(uint8_t spanclass) {
    return spanclass >> 1;
}

static uint64_t take_pages_count(uint8_t spanclass) {
    uint8_t sizeclass = take_sizeclass(spanclass);
    return clas_pages_count[sizeclass];
}

static bool summary_find_continuous(uint8_t level, page_summary_t *summaries, uint64_t *start, uint64_t *end, uint64_t pages_count) {
    uint64_t find_start = 0;
    uint64_t find_end = 0;
    bool find = false;

    // 当前 level 下每一个 summary 管理的 pages 的总量，这决定了当前 level 下的 start/max/min 都是小于等于该值的
    uint64_t max_pages_count = summary_page_count[level];
    uint64_t find_max = 0;
    for (uint64_t i = *start; i < *end; ++i) {
        page_summary_t s = summaries[i];
        find_max += s.start; // 左侧 + 新的 s 的 start
        if (find_max >= pages_count) {
            find = true;
            find_end = i;
            break;
        }

        // 当前 chunk 中间存在更大的 max 区域, 此时从当前 chunk 开始从新计算 find_max
        if (s.max > find_max) {
            find_max = s.max;
            find_start = i;
        }

        // find max 超过了需求熟练
        if (find_max >= pages_count) {
            // 找到了完整的空闲位置
            find = true;
            find_end = i;
            break;
        }

        // 目前的空间都不满足条件，判断当前空间是否是整个空闲(s.end = max_pages_count 表示全空闲)
        // 如果非完整的空闲，则会导致连续性中断, 此时从 s.end 从新开始计算
        if (s.end != max_pages_count) {
            if (s.end > 0) {
                find_start = i; // 已经从 i 开始记录
                find_max = s.end;
            } else {
                // s.end == 0, 表示当前 chunk 完全不可用，此时充值 find_max, 并且更新 find_start 为下一个块
                find_start = i + 1;
                find_max = 0;
            }
        }
    }
    *start = find_start;
    *end = find_end;
    return find;
}

static addr_t chunk_base(addr_t index) {
    return index * (CHUNK_BITS_COUNT * ALLOC_PAGE_SIZE) + ARENA_BASE_OFFSET;
}

/**
 * 一个 chunk 管理的内存大小是 CHUNK_BITS_COUNT * PAGE_SIZE
 * @param base
 * @return
 */
static uint64_t chunk_index(addr_t base) {
    return (base - ARENA_BASE_OFFSET) / (CHUNK_BITS_COUNT * ALLOC_PAGE_SIZE);
}

static uint64_t chunk_index_l1(uint64_t index) {
    return index / PAGE_ALLOC_CHUNK_SPLIT;
}

/**
 * 取余逻辑
 * @param index
 * @return
 */
static uint64_t chunk_index_l2(uint64_t index) {
    return index % PAGE_ALLOC_CHUNK_SPLIT;
}

/**
 * 根据内存地址找到响应的 arena
 * @param base
 * @return
 */
static page_chunk_t *take_chunk(addr_t base) {
    return &memory->mheap->page_alloc.chunks[chunk_index_l1(base)][chunk_index_l2(base)];
}

/**
 * l5 级别的 summary 是直接对 chunk 进行的 summary
 * start,max,end
 * @param chunk
 * @return
 */
static page_summary_t chunk_summarize(page_chunk_t chunk) {
    uint32_t max = 0;
    uint64_t bit_start = 0;
    uint64_t bit_end = 0;
    for (int i = 0; i < CHUNK_BITS_COUNT; ++i) {
        bool used = bitmap_test((uint8_t *)chunk.blocks, i);
        if (used) {
            // 重新开始计算
            bit_start = i + 1;
            continue;
        }

        bit_end = i;
        if ((bit_end + 1 - bit_start) > max) {
            max = (bit_end + 1 - bit_start);
        }
    }

    uint16_t start = 0;
    for (int i = 0; i < CHUNK_BITS_COUNT; ++i) {
        bool used = bitmap_test((uint8_t *)chunk.blocks, i);
        if (used) {
            break;
        }
        start += 1;
    }

    uint16_t end = 0;
    for (int i = CHUNK_BITS_COUNT - 1; i >= 0; i--) {
        bool used = bitmap_test((uint8_t *)chunk.blocks, i);
        if (used) {
            break;
        }
        end += 1;
    }
    page_summary_t summary = {
        .start = start,
        .max = max,
        .end = end,
    };

    return summary;
}

/**
 * 寻找这一组 summaries 中的最大组合值
 * @param next_summaries
 * @return
 */
static page_summary_t merge_summarize(uint8_t level, page_summary_t next_summaries[PAGE_SUMMARY_MERGE_COUNT]) {
    uint64_t max_pages_count = summary_page_count[level + 1]; // level + 1 = next level

    // max 算法参考 find 算法
    uint32_t max = 0;
    uint64_t continue_max = 0;
    for (int i = 0; i < PAGE_SUMMARY_MERGE_COUNT; ++i) {
        page_summary_t s = next_summaries[i];
        continue_max += s.start;

        if (s.max > continue_max) {
            continue_max = s.max;
        }
        if (continue_max > max) {
            max = continue_max;
        }

        // s.end 不等于完整 max_pages_count, 就说明 s.start 此时和 max 之间不是连续的，所以对 continue_max 进行中断
        if (s.end != max_pages_count) {
            continue_max = s.end;
        }
    }

    // 找 start
    uint16_t start = 0;
    for (int i = 0; i < PAGE_SUMMARY_MERGE_COUNT; ++i) {
        page_summary_t s = next_summaries[i];
        start += s.start;
        if (s.start == max_pages_count) {
            continue;
        }
        break;
    }

    // 找 end
    uint16_t end = 0;
    for (int i = PAGE_SUMMARY_MERGE_COUNT - 1; i >= 0; --i) {
        page_summary_t s = next_summaries[i];
        end += s.end;
        if (s.end == max_pages_count) {
            continue;
        }
        break;
    }

    page_summary_t summary = {.start = start, .max = max, .end = end};

    return summary;
}

/**
 * 计算 base ~ end 所涉及的索引地址范围,level 越高，通常涉及的索引范围也就越小
 * @param level
 * @param base
 * @param end
 * @param base_index
 * @param end_index
 */
static void calc_page_summary_index(uint8_t level, addr_t start, addr_t end, uint64_t *start_index, uint64_t *end_index) {
    uint64_t scale = summary_index_scale[level];
    // 计算 chunk index
    *start_index = chunk_index(start) / scale;
    *end_index = chunk_index(end - 1) / scale;
}

/**
 * 更新 l5 ~ l1 的 summary
 * @param base
 * @param end
 */
static void page_summary_update(addr_t base, uint64_t size) {
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;
    page_summary_t *last_summaries = page_alloc->summary[PAGE_SUMMARY_LEVEL - 1];
    addr_t end = base + size;

    // 维护 chunks summary 数据
    for (uint64_t index = chunk_index(base); index <= chunk_index(end - 1); index++) {
        // 计算 l1 可能为 null
        page_chunk_t *l1_chunks = page_alloc->chunks[chunk_index_l1(index)];
        assert(l1_chunks && "chunks is null");

        page_chunk_t chunk = l1_chunks[chunk_index_l2(index)];
        // 计算每个 chunk 的  summary
        last_summaries[index] = chunk_summarize(chunk);
    }

    // l5 一个 summary 对应一个 chunk 对应 4MB 的数据
    // l4 则可以管理 4 * 8 = 32M 的数据

    // update l4 ~ l1 summary
    for (int8_t level = PAGE_SUMMARY_LEVEL - 2; level >= 0; level--) {
        // - 计算 addr 在当前层的 index,例如在 l4 中，
        uint64_t base_index = 0;
        uint64_t end_index = 0;
        calc_page_summary_index(level, base, end, &base_index, &end_index);
        page_summary_t *current_summaries = page_alloc->summary[level];
        page_summary_t *next_level_summaries = page_alloc->summary[level + 1];
        for (uint64_t i = base_index; i <= end_index; ++i) {
            // 基于下一级别的 8 个 block, merge 出新的 summary 并赋值
            uint64_t next_summary_start = i * 8;
            uint64_t next_summary_end = next_summary_start + 8;
            page_summary_t temp_summaries[PAGE_SUMMARY_MERGE_COUNT] = {0};
            uint8_t temp_index = 0;
            for (uint64_t j = next_summary_start; j < next_summary_end; j++) {
                page_summary_t temp = next_level_summaries[j];
                temp_summaries[temp_index] = temp;
                temp_index += 1;
            }

            // calc summary
            current_summaries[i] = merge_summarize(level, temp_summaries);
        }
    }
}

/**
 * TODO 可能有越界问题,debug 时需要特别关注
 * @param base
 * @param size
 * @param value 1 表示 page使用中， 0 表示 page 空闲
 */
static void chunks_set(addr_t base, uint64_t size, bool value) {
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;
    uint64_t end = base + size; // 假如 base = 0， size = 3, 那么申请的空间是 [0~1), [1~2), [2~3), 其中 3 是应该不属于当前空间
    for (uint64_t index = chunk_index(base); index <= chunk_index(end - 1); index++) {
        // 计算 chunk
        page_chunk_t *chunk = &page_alloc->chunks[chunk_index_l1(index)][chunk_index_l2(index)];
        uint64_t temp_base = chunk_base(index);
        uint64_t bit_start = 0;
        // temp_base 是 chunk 的起始地址
        // base 是内存的申请位置， base - temp_base / PAGE_SIZE 可以判断出当前 base 是从 chunk 的哪一部分开始申请的
        if (temp_base < base) {
            bit_start = (base - temp_base) / ALLOC_PAGE_SIZE;
        }

        uint64_t bit_end = (end - temp_base) / ALLOC_PAGE_SIZE - 1;
        if (bit_end > CHUNK_BITS_COUNT - 1) {
            bit_end = CHUNK_BITS_COUNT - 1;
        }

        MDEBUGF("[runtime.chunks_set] chunk index: %lu, chunk block base: %p, bit_start: %lu, bit_end: %lu", index, (void *)chunk->blocks,
                bit_start, bit_end);

        for (uint64_t i = bit_start; i <= bit_end; ++i) {
            if (value) {
                bitmap_set((uint8_t *)chunk->blocks, i);
            } else {
                bitmap_clear((uint8_t *)chunk->blocks, i);
            };
        }
    }

    // 更新 summary
    page_summary_update(base, size);
}

/**
 * 从 mheap page_alloc_t 中查找连续空闲的 pages
 * - level 1 查找,找不到就返回 0 (查找时需要综合考虑多个 block 联合的情况,尤其是一组空闲的 summary)
 * - 最终会找到一组连续空闲的 chunks
 * - 更新这一组 chunks 为 1， 并更新相关索引
 * - 返回 chunks 的起始地址就行了
 *
 * summaries 可能会跨越多个 arena, 此时并不遵守连续性的事实？尤其是在计算更高级别的摘要时？
 * chunk 和 summary 覆盖了从 0.75T ~ 128T之间的所有的内存，所以并不存在连续性的问题，在非 arena 申请的区域
 * 其 summary [star=0,max=0,end=0] 并不会影响搜索 pages
 * @param pages_count
 * @return
 */
static addr_t page_alloc_find(uint64_t pages_count) {
    // 第一个 level 需要查找所有的元素
    uint64_t start = 0;
    uint64_t end = PAGE_SUMMARY_COUNT_L1; // l1 有 8192 个元素
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;

    for (int level = 0; level < PAGE_SUMMARY_LEVEL; ++level) {
        page_summary_t *summaries = page_alloc->summary[level];
        bool found = summary_find_continuous(level, summaries, &start, &end, pages_count);
        if (level == 0 && !found) {
            return 0;
        }
        assert(found && "level zero find, next level must found");
        // start ~ end 是当前 level 下的 index， 其在下一级的范围的 index为当前登记的 index * 8
        if (level < (PAGE_SUMMARY_LEVEL - 1)) {
            start *= PAGE_SUMMARY_MERGE_COUNT;
            end = (end + 1) * PAGE_SUMMARY_MERGE_COUNT; // start <= index < end
        }
    }

    MDEBUGF("[runtime.page_alloc_find] find continuous pages, start: %lu, end: %lu", start, end);

    // start ~ end 指向的一组 chunks 中包含连续的内存空间，现在需要确认起起点位置(假设 start == end, 其 可能是 start or mid or end
    // 中的任意一个位置)
    addr_t find_addr = 0;
    if (start == end) {
        // 从头到尾遍历找到可能的起点位置
        uint64_t bit_start = 0;
        uint64_t bit_end = 0;
        // 一个 chunk 有 512 bit
        page_chunk_t *chunk = take_chunk(start);
        for (int i = 0; i < CHUNK_BITS_COUNT; i++) {
            bool used = bitmap_test((uint8_t *)chunk->blocks, i);
            if (used) {
                bit_start = i + 1;
                continue;
            }

            bit_end = i;
            // 1, 1
            if ((bit_end + 1 - bit_start) >= pages_count) {
                break;
            }
        }

        // 计算 find_addr
        find_addr = chunk_base(start) + bit_start * ALLOC_PAGE_SIZE;

        MDEBUGF("[runtime.page_alloc_find] find addr=%p, start == end, start: %lu, chunk_base: %p, bit start: %lu, bit end: %lu",
                (void *)find_addr, start, chunk->blocks, bit_start, bit_end);

        // 更新从 find_addr 对应的 bit ~ page_count 位置的所有 chunk 的 bit 为 1
    } else {
        // start ~ end 这一段连续的内存空间跨越多个 chunk
        page_summary_t *l5_summaries = page_alloc->summary[PAGE_SUMMARY_LEVEL - 1];
        page_summary_t start_summary = l5_summaries[start];
        uint64_t bit_start = CHUNK_BITS_COUNT + 1 - start_summary.end;
        MDEBUGF(
            "[runtime.page_alloc_find] find addr=%p, start != end, start chunk: %lu, chunk summary [%d, %d, %d],"
            " end: %lu, bit start: %lu",
            (void *)find_addr, start, start_summary.start, start_summary.max, start_summary.end, end, bit_start);
        find_addr = chunk_base(start) + bit_start * ALLOC_PAGE_SIZE;
    }
    assert(find_addr % ALLOC_PAGE_SIZE == 0 && "find addr not align_up");

    // 更新相关的 chunks 为使用状态
    chunks_set(find_addr, pages_count * ALLOC_PAGE_SIZE, 1);

    MDEBUGF("[runtime.page_alloc_find] find_addr: %p, page_count: %lu, size: %lu", (void *)find_addr, pages_count,
            pages_count * ALLOC_PAGE_SIZE);

    return find_addr;
}

/**
 * 建立 mheap.page_alloc_find.chunks
 * 记录一下最大的 index, 待会查找的时候能用到
 * @param base
 * @param size
 * @return
 */
static void page_alloc_grow(addr_t base, uint64_t size) {
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;

    // 维护 chunks 数据
    uint64_t end = base + size;
    for (uint64_t index = chunk_index(base); index <= chunk_index(end - 1); index++) {
        // 计算 l1 可能为 null
        uint64_t l1 = chunk_index_l1(index);
        if (page_alloc->chunks[l1] == NULL) {
            // 这里实际上分配了 8192 个 chunk, 且这些 chunk 都有默认值 0， 我们知道 0 表示对应的 arena page 空闲
            // 但是实际上可能并不是这样，但是不用担心，我们只会更新从 base ~ base+size 对应的 summary
            // 其他 summary 的值默认也为 0，而 summary 为 0 表示没有空闲的内存使用
            // grow 本质是增加一些空闲的页面，而不需要修改 chunk 的 bit = 1
            // 这个工作交给 page_alloc_find 去做
            page_alloc->chunks[l1] = mallocz(sizeof(page_chunk_t) * PAGE_ALLOC_CHUNK_SPLIT);
        }
    }

    // grow 增长的内存相当于增长了内存的区域的内存是空闲且干净的，所以 chunks 的值不需要更新，默认就是 0
    // 只需要进行相关的 l5~l1 的 summary 更新，他们将不再是 0 了。
    page_summary_update(base, size);
}

/**
 * - 分配的大小是固定的，再 linux 64 中就是 64MB
 * - 基于 hint 调用 mmap 申请一块内存(如果申请成功则更新 hint)
 * - 更新 mheap.current_arena
 * - 由于这是一个新的 arena,所以需要像 mheap.arenas 中添加数据，而不再是 null 了
 * 但是不需要更新 mheap.arenas 相关的值,因为没有真正的开始触发分配逻辑
 * 同样mheap.pages 同样也不需要更新，因为没有相关的值被更新
 * @return
 */
void *mheap_sys_alloc(mheap_t *mheap, uint64_t *size) {
    arena_hint_t *hint = mheap->arena_hints;

    // size 对齐
    uint64_t alloc_size = align_up((int64_t)*size, ARENA_SIZE);

    void *v = NULL;
    while (true) {
        allocated_total_bytes += alloc_size;
        v = sys_memory_map((void *)hint->addr, alloc_size);
        if (v == (void *)hint->addr) {
            // 分配成功, 定义下一个分配点,基于此可以获得 64MB 对齐的内存地址
            hint->addr += alloc_size;
            break;
        }
        // 分配失败
        // 释放刚刚申请的内存区域
        sys_memory_unmap(v, alloc_size);

        // 进行申请重试
        assert(!hint->last && "out of memory: arena hint use up");
        hint = hint->next;
    }

    // 申请成功，申请的范围是 v ~ (v+alloc_size), 可能包含多个 arena, 需要创建相关 arena meta
    for (uint64_t i = arena_index((uint64_t)v); i <= arena_index((uint64_t)v + alloc_size - 1); ++i) {
        arena_t *arena = NEW(arena_t);
        arena->base = arena_base(i);
        mheap->arenas[i] = arena;
        slice_push(mheap->arena_indexes, (void *)i);
    }

    // 虚拟内存映射并不会真的写入到内存，必须触发内存页中断才行
    DEBUGF("[mheap_sys_alloc] allocate_total_bytes=%lu", allocated_total_bytes);

    *size = alloc_size;
    return v;
}

/**
 * 计算 mheap 的 pages ->spans
 * @param span
 */
static void mheap_set_spans(mspan_t *span) {
    // - 根据 span.base 定位 arena
    arena_t *arena = take_arena(span->base);

    // 一个 span 可能会占用多个 page
    uint64_t page_index = (span->base - arena->base) / ALLOC_PAGE_SIZE;
    for (int i = 0; i < span->pages_count; i++) {
        arena->spans[page_index] = span;
        TDEBUGF("[mheap_set_spans] arena_base: %p page_index=%lu set span=%p, span_base=%p", (void *)arena->base, page_index, span,
                (void *)span->base)
        page_index += 1;
    }
}

static void mheap_clear_spans(mspan_t *span) {
    DEBUGF("[runtime.mheap_clear_spans] span=%p, obj_size: %lu, pages_count: %lu", (void *)span->base, span->obj_size, span->pages_count);

    // - 根据 span.base 定位 arena
    arena_t *arena = take_arena(span->base);

    uint64_t page_index = (span->base - arena->base) / ALLOC_PAGE_SIZE;
    for (int i = 0; i < span->pages_count; i++) {
        TDEBUGF("[mheap_clear_spans] arena_base: %p page_index=%lu set span=%p, span_base=%p", (void *)arena->base, page_index, span,
                (void *)span->base)
        arena->spans[page_index] = NULL;
        page_index += 1;
    }
}

/**
 * arena heap 增长 pages_count 长度,如果 current arena 没有足够的空间，将会申请 1 个或者 n个崭新的 arena
 * @param pages_count
 */
static void mheap_grow(uint64_t pages_count) {
    // pages_alloc 按 chunk 管理内存，所以需要按 chunk 包含的 pages_count 对齐,其大小为 512bit * 8KiB = 4MiB
    uint64_t size = align_up(pages_count, CHUNK_BITS_COUNT) * ALLOC_PAGE_SIZE;

    addr_t cursor = memory->mheap->current_arena.cursor;
    addr_t end = memory->mheap->current_arena.end;
    assert(end >= cursor && "mheap not hold arena failed"); // 首次初始化时 end == cursor == 0

    if (end - cursor < size) {
        // cursor 没有足够的空间，需要重新申请一个新的空间
        // alloc_size 在方法内部会按 arena size 对齐
        uint64_t alloc_size = size;
        addr_t v = (addr_t)mheap_sys_alloc(memory->mheap, &alloc_size);
        if (v == end) {
            // arena 空间连续,此时只需要更新 end
            end += alloc_size;
        } else {
            cursor = v;
            end = v + alloc_size;
        }
    }

    // 基于 current_arena 进行 pages 的分配,分配的 pages 需要被 page_alloc 索引
    page_alloc_grow(cursor, size);

    // 更新 current arena (cursor 需要忽略掉已经申请了 size 的部分,其在上面已经进行了 grow 咯)
    memory->mheap->current_arena.cursor = cursor + size;
    memory->mheap->current_arena.end = cursor + end;
}

/**
 * @param pages_count
 * @param spanclass
 * @return
 */
static mspan_t *mheap_alloc_span(uint64_t pages_count, uint8_t spanclass) {
    assert(pages_count > 0);
    // - 从 page_alloc 中查看有没有连续 pages_count 空闲的页，如果有就直接分配
    // 因为有垃圾回收的存在，所以 page_alloc 中的历史上的某些部分存在空闲且连续的 pages
    addr_t base = page_alloc_find(pages_count);

    if (base == 0) {
        // -  page_alloc_t 中没有找到可用的 pages,基于 current arena 对当前 page alloc 进行扩容(以 chunk 为单位)
        mheap_grow(pages_count);

        // - 经过上面的 grow 再次从 page_alloc 中拉取合适大小的 pages_count 并组成 span
        base = page_alloc_find(pages_count);
    }
    assert(base > 0 && "out of memory: page alloc failed");

    // - 新增的 span 需要在 arena 中建立 page -> span 的关联关系
    mspan_t *span = mspan_new(base, pages_count, spanclass);
    mheap_set_spans(span); // 大内存申请时 span 同样放到了此处管理

    return span;
}

static void mcentral_grow(mcentral_t *mcentral) {
    MDEBUGF("[runtime.mcentral_grow] spanclass=%d, sizeclass=%d, pages_count=%lu", mcentral->spanclass, take_sizeclass(mcentral->spanclass),
            take_pages_count(mcentral->spanclass));

    // 从 mheap 中按 page 申请一段内存, mspan 对 mheap 是已知的， mheap 同样需要管理 mspan 列表
    uint64_t pages_count = take_pages_count(mcentral->spanclass);

    mspan_t *span = mheap_alloc_span(pages_count, mcentral->spanclass);
    assert(span->obj_count > 0 && "alloc span failed");

    MDEBUGF("[runtime.mcentral_grow] success, spanclass=%d, base=%lx, alloc_count=%lu, obj_count=%lu", span->spanclass, span->base,
            span->alloc_count, span->obj_count);

    // 新申请的 span 全都是空闲 obj, 直接插入到 mcentral 中
    RT_LIST_PUSH_HEAD(mcentral->partial_list, span);
}

/**
 * 从 mcache 中找到一个空闲的 mspan 并返回
 * @param mcentral
 * @return
 */
static mspan_t *cache_span(mcentral_t *mcentral) {
    mspan_t *span = NULL;
    if (mcentral->partial_list) {
        // partial 是空的，表示当前 mcentral 中已经没有任何可以使用的 mspan 了，需要去 mheap 中申请咯
        RT_LIST_POP_HEAD(mcentral->partial_list, &span);
        goto HAVE_SPAN;
    }

    // 当前 mcentral 中已经没有可以使用的 mspan 需要走 grow 逻辑
    mcentral_grow(mcentral);
    assert(mcentral->partial_list && "out of memory: mcentral grow failed");

    RT_LIST_POP_HEAD(mcentral->partial_list, &span);
HAVE_SPAN:
    TDEBUGF("[cache_span] span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu", span, (void *)span->base, span->spanclass,
            span->obj_count, span->alloc_count);

    assert(span && span->obj_count - span->alloc_count > 0 && "span unavailable");

    return span;
}

/**
 * 将 mspan 归还到 mcentral 队列中方便后续的清理
 * @param mcentral
 * @param span
 */
void uncache_span(mcentral_t *mcentral, mspan_t *span) {
    // 如果 span 还有空闲则丢到 partial 否则丢到 full
    if (span->obj_count - span->alloc_count > 0) {
        RT_LIST_PUSH_HEAD(mcentral->partial_list, span);
    } else {
        RT_LIST_PUSH_HEAD(mcentral->full_list, span);
    }
}

/**
 * 从 mcentral 中找到一个可以使用的 span 给 mcache 使用
 * mcache.alloc[spanclass] 对应的 mspan 可能 null
 * @param mcache
 * @param spanclass
 * @return
 */
static mspan_t *mcache_refill(mcache_t *mcache, uint64_t spanclass) {
    MDEBUGF("[mcache_refill] mcache=%p, spanclass=%lu start, wait locker", mcache, spanclass);
    mutex_lock(memory->locker);
    MDEBUGF("[mcache_refill] mcache=%p, spanclass=%lu get clock", mcache, spanclass);

    mspan_t *mspan = mcache->alloc[spanclass];

    mcentral_t *mcentral = &memory->mheap->centrals[spanclass];

    if (mspan) {
        // mspan to mcentral
        uncache_span(mcentral, mspan);
        MDEBUGF("mcache_refill] mcache=%p, spanclass=%lu uncache span success", mcache, spanclass);
    }

    // 从 mcentral 中读取一个 span 进行读取
    mspan = cache_span(mcentral);
    mcache->alloc[spanclass] = mspan;
    MDEBUGF("[mcache_refill] mcentral=%p, spanclass=%lu|%d, mspan_base=%p - %p, obj_size=%lu, alloc_count=%lu", mcentral, spanclass,
            mspan->spanclass, (void *)mspan->base, (void *)mspan->end, mspan->obj_size, mspan->alloc_count);

    mutex_unlock(memory->locker);
    MDEBUGF("[mcache_refill] mcache=%p, spanclass=%lu unlocker", mcache, spanclass);
    return mspan;
}

/**
 * 从 spanclass 对应的 span 中找到一个 free 的 obj 并返回
 * @param spanclass
 * @return
 */
static addr_t mcache_alloc(uint8_t spanclass, mspan_t **span) {
    MDEBUGF("[runtime.mcache_alloc] start, spc=%d", spanclass);
    processor_t *p = processor_get();
    mcache_t *mcache = &p->mcache;
    mspan_t *mspan = mcache->alloc[spanclass];

    if (mspan) {
        MDEBUGF("[mcache_alloc] p_index_%d=%d, span=%p, alloc_count=%lu, obj_count=%lu", p->share, p->index, mspan, mspan->alloc_count,
                mspan->obj_count);
    } else {
        MDEBUGF("[mcache_alloc] mspan is null");
    }

    // 如果 mspan 中有空闲的 obj 则优先选择空闲的 obj 进行分配
    // 判断当前 mspan 是否已经满载了，如果满载需要从 mcentral 中填充 mspan
    if (mspan == NULL || mspan->alloc_count == mspan->obj_count) {
        mspan = mcache_refill(mcache, spanclass);
        MDEBUGF("[runtime.mcache_alloc] p_index_%d=%d, refill mspan=%p", p->share, p->index, mspan);
    }

    *span = mspan;

    mutex_lock(&mspan->alloc_locker);

    for (int i = 0; i <= mspan->obj_count; i++) {
        // TODO 优化一下计算起点，不能每次都用遍历法
        bool used = bitmap_test(mspan->alloc_bits, i);
        if (used) {
            // DEBUGF("[runtime.mcache_alloc] obj_index=%d/%lu, used, continue", i, mspan->obj_count);
            continue;
        }

        // 找到了一个空闲的 obj 进行分配
        addr_t addr = mspan->base + i * mspan->obj_size;

        // 标记该节点已经被使用
        bitmap_set(mspan->alloc_bits, i);
        mspan->alloc_count += 1;

        mutex_unlock(&mspan->alloc_locker);
        MDEBUGF("[runtime.mcache_alloc] p_index_%d=%d, find can use addr=%p", p->share, p->index, (void *)addr);
        return addr;
    }

    mutex_unlock(&mspan->alloc_locker);

    assert(false && "out of memory: mcache_alloc");
    return 0;
}

/**
 * 设置 arena_t 的 bits 具体怎么设置可以参考 arenat_t 中的注释
 * @param addr
 * @param size
 * @param obj_size
 * @param rtype
 */
static void heap_arena_bits_set(addr_t addr, uint64_t size, uint64_t obj_size, rtype_t *rtype) {
    TDEBUGF("[runtime.heap_arena_bits_set] addr=%p, size=%lu, obj_size=%lu, start, wait locker", (void *)addr, size, obj_size);

    mutex_lock(memory->locker);
    TDEBUGF("[runtime.heap_arena_bits_set] addr=%p, size=%lu, obj_size=%lu, lock success", (void *)addr, size, obj_size);

    int index = 0;
    for (addr_t temp_addr = addr; temp_addr < addr + obj_size; temp_addr += POINTER_SIZE) {
        // 确定 arena bits base
        arena_t *arena = take_arena(addr);

        // 标记的是 ptr bit，(scan bit 暂时不做支持)
        uint64_t bit_index = arena_bits_index(arena, temp_addr);
        int bit_value;
        if (bitmap_test(rtype->gc_bits, index)) {
            bitmap_set(arena->bits, bit_index); // 1 表示为指针
            bit_value = 1;
        } else {
            bitmap_clear(arena->bits, bit_index);
            bit_value = 0;
        }

        TDEBUGF("[runtime.heap_arena_bits_set] rtype_kind=%s, size=%lu, scan_addr=0x%lx, temp_addr=0x%lx, bit_index=%ld, bit_value = % d ",
                type_kind_str[rtype->kind], size, addr, temp_addr, bit_index, bit_value);

        index += 1;
    }

    mutex_unlock(memory->locker);
    TDEBUGF("[runtime.heap_arena_bits_set] addr=%p, size=%lu, obj_size=%lu, unlock, end", (void *)addr, size, obj_size);
}

// 单位
static addr_t std_malloc(uint64_t size, rtype_t *rtype) {
    MDEBUGF("[std_malloc] start");
    bool has_ptr = rtype != NULL && rtype->last_ptr;

    uint8_t sizeclass = calc_sizeclass(size);
    uint8_t spanclass = make_spanclass(sizeclass, !has_ptr);
    assert(sizeclass > 0 && spanclass > 1);

    MDEBUGF("[std_malloc] spanclass=%d", spanclass);

    mspan_t *span;
    addr_t addr = mcache_alloc(spanclass, &span);
    assert(span && "std_malloc notfound span");

    TDEBUGF("[std_malloc] mcache_alloc addr=%p", (void *)addr);

    if (has_ptr) {
        // 对 arena.bits 做标记,标记是指针还是标量
        heap_arena_bits_set(addr, size, span->obj_size, rtype);
    }

    allocated_bytes += span->obj_size;

    char *debug_kind = "";
    if (rtype) {
        debug_kind = type_kind_str[rtype->kind];
    }
    TDEBUGF(
        "[std_malloc] success, span.class=%d, span.base=0x%lx, span.obj_size=%ld, span.alloc_count=%ld,need_size=%ld, "
        "type_kind=%s, addr=0x%lx, allocator_bytes=%ld",
        span->spanclass, span->base, span->obj_size, span->alloc_count, size, debug_kind, addr, allocated_bytes);

    return addr;
}

static addr_t large_malloc(uint64_t size, rtype_t *rtype) {
    bool no_ptr = rtype == NULL || rtype->last_ptr == 0;
    uint8_t spanclass = make_spanclass(0, no_ptr);

    // 计算需要分配的 page count(向上取整)
    uint64_t pages_count = size / ALLOC_PAGE_SIZE;
    if ((size & PAGE_MASK) != 0) {
        pages_count += 1;
    }

    mutex_lock(memory->locker);

    // 直接从堆中分配 span
    mspan_t *span = mheap_alloc_span(pages_count, spanclass);

    assert(span != NULL && "out of memory: large malloc");

    // 将 span 推送到 full swept 中，这样才能被 sweept
    mcentral_t *central = &memory->mheap->centrals[spanclass];
    RT_LIST_PUSH_HEAD(central->full_list, span);

    mutex_unlock(memory->locker);

    allocated_bytes += span->obj_size;

    char *debug_kind = "";
    if (rtype) {
        debug_kind = type_kind_str[rtype->kind];
    }
    TDEBUGF(
        "[runtime.large_malloc] success, span->class=%d, span->base=0x%lx, span->obj_size=%ld, need_size=%ld, type_kind=%s, "
        "addr=0x%lx, allocator_bytes=%ld",
        span->spanclass, span->base, span->obj_size, size, debug_kind, span->base, allocated_bytes);

    return span->base;
}

/**
 * @return
 */
arena_hint_t *arena_hints_init() {
    arena_hint_t *first = NEW(arena_hint_t);
    first->addr = (uint64_t)ARENA_HINT_BASE;
    first->last = false;
    first->next = NULL;

    arena_hint_t *prev = first;
    for (int i = 1; i < ARENA_HINT_COUNT; i++) {
        arena_hint_t *item = NEW(arena_hint_t);
        item->addr = prev->addr + ARENA_HINT_SIZE;
        prev->next = item;
        prev = item;
    }
    prev->last = true;
    return first;
}

/**
 * - 将 span 从 mcentral 中移除
 * - mspan.base ~ mspan.end 所在的内存区域的 page 需要进行释放
 * - 更新 arena_t 的 span
 * - 能否在不 unmap 的情况下回收物理内存？
 *   使用 madvise(addr, 50 * 1024 * 1024, MADV_REMOVE);
 * @param mheap
 * @param span
 */
void mheap_free_span(mheap_t *mheap, mspan_t *span) {
    RDEBUGF("[mheap_free_span] start, span->base=%p, pages_count=%lu, chunk_index=%lu", (void *)span->base, span->pages_count,
            chunk_index(span->base));

    // 从 page_alloc 的视角清理 span 对应的内存页
    // chunks bit = 0 表示空闲
    chunks_set(span->base, span->pages_count * ALLOC_PAGE_SIZE, 0);

    RDEBUGF("[mheap_free_span] chunk set success");

    // 从 arena 视角清理 span
    mheap_clear_spans(span);

    RDEBUGF("[mheap_free_span] mheap_clear_spans success");
    // arena.bits 保存了当前 span 中的指针 bit, 当下一次当前内存被分配时会覆盖写入
    // 垃圾回收期间不会有任何指针指向该空间，因为当前 span 就是因为没有被任何 ptr 指向才被回收的

    remove_total_bytes += span->pages_count * ALLOC_PAGE_SIZE;

    // 将物理内存归还给操作系统
    RDEBUGF("[runtime.mheap_free_span] remove_total_bytes=%lu MB, span.base=%p, span.pages_count=%ld, remove_size=%lu",
            remove_total_bytes / 1024 / 1024, (void *)span->base, span->pages_count, span->pages_count * ALLOC_PAGE_SIZE);

    sys_memory_remove((void *)span->base, span->pages_count * ALLOC_PAGE_SIZE);

    RDEBUGF("[mheap_free_span] sys_memory_remove success");
}

void memory_init() {
    memory = NEW(memory_t);
    memory->sweepgen = 0;
    memory->gc_count = 0;
    memory->locker = mutex_new(false);

    // 初始化 gc 参数
    allocated_bytes = 0;
    next_gc_bytes = DEFAULT_NEXT_GC_BYTES;

    // - 初始化 mheap
    mheap_t *mheap = mallocz_big(sizeof(mheap_t)); // 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap->page_alloc.summary[4] = mallocz_big(PAGE_SUMMARY_COUNT_L5 * sizeof(page_summary_t));
    mheap->page_alloc.summary[3] = mallocz_big(PAGE_SUMMARY_COUNT_L4 * sizeof(page_summary_t));
    mheap->page_alloc.summary[2] = mallocz_big(PAGE_SUMMARY_COUNT_L3 * sizeof(page_summary_t));
    mheap->page_alloc.summary[1] = mallocz_big(PAGE_SUMMARY_COUNT_L2 * sizeof(page_summary_t));
    mheap->page_alloc.summary[0] = mallocz_big(PAGE_SUMMARY_COUNT_L1 * sizeof(page_summary_t)); // 8192

    // - arena hint init
    mheap->arena_hints = arena_hints_init();

    // - arena index init
    mheap->arena_indexes = slice_new();
    mheap->spans = slice_new();

    // new current arena
    mheap->current_arena.cursor = 0;
    mheap->current_arena.end = 0;

    // - 初始化 mcentral
    for (int i = 0; i < SPANCLASS_COUNT; i++) {
        mcentral_t *item = &mheap->centrals[i];
        item->spanclass = i;

        item->partial_list = NULL;
        item->full_list = NULL;
    }

    // - 初始化 fixalloc
    fixalloc_init(&mheap->spanalloc, sizeof(mspan_t));

    // - 初始化 gcbits
    gcbits_areas_init();

    // links 数据反序列化，此时 rt_fndef_data rt_rtype_data 等数据可以正常使用
    DEBUGF("[memory_init] will start deserialize");
    DEBUGF("[memory_init] fndef count = %lu", rt_fndef_count);
    DEBUGF("[memory_init] symdef count = %lu", rt_symdef_count);
    DEBUGF("[memory_init] rtype count = %lu", rt_rtype_count);

    fndefs_deserialize();
    DEBUGF("[memory_init] fndefs_deserialize success");
    symdefs_deserialize();
    DEBUGF("[memory_init] symdefs_deserialize success");
    rtypes_deserialize();
    DEBUGF("[memory_init] rtypes_deserialize success");

    memory->mheap = mheap;
}

mspan_t *span_of(uint64_t addr) {
    // DEBUGF("[span_of] addr = %0lx", addr);
    // 根据 ptr 定位 arena, 找到具体的 page_index,
    arena_t *arena = take_arena(addr);
    TDEBUGF("[span_of] addr=%p", (void *)addr);
    assert(arena && "not found arena");

    // 一个 arena 有 ARENA_PAGES_COUNT(8192 个 page), 根据 addr 定位 page_index
    uint64_t page_index = (addr - arena->base) / ALLOC_PAGE_SIZE;
    mspan_t *span = arena->spans[page_index];
    TDEBUGF("[span_of] page_index=%lu, span=%p", page_index, span);
    assert(span && "not found span by page_index");
    return span;
}

addr_t mstack_new(uint64_t size) {
    // TODO 仅仅是为了方便调试，才指定了映地址
    void *base = sys_memory_map((void *)0x4000000000, size);

    return (addr_t)base;
}

/**
 * 会将分配的内存进行一个清空处理
 * @param size
 * @param rtype
 * @return
 */
void *rt_clr_malloc(uint64_t size, rtype_t *rtype) {
    void *ptr = rt_gc_malloc(size, rtype);
    memset(ptr, 0, size);
    return ptr;
}

/**
 * safe
 * @return
 */
void *rt_gc_malloc(uint64_t size, rtype_t *rtype) {
    processor_t *p = processor_get();
    assert(p);
    assert(p->can_preempt == false);

    MDEBUGF("[rt_gc_malloc] start p_index_%d=%d", p->share, p->index);

    if (rtype) {
        MDEBUGF("[rt_gc_malloc] size=%ld, type_kind=%s", size, type_kind_str[rtype->kind]);
    } else {
        MDEBUGF("[rt_gc_malloc] size=%ld, type is null", size);
    }

    void *ptr;
    if (size <= STD_MALLOC_LIMIT) {
        MDEBUGF("[rt_gc_malloc] std malloc");
        // 1. 标准内存分配(0~32KB)
        ptr = (void *)std_malloc(size, rtype);
    } else {
        MDEBUGF("[rt_gc_malloc] large malloc");
        // 2. 大型内存分配(大于>32KB)
        ptr = (void *)large_malloc(size, rtype);
    }

    // 如果当前写屏障开启，则新分配的对象都是黑色(不在工作队列且被 span 标记), 避免在本轮被 GC 清理
    if (gc_barrier_get()) {
        mark_ptr_black(ptr);
    }

    MDEBUGF("[rt_gc_malloc] end p_index_%d=%d, result=%p", p->share, p->index, ptr);
    return ptr;
}

/**
 * @param base
 * @param pages_count
 * @param spanclass
 * @return
 */
mspan_t *mspan_new(uint64_t base, uint64_t pages_count, uint8_t spanclass) {
    // 必须要加锁正太才能使用 fixalloc
    assert(memory->locker->locker_count > memory->locker->unlocker_count);
    mspan_t *span = fixalloc_alloc(&memory->mheap->spanalloc);

    span->base = base;
    span->next = NULL;
    span->pages_count = pages_count;
    span->alloc_count = 0;
    span->sweepgen = 0;
    span->spanclass = spanclass;
    uint8_t sizeclass = take_sizeclass(spanclass);
    if (sizeclass == 0) { // 使用 spanclass = 0 来管理 large_malloc
        span->obj_size = pages_count * ALLOC_PAGE_SIZE;
        span->obj_count = 1;
    } else {
        span->obj_size = class_obj_size[sizeclass];
        assert(span->obj_size > 0 && "span obj_size is zero");
        span->obj_count = span->pages_count * ALLOC_PAGE_SIZE / span->obj_size;
    }

    span->end = span->base + (span->pages_count * ALLOC_PAGE_SIZE);
    mutex_init(&span->gcmark_locker, false);
    mutex_init(&span->alloc_locker, false);
    span->alloc_bits = gcbits_new(span->obj_count);
    span->gcmark_bits = gcbits_new(span->obj_count);

    DEBUGF("[mspan_new] success, base=%lx, pages_count=%lu, spanclass=%d, sizeclass=%d, obj_size=%lu, obj_count=%lu", span->base,
           span->pages_count, span->spanclass, sizeclass, span->obj_size, span->obj_count);
    return span;
}

uint64_t runtime_malloc_bytes() {
    return allocated_bytes;
}

void runtime_eval_gc() {
    mutex_lock(gc_stage_locker);

    if (gc_stage != GC_STAGE_OFF) {
        DEBUGF("[runtime_eval_gc] gc is running = %d, skip", gc_stage);
        goto EXIT;
    }

    if (allocated_bytes < next_gc_bytes) {
        goto EXIT;
    } else {
        DEBUGF("[runtime_eval_gc] will gc, because allocated_bytes=%ld > next_gc_bytes=%ld", allocated_bytes, next_gc_bytes);
        next_gc_bytes = allocated_bytes * NEXT_GC_FACTOR;
    }

    gc_stage = GC_STAGE_START;
    uv_thread_t runtime_gc_thread;
    uv_thread_create(&runtime_gc_thread, runtime_gc, NULL);

EXIT:
    mutex_unlock(gc_stage_locker);
}

void runtime_force_gc() {
    if (mutex_trylock(gc_stage_locker) != 0) {
        return;
    }

    DEBUGF("[runtime_force_gc] start");

    if (gc_stage != GC_STAGE_OFF) {
        DEBUGF("[runtime_force_gc] gc is running, skip");
        goto EXIT;
    }

    gc_stage = GC_STAGE_START;
    uv_thread_t runtime_gc_thread;
    uv_thread_create(&runtime_gc_thread, runtime_gc, NULL);

EXIT:
    mutex_unlock(gc_stage_locker);
    DEBUGF("[runtime_force_gc] end");
}

void *runtime_malloc(uint64_t rtype_hash) {
    rtype_t *rtype = rt_find_rtype(rtype_hash);
    return rt_clr_malloc(rtype->size, rtype);
}
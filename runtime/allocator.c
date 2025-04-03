#include "gcbits.h"
#include "memory.h"
#include "processor.h"

static uint8_t calc_sizeclass(uint64_t size) {
    // 跳过 jit size
    for (int i = 0; i < SIZECLASS_COUNT - 1; ++i) {
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

// 7 位sizeclass + 1位是否包含指针
// 首位是 1 标识 no_ptr, 首位是 0 标识 ptr
static inline uint8_t make_spanclass(uint8_t sizeclass, uint8_t no_ptr) {
    return (sizeclass << 1) | no_ptr;
}

static inline uint8_t take_sizeclass(uint8_t spanclass) {
    return spanclass >> 1;
}

static inline uint64_t take_pages_count(uint8_t spanclass) {
    uint8_t sizeclass = take_sizeclass(spanclass);
    return clas_pages_count[sizeclass];
}

static bool summary_find_continuous(uint8_t level, page_summary_t *summaries, uint64_t *start, uint64_t *end,
                                    uint64_t pages_count) {
    uint64_t find_start = *start;
    uint64_t find_end = *end;
    bool find = false;

    // 当前 level 下每一个 summary 管理的 pages 的总量，这决定了当前 level 下的 start/max/min 都是小于等于该值的
    uint64_t max_pages_count = summary_page_count[level];
    uint64_t find_max = 0;
    for (uint64_t i = *start; i < *end; ++i) {
        page_summary_t s = summaries[i];
        //        DEBUGF("[runtime.summary_find_continuous] level=%d, i=%lu, start=%d, max=%d, end=%d, find_max=%lu, max_pages_count=%lu", level, i, s.start, s.max, s.end, find_max, max_pages_count);
        find_max += s.start;// 左侧 + 新的 s 的 start
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
                find_start = i;// 已经从 i 开始记录
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
        bool used = bitmap_test((uint8_t *) chunk.blocks, i);
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
        bool used = bitmap_test((uint8_t *) chunk.blocks, i);
        if (used) {
            break;
        }
        start += 1;
    }

    uint16_t end = 0;
    for (int i = CHUNK_BITS_COUNT - 1; i >= 0; i--) {
        bool used = bitmap_test((uint8_t *) chunk.blocks, i);
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
    uint64_t max_pages_count = summary_page_count[level + 1];// level + 1 = next level

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

    uint64_t current_level_max_page_count = summary_page_count[level];
    assert(start <= current_level_max_page_count);
    assert(max <= current_level_max_page_count);
    assert(end <= current_level_max_page_count);
    page_summary_t summary = {.start = start, .max = max, .end = end};

    return summary;
}

// page summary 是一个  radix tree 解构
// level 0 ～ 4， 每一级别的都管理着一定数量的 next page
// L4 直接管理 chunk, 没有下一级。
// L3 每个 summary 管理 8 个 L4 summary 块
// L2 管理 8 个 L3 块, 64 个 L4
// L1 管理 8 个 L2 块, 64 个 L3, 512 个 L4
// L0 管理 8 个 L1 块, 64 个 L2, 512 个 L3, 4096 个 L4
// l4_index 是 l4 的索引, 请计算给定 level 所在的索引
// level 会传递 0 ～ 3, 请基于 L4_index, 计算出所在 level 的 index
uint64_t calc_page_summary_level_index(uint8_t level, uint64_t l4_index) {
    switch (level) {
        case 0:
            return l4_index / 4096;
        case 1:
            return l4_index / 512;
        case 2:
            return l4_index / 64;
        case 3:
            return l4_index / 8;
        default:
            assert(false);
    }
}

/**
 * 更新 l5 ~ l1 的 summary
 * @param base
 * @param end
 */
static void page_summary_update(addr_t base, uint64_t size) {
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;
    page_summary_t *lowest_summaries = page_alloc->summary[PAGE_SUMMARY_LEVEL - 1];
    addr_t end = base + size - 1;// 从 0 开始计算 offset
    uint64_t lowest_base_index = chunk_index(base);
    uint64_t lowest_end_index = chunk_index(end);

    // 维护 chunks summary 数据
    for (uint64_t index = lowest_base_index; index <= lowest_end_index; index++) {
        // 计算 l1 可能为 null
        page_chunk_t *l1_chunks = page_alloc->chunks[chunk_index_l1(index)];
        assert(l1_chunks && "chunks is null");

        page_chunk_t chunk = l1_chunks[chunk_index_l2(index)];

        // 计算每个 chunk 的  summary
        lowest_summaries[index] = chunk_summarize(chunk);
    }

    // l5 一个 summary 对应一个 chunk 对应 4MB 的数据
    // l4 则可以管理 4 * 8 = 32M 的数据, 以此类推

    // update l4 ~ l1 summary
    for (int8_t level = PAGE_SUMMARY_LEVEL - 2; level >= 0; level--) {
        // - 计算 addr 在当前层的 index,例如在 l4 中，
        // base_index <= ~ < end_index 之间的所有索引都需要被重新计算
        uint64_t base_index = calc_page_summary_level_index(level, lowest_base_index);
        uint64_t end_index = calc_page_summary_level_index(level, lowest_end_index);

        page_summary_t *current_summaries = page_alloc->summary[level];
        page_summary_t *next_level_summaries = page_alloc->summary[level + 1];

        for (uint64_t i = base_index; i <= end_index; ++i) {
            // 基于下一级别的 8 个 block, merge 出新的 summary 并赋值
            uint64_t next_summary_start = i * 8;
            uint64_t next_summary_end = next_summary_start + 8;// 8个一组
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
 * @param value 1 表示 page 使用中， 0 表示 page 空闲
 */
static void chunks_set(addr_t base, uint64_t size, uint8_t value) {
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;
    uint64_t end = base + size;// 假如 base = 0， size = 3, 那么申请的空间是 [0~1), [1~2), [2~3), 其中 3 是应该不属于当前空间
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

        MDEBUGF("[runtime.chunks_set] chunk index: %lu, chunk block base: %p, bit_start: %lu, bit_end: %lu", index,
                (void *) chunk->blocks,
                bit_start, bit_end);

        for (uint64_t i = bit_start; i <= bit_end; ++i) {
            if (value == 1) {
                bitmap_set((uint8_t *) chunk->blocks, i);
            } else {
                bitmap_clear((uint8_t *) chunk->blocks, i);
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
uint64_t page_alloc_find(uint64_t pages_count, bool must_find) {
    addr_t hint_addr = memory->mheap->arena_hints->addr;

    // PAGE_SUMMARY_COUNT_L1  = 8192 个 summary, 每个 summary 管理 32G 空间
    int64_t heap_used = (int64_t) hint_addr - ARENA_HINT_BASE;
    assertf(heap_used >= 0, "heap used must > 0, heap used=%lu, hint_addr=%lu, arena_hint_base=%lu", heap_used,
            hint_addr, ARENA_HINT_BASE);

    // end 基于 hint_addr 计算 max end
    //     uint64_t end = PAGE_SUMMARY_COUNT_L1;// l1 有 8192 个元素
    // 基于 used 大小计算 end 的最大值
    uint64_t end = (heap_used / (32LL * 1024 * 1024 * 1024)) + 1;
    // 第一个 level 需要查找所有的元素
    uint64_t start = 0;


    page_alloc_t *page_alloc = &memory->mheap->page_alloc;
    DEBUGF(
            "[runtime.page_alloc_find] will find continuous pages, l1 start=%lu, end=%lu, heap_used=%lu, pages_count=%lu",
            start, end,
            heap_used, pages_count);

    if (start == end) {
        if (must_find) {
            assert(false);
        }

        return 0;
    }


    for (int level = 0; level < PAGE_SUMMARY_LEVEL; ++level) {
        page_summary_t *summaries = page_alloc->summary[level];
        bool found = summary_find_continuous(level, summaries, &start, &end, pages_count);
        if (level == 0 && !found) {
            if (must_find) {
                assert(false);
            }

            return 0;
        }

        DEBUGF(
                "[runtime.page_alloc_find] level=%d, found=%d, start=%lu, end=%lu, start_summary=[%u, %u, %u], end_summary=[%u, %u, %u]",
                level + 1, found, start, end, summaries[start].start, summaries[start].max, summaries[start].end,
                summaries[end].start, summaries[end].max, summaries[end].end);

        assert(found && "level zero find, next level must found");
        // start ~ end 是当前 level 下的 index， 其在下一级的范围的 index为当前登记的 index * 8
        if (level < (PAGE_SUMMARY_LEVEL - 1)) {
            start *= PAGE_SUMMARY_MERGE_COUNT;
            end = (end + 1) * PAGE_SUMMARY_MERGE_COUNT;// start <= index < end
        }
    }
    page_summary_t *lowest_summaries = page_alloc->summary[PAGE_SUMMARY_LEVEL - 1];
    DEBUGF(
            "[runtime.page_alloc_find] success find continuous pages(%lu), l5 start=%lu, end=%lu, start_summary=[%u, %u, %u], end_summary=[%u, %u, %u]",
            pages_count,
            start, end, lowest_summaries[start].start, lowest_summaries[start].max, lowest_summaries[start].end,
            lowest_summaries[end].start, lowest_summaries[end].max, lowest_summaries[end].end);

    // start 和 chunk 表示 chunk 的 index,
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
            bool used = bitmap_test((uint8_t *) chunk->blocks, i);
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

        DEBUGF(
                "[runtime.page_alloc_find] find addr=%p, start == end, start: %lu, chunk_base: %p, bit start: %lu, bit end: %lu",
                (void *) find_addr, start, chunk->blocks, bit_start, bit_end);

        // 更新从 find_addr 对应的 bit ~ page_count 位置的所有 chunk 的 bit 为 1
    } else {
        // start ~ end 这一段连续的内存空间跨越多个 chunk，则 summary [start,max,end]
        // 在跨越多个 chunk 的情况下连续空间一定由 end 标记
        page_summary_t start_summary = lowest_summaries[start];
        // summary.end 表示 chunk 尾部可用的空间
        uint64_t bit_start = CHUNK_BITS_COUNT - start_summary.end;// 512 - start_summary.end
        DEBUGF(
                "[runtime.page_alloc_find] find addr=%p, start:%lu != end:%lu, start.summary [%d, %d, %d], bit_offset: %lu",
                (void *) find_addr, start, end, start_summary.start, start_summary.max, start_summary.end, bit_start);
        find_addr = chunk_base(start) + bit_start * ALLOC_PAGE_SIZE;
    }
    assert(find_addr % ALLOC_PAGE_SIZE == 0 && "find addr not align_up");

    // 更新相关的 chunks 为使用状态
    chunks_set(find_addr, pages_count * ALLOC_PAGE_SIZE, 1);

    DEBUGF("[runtime.page_alloc_find] find_addr: %p, page_count: %lu, size: %lu", (void *) find_addr, pages_count,
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
    DEBUGF("[runtime.page_alloc_grow] base=%p, size=%luM", (void *) base, size / 1024 / 1024);
    page_alloc_t *page_alloc = &memory->mheap->page_alloc;

    // 维护 chunks 数据
    uint64_t end = base + size - 1;
    for (uint64_t index = chunk_index(base); index <= chunk_index(end); index++) {
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

    // page_summary l1 test check，必须要有空间
    page_summary_t *summaries = memory->mheap->page_alloc.summary[0];// l1 的 first summaries 管理 32G 的内存
    page_summary_t summary = summaries[0];
    assert(summary.max > 0);
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
    uint64_t alloc_size = align_up((int64_t) *size, ARENA_SIZE);

    DEBUGF("[mheap_sys_alloc] hint addr=%p, need_size=%luM, align_alloc_size=%luM", (void *) hint->addr,
           *size / 1024 / 1024, alloc_size / 1024 / 1024);

    void *v = NULL;
    while (true) {
        allocated_total_bytes += alloc_size;
        v = sys_memory_map((void *) hint->addr, alloc_size);
        if (v == (void *) hint->addr) {
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
    for (uint64_t i = arena_index((uint64_t) v); i <= arena_index((uint64_t) v + alloc_size - 1); ++i) {
        arena_t *arena = NEW(arena_t);
        arena->base = arena_base(i);
        mheap->arenas[i] = arena;
        slice_push(mheap->arena_indexes, (void *) i);
    }

    // 虚拟内存映射并不会真的写入到内存，必须触发内存页中断才行
    DEBUGF("[mheap_sys_alloc] allocate_total_bytes=%lu, hint_addr=%p", allocated_total_bytes, (void *) hint->addr);

    *size = alloc_size;
    return v;
}

/**
 * 计算 mheap 的 pages ->spans
 * @param span
 */
static void mheap_set_spans(mspan_t *span) {
    MDEBUGF("[mheap_set_spans] start, span=%p, base=%p, spc=%d, obj_size=%lu, pages_count=%lu", span,
            (void *) span->base, span->spanclass,
            span->obj_size, span->pages_count);

    for (int i = 0; i < span->pages_count; i++) {
        addr_t cursor_addr = span->base + (i * ALLOC_PAGE_SIZE);
        arena_t *arena = take_arena(cursor_addr);
        assert(arena && "cannot find arena by addr");

        uint64_t page_index = (cursor_addr - arena->base) / ALLOC_PAGE_SIZE;

        // 判断当前 page_index 是否已经被其他 span 占用，如果占用了
        DEBUGF(
                "[mheap_set_spans] arena_base=%p page_index=%lu will set span=%p, span_base=%p, cursor_addr=%p, page_count=%lu",
                (void *) arena->base,
                page_index, span, (void *) span->base, (void *) cursor_addr,
                span->pages_count);


        assert(arena->spans[page_index] == NULL && "span overlap");

        arena->spans[page_index] = span;
    }
}

static void mheap_clear_spans(mspan_t *span) {
    DEBUGF("[mheap_clear_spans] span=%p, base=%p, obj_size: %lu, pages_count: %lu", span, (void *) span->base,
           span->obj_size,
           span->pages_count);

    for (int i = 0; i < span->pages_count; i++) {
        addr_t cursor_addr = span->base + (i * ALLOC_PAGE_SIZE);
        arena_t *arena = take_arena(cursor_addr);
        assert(arena && "cannot find arena by addr");
        uint64_t page_index = (cursor_addr - arena->base) / ALLOC_PAGE_SIZE;


        DEBUGF("[mheap_clear_spans] arena_base: %p page_index=%lu set span=%p, span_base=%p, pages_count=%ld",
               (void *) arena->base, page_index, span,
               (void *) span->base, span->pages_count)

        if (arena->spans[page_index] == NULL) {
            assert(false && "span not set");
        }

        arena->spans[page_index] = NULL;
    }
}

/**
 * arena heap 增长 pages_count 长度,如果 current arena 没有足够的空间，将会申请 1 个或者 n个崭新的 arena
 * @param pages_count
 */
void mheap_grow(uint64_t pages_count) {
    // pages_alloc 按 chunk 管理内存，所以需要按 chunk 包含的 pages_count 对齐,其大小为 512bit * 8KiB = 4MiB
    uint64_t size = align_up(pages_count, CHUNK_BITS_COUNT) * ALLOC_PAGE_SIZE;

    // end 是 offset, 但是可使用的 offset 不包含 end 本身
    // [1 <= x  < 3], 1, 2
    // [0 <= x  < 3], 0, 1, 2
    // end - cursor = 可用的总空间
    addr_t cursor = memory->mheap->current_arena.cursor;
    addr_t end = memory->mheap->current_arena.end;
    assert(end >= cursor && "mheap not hold arena failed");// 首次初始化时 end == cursor == 0

    if ((end - cursor) < size) {
        // cursor 没有足够的空间，需要重新申请一个新的空间
        // alloc_size 在方法内部会按 arena size 对齐
        uint64_t alloc_size = size;
        // 超过 64M 的内存分配可能会造成空间不连续的问题, 需要需要让 cursor 重新指向
        addr_t v = (addr_t) mheap_sys_alloc(memory->mheap, &alloc_size);

        assert(v != 0);
        if (v == end) {
            // arena 空间连续,此时只需要更新 end 不需要更新 cursor
            end += alloc_size;
        } else {
            cursor = v;
            end = v + alloc_size;
        }
    }

    // 基于 current_arena 进行 pages 的分配,分配的 pages 需要被 page_alloc 索引
    page_alloc_grow(cursor, size);

    // 更新 current arena (cursor 需要忽略掉已经申请了 size 的部分,  所以需要 size - 1 才是 0ffset)
    // 由于 cursor 是 offset，指向可以使用的内存 offset, 加入 offset = 0, size = 3, 那么 0，1，2 这三个空间被使用掉了,
    // 则下一个可用 offset = 3。 也就是 0 + 3 = 3，
    memory->mheap->current_arena.cursor = cursor + size;
    memory->mheap->current_arena.end = end;
}

/**
 * @param pages_count
 * @param spanclass
 * @return
 */
static mspan_t *mheap_alloc_span(uint64_t pages_count, uint8_t spanclass) {
    mutex_lock(&memory->locker);
    assert(pages_count > 0);
    // - 从 page_alloc 中查看有没有连续 pages_count 空闲的页，如果有就直接分配
    // 因为有垃圾回收的存在，所以 page_alloc 中的历史上的某些部分存在空闲且连续的 pages
    addr_t base = page_alloc_find(pages_count, false);
    if (base == 0) {
        DEBUGF("[mheap_alloc_span] cannot find continuous pages, will grow, pages_count=%lu", pages_count);
        // -  page_alloc_t 中没有找到可用的 pages,基于 current arena 对当前 page alloc 进行扩容(以 chunk 为单位)
        mheap_grow(pages_count);

        // - 经过上面的 grow 再次从 page_alloc 中拉取合适大小的 pages_count 并组成 span
        base = page_alloc_find(pages_count, true);
        assert(base > 0 && "out of memory: page alloc failed");
    }

    // - 新增的 span 需要在 arena 中建立 page -> span 的关联关系
    mspan_t *span = mspan_new(base, pages_count, spanclass);
    mheap_set_spans(span);// 大内存申请时 span 同样放到了此处管理

    mutex_unlock(&memory->locker);
    DEBUGF("[mheap_alloc_span] success, span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu", span,
           (void *) span->base,
           span->spanclass, span->obj_count, span->alloc_count);
    return span;
}

static void mcentral_grow(mcentral_t *mcentral) {
    MDEBUGF("[runtime.mcentral_grow] spc=%d, szc=%d, need pages_count=%lu", mcentral->spanclass,
            take_sizeclass(mcentral->spanclass),
            take_pages_count(mcentral->spanclass));

    // 从 mheap 中按 page 申请一段内存, mspan 对 mheap 是已知的， mheap 同样需要管理 mspan 列表
    uint64_t pages_count = take_pages_count(mcentral->spanclass);

    mspan_t *span = mheap_alloc_span(pages_count, mcentral->spanclass);
    assert(span->obj_count > 0 && "alloc span failed");

    DEBUGF("[runtime.mcentral_grow] success, spc=%d, base=%p, alloc_count=%lu, obj_count=%lu", span->spanclass,
           (void *) span->base,
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
    MDEBUGF("[cache_span] start, will lock, mcentral=%p, spc=%d", mcentral, mcentral->spanclass);
    mutex_lock(&mcentral->locker);

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
    MDEBUGF("[cache_span] span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu", span, (void *) span->base,
            span->spanclass,
            span->obj_count, span->alloc_count);

    assert(span && span->obj_count - span->alloc_count > 0 && "span unavailable");
    mutex_unlock(&mcentral->locker);
    MDEBUGF("[cache_span] success, unlocked mcentral=%p, span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu",
            mcentral, span,
            (void *) span->base, span->spanclass, span->obj_count, span->alloc_count);
    return span;
}

/**
 * 将 mspan 归还到 mcentral 队列中方便后续的清理
 * @param mcentral
 * @param span
 */
void uncache_span(mcentral_t *mcentral, mspan_t *span) {
    MDEBUGF(
            "[runtime.uncache_span] start, will lock mcentral=%p, span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu",
            mcentral, span,
            (void *) span->base, span->spanclass, span->obj_count, span->alloc_count);
    mutex_lock(&mcentral->locker);

    // 如果 span 还有空闲则丢到 partial 否则丢到 full
    if (span->obj_count - span->alloc_count > 0) {
        RT_LIST_PUSH_HEAD(mcentral->partial_list, span);
    } else {
        RT_LIST_PUSH_HEAD(mcentral->full_list, span);
    }

    mutex_unlock(&mcentral->locker);
    MDEBUGF("[runtime.uncache_span] success mcentral=%p, span=%p, base=%p, spc=%d, obj_count=%lu, alloc_count=%lu",
            mcentral, span,
            (void *) span->base, span->spanclass, span->obj_count, span->alloc_count);
}

/**
 * 从 mcentral 中找到一个可以使用的 span 给 mcache 使用
 * mcache.alloc[spanclass] 对应的 mspan 可能 null
 * @param mcache
 * @param spanclass
 * @return
 */
static mspan_t *mcache_refill(mcache_t *mcache, uint64_t spanclass) {
    DEBUGF("[mcache_refill] mcache=%p, spanclass=%lu start", mcache, spanclass);

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
    MDEBUGF("[mcache_refill] mcentral=%p, spanclass=%lu|%d, mspan_base=%p - %p, obj_size=%lu, alloc_count=%lu",
            mcentral, spanclass,
            mspan->spanclass, (void *) mspan->base, (void *) mspan->end, mspan->obj_size, mspan->alloc_count);

    return mspan;
}

/**
 * 从 spanclass 对应的 span 中找到一个 free 的 obj 并返回
 * @param spanclass
 * @return
 */
static addr_t mcache_alloc(uint8_t spanclass, mspan_t **span) {
    MDEBUGF("[runtime.mcache_alloc] start, spc=%d", spanclass);
    n_processor_t *p = processor_get();
    assert(p);

    mcache_t *mcache = &p->mcache;
    mspan_t *mspan = mcache->alloc[spanclass];

    if (mspan) {
        MDEBUGF("[mcache_alloc] p_index_%d=%d, span=%p, alloc_count=%lu, obj_count=%lu", p->share, p->index, mspan,
                mspan->alloc_count,
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
    int used_count = 0;
    for (uint64_t i = mspan->free_index; i < mspan->obj_count; i++) {
        bool used = bitmap_test(mspan->alloc_bits, i);
        if (used) {
            used_count += 1;
            // TDEBUGF("[runtime.mcache_alloc] obj_index=%d/%lu, used, continue", i, mspan->obj_count);
            continue;
        }

        // 找到了一个空闲的 obj 进行分配
        addr_t addr = mspan->base + i * mspan->obj_size;

        // 标记该节点已经被使用
        bitmap_set(mspan->alloc_bits, i);
        mspan->free_index = i + 1;
        mspan->alloc_count += 1;
        used_count += 1;

        mutex_unlock(&mspan->alloc_locker);
        MDEBUGF("[runtime.mcache_alloc] p_index_%d=%d, find can use addr=%p", p->share, p->index, (void *) addr);
        return addr;
    }

    assert(used_count == mspan->alloc_count);
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
    DEBUGF("[runtime.heap_arena_bits_set] addr=%p, size=%lu, obj_size=%lu, start", (void *) addr, size, obj_size);

    uint8_t *gc_bits = rtype->malloc_gc_bits;
    if (rtype->malloc_gc_bits == NULL) {
        gc_bits = (uint8_t *) &rtype->gc_bits;
    }

    bool arr_ptr = rtype->kind == TYPE_ARR && rtype->last_ptr > 0 && rtype->malloc_gc_bits == NULL;

    int index = 0;
    addr_t end = addr + obj_size;
    for (addr_t temp_addr = addr; temp_addr < end; temp_addr += POINTER_SIZE) {
        arena_t *arena = memory->mheap->arenas[arena_index(temp_addr)];
        assert(arena && "cannot find arena by addr");
        uint64_t bit_index = arena_bits_index(arena, temp_addr);

        DEBUGF("[runtime.heap_arena_bits_set] bit_index=%lu, temp_addr=%p, addr=%p, obj_size=%lu", bit_index,
               (void *) temp_addr,
               (void *) addr, obj_size);


        if (arr_ptr) {
            bitmap_set(arena->bits, bit_index);
        } else {
            int bit_value;
            if (bitmap_test(gc_bits, index)) {
                bitmap_set(arena->bits, bit_index);// 1 表示为指针
                bit_value = 1;
            } else {
                bitmap_clear(arena->bits, bit_index);
                bit_value = 0;
            }

            DEBUGF(
                    "[runtime.heap_arena_bits_set] rtype_kind=%s, size=%lu, scan_addr=0x%lx, temp_addr=0x%lx, bit_index=%ld, bit_value = % d ",
                    type_kind_str[rtype->kind], size, addr, temp_addr, bit_index, bit_value);
        }


        index += 1;
    }

    TRACEF("[runtime.heap_arena_bits_set] addr=%p, size=%lu, obj_size=%lu, unlock, end", (void *) addr, size, obj_size);
}

// 单位
static addr_t std_malloc(uint64_t size, rtype_t *rtype) {
    MDEBUGF("[std_malloc] start");
    assert(size > 0);
    bool has_ptr = rtype != NULL && rtype->last_ptr > 0;

    uint8_t sizeclass = calc_sizeclass(size);
    if (rtype && rtype->kind == TYPE_GC_FN) {
        sizeclass = JIT_SIZECLASS;
    }

    uint8_t spanclass = make_spanclass(sizeclass, !has_ptr);
    assert(sizeclass > 0 && spanclass > 1);


    MDEBUGF("[std_malloc] spanclass=%d", spanclass);

    mspan_t *span = NULL;
    addr_t addr = mcache_alloc(spanclass, &span);
    assert(span && "std_malloc notfound span");

    MDEBUGF("[std_malloc] mcache_alloc addr=%p", (void *) addr);

    // 对 arena.bits 做标记,标记是指针还是标量, has ptr 需要借助 arena bits 进行扫描
    if (has_ptr) {
        heap_arena_bits_set(addr, size, span->obj_size, rtype);
    }

    allocated_bytes += span->obj_size;

    char *debug_kind = "";
    if (rtype) {
        debug_kind = type_kind_str[rtype->kind];
    }
    DEBUGF(
            "[std_malloc] success, spc=%d,base=%p,alloc_count=%ld,size=%ld,kind=%s,addr=%p,alloc_bytes=%ld,gc_barrier=%d",
            span->spanclass, (void *) span->base, span->alloc_count,
            size, debug_kind, (void *) addr, allocated_bytes, gc_barrier_get());

    assert(span_of(addr) == span && "std_malloc span not match");

    return addr;
}

static addr_t large_malloc(uint64_t size, rtype_t *rtype) {
    bool has_ptr = rtype != NULL && rtype->last_ptr > 0;
    uint8_t spanclass = make_spanclass(0, !has_ptr);

    // 计算需要分配的 page count(向上取整)
    uint64_t pages_count = size / ALLOC_PAGE_SIZE;
    if ((size & ALLOC_PAGE_MASK) != 0) {
        pages_count += 1;
    }

    // 直接从堆中分配 span
    mspan_t *span = mheap_alloc_span(pages_count, spanclass);

    assert(span != NULL && "out of memory: large malloc");

    // 将 span 推送到 full swept 中，这样才能被 sweept
    mcentral_t *central = &memory->mheap->centrals[spanclass];
    mutex_lock(&central->locker);
    RT_LIST_PUSH_HEAD(central->full_list, span);
    mutex_unlock(&central->locker);

    // heap_arena_bits_set
    if (has_ptr) {
        heap_arena_bits_set(span->base, size, span->obj_size, rtype);
    }

    assert(span->obj_count == 1);
    bitmap_set(span->alloc_bits, 0);
    span->alloc_count += 1;

    allocated_bytes += span->obj_size;

    char *debug_kind = "";
    if (rtype) {
        debug_kind = type_kind_str[rtype->kind];
    }
    DEBUGF(
            "[runtime.large_malloc] success, spc=%d, span_base=%p, obj_size=%ld, need_size=%ld, type_kind=%s, "
            "addr=%p,alloc_count=%lu,alloc_bit=%d, allocator_bytes=%ld",
            span->spanclass, (void *) span->base, span->obj_size, size, debug_kind, (void *) span->base,
            span->alloc_count,
            bitmap_test(span->alloc_bits, 0), allocated_bytes);

    return span->base;
}

/**
 * @return
 */
arena_hint_t *arena_hints_init() {
    arena_hint_t *first = NEW(arena_hint_t);
    first->addr = (uint64_t) ARENA_HINT_BASE;
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
    RDEBUGF("[mheap_free_span] start, span->base=%p, pages_count=%lu, chunk_index=%lu", (void *) span->base,
            span->pages_count,
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
            remove_total_bytes / 1024 / 1024, (void *) span->base, span->pages_count,
            span->pages_count * ALLOC_PAGE_SIZE);

    sys_memory_remove((void *) span->base, span->pages_count * ALLOC_PAGE_SIZE);

    RDEBUGF("[mheap_free_span] sys_memory_remove success");
}

void memory_init() {
    memory = NEW(memory_t);
    memory->sweepgen = 0;
    memory->gc_count = 0;
    mutex_init(&memory->locker, false);

    // 初始化 gc 参数
    allocated_bytes = 0;
    next_gc_bytes = DEFAULT_NEXT_GC_BYTES;

    // - 初始化 mheap
    mheap_t *mheap = mallocz_big(sizeof(mheap_t));// 所有的结构体，数组初始化为 0, 指针初始化为 null
    mheap->page_alloc.summary[4] = mallocz_big(PAGE_SUMMARY_COUNT_L4 * sizeof(page_summary_t));
    mheap->page_alloc.summary[3] = mallocz_big(PAGE_SUMMARY_COUNT_L3 * sizeof(page_summary_t));
    mheap->page_alloc.summary[2] = mallocz_big(PAGE_SUMMARY_COUNT_L2 * sizeof(page_summary_t));
    mheap->page_alloc.summary[1] = mallocz_big(PAGE_SUMMARY_COUNT_L1 * sizeof(page_summary_t));
    mheap->page_alloc.summary[0] = mallocz_big(PAGE_SUMMARY_COUNT_L0 * sizeof(page_summary_t));// 8192

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
        mcentral_t *central = &mheap->centrals[i];
        central->spanclass = i;

        central->partial_list = NULL;
        central->full_list = NULL;
        mutex_init(&central->locker, false);
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
    callers_deserialize();
    DEBUGF("[memory_init] callers_deserialize success");
    symdefs_deserialize();
    DEBUGF("[memory_init] symdefs_deserialize success");
    rtypes_deserialize();
    DEBUGF("[memory_init] rtypes_deserialize success");

    memory->mheap = mheap;
}

mspan_t *span_of(addr_t addr) {
    if (addr <= 0) {
        return NULL;
    }
    // DEBUGF("[span_of] addr = %0lx", addr);
    // 根据 ptr 定位 arena, 找到具体的 page_index,
    arena_t *arena = take_arena(addr);
    if (arena == NULL) {
        TRACEF("[span_of] arena is null by addr=%p", (void *) addr);
        return NULL;
    }

    TRACEF("[span_of] addr=%p", (void *) addr);

    // 一个 arena 有 ARENA_PAGES_COUNT(8192 个 page), 根据 addr 定位 page_index
    uint64_t page_index = (addr - arena->base) / ALLOC_PAGE_SIZE;
    mspan_t *span = arena->spans[page_index];

    TRACEF("[span_of] page_index=%lu, span=%p", page_index, span);

    return span;
}

addr_t mstack_new(uint64_t size) {
    void *base = sys_memory_map((void *) 0x4000000000, size);

    return (addr_t) base;
}


/**
 * safe
 * @return
 */
void *rti_gc_malloc(uint64_t size, rtype_t *rtype) {
    uint64_t start = uv_hrtime();
    n_processor_t *p = processor_get();
    if (!p) {
        p = share_processor_index[0];
    }
    assert(p);

    // 不对，如果运行到一半需要锁怎么办, 每个 solo p 都应该有一个 stw locker 才行。
    if (!p->share) {
        MDEBUGF("[rti_gc_malloc] solo need gc_stw_locker p_index_%d=%d, co=%p", p->share, p->index, coroutine_get());
        mutex_lock(&p->gc_stw_locker);
    }

    MDEBUGF("[rti_gc_malloc] start p_index_%d=%d", p->share, p->index);

    if (rtype) {
        MDEBUGF("[rti_gc_malloc] size=%ld, type_kind=%s", size, type_kind_str[rtype->kind]);
    } else {
        MDEBUGF("[rti_gc_malloc] size=%ld, type is null", size);
    }

    void *ptr;
    if (size <= STD_MALLOC_LIMIT) {
        MDEBUGF("[rti_gc_malloc] std malloc");
        // 1. 标准内存分配(0~32KB)
        ptr = (void *) std_malloc(size, rtype);
    } else {
        MDEBUGF("[rti_gc_malloc] large malloc");
        // 2. 大型内存分配(大于>32KB)
        ptr = (void *) large_malloc(size, rtype);
    }

    // 如果当前写屏障开启，则新分配的对象都是黑色(不在工作队列且被 span 标记), 避免在本轮被 GC 清理
    if (gc_barrier_get()) {
        DEBUGF("[rti_gc_malloc] p_index_%d=%d(%lu), p_status=%d, gc barrier enabled, will mark ptr as black, ptr=%p",
               p->share, p->index,
               (uint64_t) p->thread_id, p->status, ptr);
        mark_ptr_black(ptr);
    }

    if (!p->share) {
        mutex_unlock(&p->gc_stw_locker);
    }

    MDEBUGF("[rti_gc_malloc] end p_index_%d=%d, co=%p, result=%p", p->share, p->index, coroutine_get(), ptr);

    // jit span 不用清 0， 权限不足也无法进行清零
    if (rtype && rtype->kind != TYPE_GC_FN) {
        memset(ptr, 0, size);
    }

    DEBUGF("[rti_gc_malloc] end success, size=%lu, use time: %lu, rtype: %p, has_ptr: %d", size, uv_hrtime() - start,
           rtype, rtype != NULL && rtype->last_ptr > 0);
    return ptr;
}

/**
 * @param base
 * @param pages_count
 * @param spanclass
 * @return
 */
mspan_t *mspan_new(uint64_t base, uint64_t pages_count, uint8_t spanclass) {
    assert(memory->locker.locker_count > memory->locker.unlocker_count);
    mspan_t *span = fixalloc_alloc(&memory->mheap->spanalloc);

    span->base = base;
    span->next = NULL;
    span->pages_count = pages_count;
    span->alloc_count = 0;
    span->free_index = 0;
    span->sweepgen = 0;
    span->spanclass = spanclass;
    uint8_t sizeclass = take_sizeclass(spanclass);
    if (sizeclass == LARGE_SIZECLASS) {
        // 使用 spanclass = 0 来管理 large_malloc
        span->obj_size = pages_count * ALLOC_PAGE_SIZE;
        span->obj_count = 1;
    } else {
        span->obj_size = class_obj_size[sizeclass];
        assert(span->obj_size > 0 && "span obj_size is zero");
        span->obj_count = span->pages_count * ALLOC_PAGE_SIZE / span->obj_size;
    }

    if (sizeclass == JIT_SIZECLASS) {
        // 为 darwin/arm64 设计

        // 数据清空
        memset((void *) base, 0, pages_count * ALLOC_PAGE_SIZE);

        if (mprotect((void *) base, pages_count * ALLOC_PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) == -1) {
            assertf(false, "mprotect failed, page_start=%p, size=%lu, err=%s", (void *) base,
                    pages_count * ALLOC_PAGE_SIZE,
                    strerror(errno));
        }
        DEBUGF("[mspan_new] jit span malloc and mprotect success, mspan=%p, mspan_lock=%p, base=%p, pages_size=%ld",
               span, &span->alloc_locker,
               (void *) span->base, pages_count * ALLOC_PAGE_SIZE);
    }

    span->end = span->base + (span->pages_count * ALLOC_PAGE_SIZE);
    mutex_init(&span->gcmark_locker, false);
    mutex_init(&span->alloc_locker, false);
    span->alloc_bits = gcbits_new(span->obj_count);

    // assert(bitmap_empty(span->alloc_bits, span->obj_count));

    span->gcmark_bits = gcbits_new(span->obj_count);

    DEBUGF("[mspan_new] success, base=%lx, pages_count=%lu, spc=%d, szc=%d, obj_size=%lu, obj_count=%lu", span->base,
           span->pages_count,
           span->spanclass, sizeclass, span->obj_size, span->obj_count);

    return span;
}

uint64_t runtime_malloc_bytes() {
    return allocated_bytes;
}

void runtime_eval_gc() {
    mutex_lock(&gc_stage_locker);

    if (gc_stage != GC_STAGE_OFF) {
        DEBUGF("[runtime_eval_gc] gc is running = %d, skip", gc_stage);
        goto EXIT;
    }

    if (allocated_bytes < next_gc_bytes) {
        goto EXIT;
    } else {
        DEBUGF("[runtime_eval_gc] will gc, because allocated_bytes=%ld > next_gc_bytes=%ld", allocated_bytes,
               next_gc_bytes);
        next_gc_bytes = allocated_bytes * NEXT_GC_FACTOR;
    }

    gc_stage = GC_STAGE_START;
    uv_thread_t runtime_gc_thread;
    uv_thread_create(&runtime_gc_thread, runtime_gc, NULL);

EXIT:
    mutex_unlock(&gc_stage_locker);
}

void runtime_force_gc() {
    if (mutex_trylock(&gc_stage_locker) != 0) {
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
    mutex_unlock(&gc_stage_locker);
    DEBUGF("[runtime_force_gc] end");
}

void *gc_malloc(uint64_t rhash) {
    // uint64_t start = uv_hrtime();
    PRE_RTCALL_HOOK();

    rtype_t *rtype = sc_map_get_64v(&rt_rtype_map, rhash);
    assertf(rtype, "notfound rtype by hash=%ld", rhash);

    // uint64_t stage2 = uv_hrtime();
    // TDEBUGF("[gc_malloc] rhash=%lu, malloc size is %lu, use time %lu ", rhash, rtype->size, stage2 - start);

    void *result = rti_gc_malloc(rtype->size, rtype);
    DEBUGF("[gc_malloc] size %lu, value %p", rtype->size, result);
    return result;
}

void *gc_malloc_size(uint64_t size) {
    // uint64_t start = uv_hrtime();

    PRE_RTCALL_HOOK();

    // uint64_t stage2 = uv_hrtime();
    // TDEBUGF("[gc_malloc_size] malloc size is %lu, use time %lu ", size, stage2 - start);

    void *result = rti_gc_malloc(size, NULL);
    return result;
}

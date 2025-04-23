#include "simd.h"


/**
 * 深度优先(右侧优先) 遍历，并标记 loop index/header
 * 深度优先有前序和后续遍历，此时采取后续遍历的方式来为 loop header 标号，
 * 这样可以保证所有的子循环都标号完毕后才标号当前 block
 */
static void loop_header_detect(closure_t *c, basic_block_t *current, basic_block_t *parent) {
    // 探测到循环，current is loop header,parent is loop end
    // current 可能被多个 loop ends 进入,所以这里不能收集 loop headers
    if (current->loop.active) {
        assert(current->loop.visited);
        assert(parent);

        current->loop.header = true;
        parent->loop.end = true;

        parent->backward_succ = current;

        assert(parent->succs->count == 1 && parent->succs->take[0] == current && "critical edge must broken");

        slice_push(current->loop_ends, parent);
        slice_push(c->loop_ends, parent);// 一个 header 可能对应多个 end
        return;
    }

    // increment count of incoming forward branches parent -> current is forward
    current->incoming_forward_count += 1;
    if (parent) {
        slice_push(parent->forward_succs, current);
    }

    if (current->loop.visited) {
        return;
    }

    // num block++
    current->loop.visited = true;
    current->loop.active = true;

    for (int i = current->succs->count - 1; i >= 0; --i) {
        basic_block_t *succ = current->succs->take[i];
        loop_header_detect(c, succ, current);
    }

    current->loop.active = false;

    // 后序操作(此时 current 可能在某次 backward succ 中作为 loop header 打上了标记)
    // 深度优先遍历加上 visited 机制，保证一个节点只会被 iteration
    if (current->loop.header) {
        assert(current->loop.index == -1);
        // 所有的内循环已经处理完毕了，所以外循环的编号总是大于内循环
        current->loop.index = c->loop_count++;
        slice_push(c->loop_headers, current);
    }
}

/**
 * 遍历所有 loop ends，找到这个 loop 下的所有 block 即可。
 * 如果一个 block 被多个 loop 经过，则 block index_list 的 key 就是 loop_index, value 就是是否被改 loop 穿过
 */
static void loop_mark(closure_t *c) {
    linked_t *work_list = linked_new();

    for (int i = 0; i < c->loop_ends->count; ++i) {
        basic_block_t *end = c->loop_ends->take[i];

        assert(end->succs->count == 1 && "critical edge must broken");

        basic_block_t *header = end->succs->take[0];
        assert(header->loop.header);
        assert(header->loop.index >= 0);
        int8_t loop_index = header->loop.index;

        linked_push(work_list, end);
        end->loop.index_map[loop_index] = true;

        do {
            basic_block_t *current = linked_pop(work_list);

            assert(current->loop.index_map[loop_index]);

            if (current == header) {
                continue;
            }

            // end -> preds -> preds -> header 之间的所有 block 都属于当前 index
            for (int j = 0; j < current->preds->count; ++j) {
                basic_block_t *pred = current->preds->take[j];
                if (pred->loop.index_map[loop_index]) {
                    // 已经配置过了，直接跳过
                    continue;
                }

                linked_push(work_list, pred);
                pred->loop.index_map[loop_index] = true;
            }
        } while (!linked_empty(work_list));
    }
}

/**
 * 遍历所有 block 分配 index 和 depth
 * @param c
 */
static void loop_assign_depth(closure_t *c) {
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        assert(block->loop.depth == 0);
        int max_depth = 0;
        int8_t min_index = -1;
        for (int j = c->loop_count - 1; j >= 0; --j) {
            if (!block->loop.index_map[j]) {
                continue;
            }

            // block 在 loop j 中
            max_depth++;
            min_index = j;
        }

        block->loop.index = min_index;
        block->loop.depth = max_depth;
    }
}

static void loop_detect(closure_t *c) {
    loop_header_detect(c, c->entry, NULL);
    loop_mark(c);
    loop_assign_depth(c);
}


/**
 * 分析循环中的内存访问模式
 */
static bool analyze_memory_access(basic_block_t *header) {

}

/**
 * 分析循环中的数据依赖
 */
static bool analyze_data_dependencies(basic_block_t *header) {
    // 检查循环中的数据依赖
    // 检查是否有循环携带依赖

    // 简单实现，返回 true 表示没有阻碍向量化的数据依赖
    return true;
}

/**
 * 检查循环是否包含可向量化的操作
 */
static bool has_vectorizable_operations(basic_block_t *header) {
    // 检查循环中是否包含可向量化的操作（如加法、乘法等）

    // 遍历循环体中的所有基本块
    for (int i = 0; i < header->loop_ends->count; i++) {
        basic_block_t *end = header->loop_ends->take[i];

        // 遍历从 header 到 end 的所有基本块
        // 检查每个基本块中的操作是否可向量化

        // 简单实现，假设所有操作都可向量化
    }

    return true;
}

/**
 * 识别可向量化的循环
 */
static void vector_detect(closure_t *c) {
    for (int i = 0; i < c->loop_headers->count; i++) {
        basic_block_t *header = c->loop_headers->take[i];

        // 检查循环是否满足向量化条件
        if (header->loop_ends->count == 1 &&      // 只有一个后继
            analyze_memory_access(header) &&      // 内存访问模式适合向量化
            analyze_data_dependencies(header) &&  // 没有阻碍向量化的数据依赖
            has_vectorizable_operations(header)) {// 包含可向量化的操作

            // 标记循环可向量化
            header->loop.vector = true;
        }
    }
}

static void vector_generate(closure_t *c, basic_block_t *header) {
}

/**
 * ssa 之后，reg alloc 之前, 基于 lir 实现向量优化
 */
void simd(closure_t *c) {
    // 1. 循环探测
    loop_detect(c);

    // 2.识别别可向量化的循环
    vector_detect(c);

    // 3. 生成向量化 LIR
    for (int i = 0; i < c->loop_headers->count; i++) {
        basic_block_t *header = c->loop_headers->take[i];
        if (header->loop.vector) {
            vector_generate(c, header);
        }
    }
}

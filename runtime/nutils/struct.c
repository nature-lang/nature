#include "struct.h"
#include "list.h"
#include "runtime/memory.h"
#include "utils/autobuf.h"

/**
 * @param rtype_hash
 * @return
 */
n_struct_t *struct_new(uint64_t rtype_hash) {
    rtype_t *rtype = rt_find_rtype(rtype_hash);
    // 参数 2 主要是去读其中的 gc_bits 记录 gc 相关数据
    return runtime_malloc(rtype->size, rtype);
}

/**
 * rtype 中也没有保存 struct 每个 key 的 offset, 所以需要编译时计算好 offset 传过来
 * access 操作可以直接解析成 lir_indrect_addr_t, 没必要再调用这里计算地址了
 * @param s
 * @return
 */
void *struct_access(n_struct_t *s, uint64_t offset) {
    return s + offset;
}

void struct_assign(n_struct_t *s, uint64_t offset, uint64_t property_index, void *property_ref) {
    void *p = s + offset;
    uint64_t size = rtype_out_size(rt_find_rtype(property_index), POINTER_SIZE);
    memmove(p, property_ref, size);
}

/**
 * 将 nature struct 转换成 c struct, 对于 list/struct 需要进入到内部进行展开。
 * 本质上其实就是计算 struct 在 c 语言中所用的空间
 * @param s
 * @param struct_rtype_hash
 */
void struct_to_buf(n_struct_t *s, uint64_t struct_rtype_hash, autobuf_t *buf) {
    rtype_t *rtype = rt_find_rtype(struct_rtype_hash);
    uint64_t offset = 0;
    for (int i = 0; i < rtype->element_count; ++i) {
        uint64_t hash = rtype->element_hashes[i];
        rtype_t *element_rtype = rt_find_rtype(hash);
        uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);


        if (element_rtype->kind == TYPE_STRUCT) {
            struct_to_buf(s + offset, element_rtype->hash, buf);
        } else if (element_rtype->kind == TYPE_LIST) {
            list_to_buf((n_list_t *) (s + offset), element_rtype->hash, buf);
        } else {
            // set/map/string c 语言都没有实现，所以都默认为 pointer, 不需要展开
            autobuf_push(buf, s + offset, element_size);
        }

        offset += element_size;
    }
}

void *struct_toc(n_struct_t *s, uint64_t struct_rtype_hash) {
    rtype_t *rtype = rt_find_rtype(struct_rtype_hash);
    if (rtype->element_count == 0) {
        DEBUGF("[runtime.struct_toc] rtype->element_count == 0\n");
        return NULL;
    }

    autobuf_t *buf = autobuf_new(rtype->size);
    DEBUGF("[runtime.struct_toc] struct->size: %lu\n", rtype->size);
    struct_to_buf(s, struct_rtype_hash, buf);
    assertf(buf->len > rtype->size, "buf->len: %lu, rtype->size: %lu\n", buf->len, rtype->size);

    void *p = runtime_malloc(buf->len, NULL);
    memmove(p, buf->data, buf->len);
    autobuf_free(buf);

    return p;
}



#include "coding.h"
#include "runtime/memory.h"

/**
 * 将 list 中的每一个元素都 push 到 buf 中，如果 list 的元素是 list/struct 则需要进行展开
 * @param l
 * @param rtype_hash
 * @param buf
 */
static void list_spread_to_buf(n_list_t *l, uint64_t rtype_hash, autobuf_t *buf) {
    DEBUGF("[runtime.list_spread_to_buf] l->length=%lu, l->cap=%lu, hash=%lu, buf->len=%lu", l->length, l->capacity,
           rtype_hash, buf->len);

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_hash);
    assertf(element_rtype, "cannot find element rtype by hash=%lu", l->element_rtype_hash);

    DEBUGF("[runtime.list_spread_to_buf] element->kind=%s", type_kind_string[element_rtype->kind])

    for (int i = 0; i < l->length; ++i) {
        uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);
        uint8_t *p = l->data + i * element_size;

        if (element_rtype->kind == TYPE_LIST) {
            // 此时 p 存储的是一个 pointer, 指向一个 list

            list_spread_to_buf((n_list_t *) fetch_addr_value((addr_t) p), element_rtype->hash, buf);
        } else if (element_rtype->kind == TYPE_STRUCT) {
            // 此时 p 存储的是一个 pointer, 指向一个 struct
            struct_spread_to_buf((n_struct_t *) fetch_addr_value((addr_t) p), element_rtype->hash, buf);
        } else {
            autobuf_push(buf, p, element_size);
        }
    }
}


/**
 * 将 nature struct 转换成 c struct, 对于 list/struct 需要进入到内部进行展开。
 * 需要注意一下 n_struct_t 的对齐问题
 * 本质上其实就是计算 struct 在 c 语言中所用的空间
 * @param s
 * @param struct_rtype_hash
 */
static void struct_spread_to_buf(n_struct_t *s, uint64_t struct_rtype_hash, autobuf_t *buf) {
    rtype_t *rtype = rt_find_rtype(struct_rtype_hash);

    DEBUGF("[runtime.struct_spread_to_buf] struct_rtype_hash: %lu, element_count: %hu", struct_rtype_hash,
           rtype->element_count);

    uint64_t offset = 0;
    for (int i = 0; i < rtype->element_count; ++i) {
        uint64_t hash = rtype->element_hashes[i];
        rtype_t *element_rtype = rt_find_rtype(hash);
        uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);

        // TODO 这决定了下一个元素再内存中的存放位置, c 和 nature 的对齐规则并不相同?
        // 尤其是启始的对齐位置, 所以需要两个 offset 分别计算 push 位置
        offset = align(offset, element_size);

        DEBUGF("[runtime.struct_spread_to_buf] element_rtype_kind=%s, element_rtype->hash=%lu, element_size=%lu, element_offset=%lu",
               type_kind_string[element_rtype->kind],
               element_rtype->hash,
               element_size,
               offset)


        if (element_rtype->kind == TYPE_STRUCT) {
            struct_spread_to_buf((n_struct_t *) fetch_addr_value((addr_t) s + offset), element_rtype->hash, buf);
        } else if (element_rtype->kind == TYPE_LIST) {
            // 读取 s + offset 中存储的值
            list_spread_to_buf((n_list_t *) fetch_addr_value((addr_t) s + offset), element_rtype->hash, buf);
        } else {
            // set/map/string c 语言都没有实现，所以都默认为 pointer, 不需要展开
            autobuf_push(buf, s + offset, element_size);
        }

        offset += element_size;
    }
}

static void *list_encode(n_list_t *l, uint64_t rtype_hash) {
    assertf(l, "first param list is null");
    assertf(rtype_hash > 0, "second param rtype_hash failed");

    if (l->length == 0) {
        DEBUGF("[list_toc] length == 0, return NULL")
        return NULL;
    }

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_hash);
    uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);
    DEBUGF("[list_toc] element_size=%lu, length=%lu", element_size, l->length);

    autobuf_t *buf = autobuf_new(l->length * element_size);
    list_spread_to_buf(l, rtype_hash, buf);
    void *p = runtime_malloc(buf->len, NULL);
    memmove(p, buf->data, buf->len);
    autobuf_free(buf);

    return p;
}

static void *struct_encode(n_struct_t *s, uint64_t struct_rtype_hash) {
    assertf(s, "first param struct is null");
    assertf(struct_rtype_hash > 0, "second param struct_rtype_hash failed");
    DEBUGF("[runtime.struct_toc] struct_rtype_hash: %lu\n", struct_rtype_hash);

    rtype_t *rtype = rt_find_rtype(struct_rtype_hash);
    if (rtype->element_count == 0) {
        DEBUGF("[runtime.struct_toc] rtype->element_count == 0\n");
        return NULL;
    }

    autobuf_t *buf = autobuf_new(rtype->size);
    DEBUGF("[runtime.struct_toc] struct->size: %lu\n", rtype->size);
    struct_spread_to_buf(s, struct_rtype_hash, buf);

    assertf(buf->len >= rtype->size, "buf->len: %lu, rtype->size: %lu\n", buf->len, rtype->size);
    DEBUGF("[runtime.struct_toc] buf->len: %lu\n", buf->len);

    void *p = runtime_malloc(buf->len, NULL);
    memmove(p, buf->data, buf->len);
    autobuf_free(buf);

    return p;
}

void *libc_encode(n_union_t *v) {
    assertf(v, "encode param exception");
    assertf(v->rtype, "v->rtype exception");
    if (v->rtype->kind == TYPE_STRUCT) {
        return struct_encode(v->value.ptr_value, v->rtype->hash);
    }

    if (v->rtype->kind == TYPE_LIST) {
        return list_encode(v->value.ptr_value, v->rtype->hash);
    }

    assertf(false, "not support type");
    exit(EXIT_FAILURE);
}

/**
 * Decode the encoded C struct pointed to by cptr and populate the n_struct_t structure.
 * @param cptr Pointer to the encoded C struct.
 * @param s Pointer to the n_struct_t structure to be populated.
 * @param struct_rtype Pointer to the rtype_t information for the struct.
 */
static uint64_t struct_decode(uint8_t *cptr, n_struct_t *s, rtype_t *struct_rtype) {
    assertf(cptr, "decode param exception");
    assertf(s, "decode param exception");
    assertf(struct_rtype, "decode param exception");

    // struct 的对齐, 需要
    uint64_t n_offset = 0;
    uint64_t c_offset = 0;
    for (int i = 0; i < struct_rtype->element_count; ++i) {
        uint64_t hash = struct_rtype->element_hashes[i];
        rtype_t *element_rtype = rt_find_rtype(hash);
        uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);

        uint8_t c_align = element_rtype->align;
        if (c_align == 0) {
            c_align == element_size;
        }

        uint64_t n_offset = align(n_offset, element_size);

        uint64_t new_c_offset = align(c_offset, c_align);

        // 现在移动 cptr 到合适的位置 (需要记录当前的位置值,才能继续做移动)
        cptr += (new_c_offset - c_offset);
        c_offset = new_c_offset;

        uint64_t mov_size = 0;
        if (element_rtype->kind == TYPE_STRUCT) {
            // 直接把 element 的对齐规则也写到 rtype 中?
            mov_size = struct_decode(cptr,
                                     (n_struct_t *) fetch_addr_value((addr_t) s + n_offset),
                                     element_rtype);
        } else if (element_rtype->kind == TYPE_LIST) {
//            cptr = list_decode((uint8_t *) cptr + offset,
//                        (n_list_t *) fetch_addr_value((addr_t) s + offset),
//                        element_rtype);
        } else {
            // For simple types, copy the data directly into the n_struct_t
            memmove((uint8_t *) s + n_offset, cptr, element_size);
            // cptr 使用了 element_size, 所以需要向后移动 element_size
            mov_size = element_size;
        }

        n_offset += element_size;
        c_offset += mov_size;
    }

    return c_offset;
}

void libc_decode(void *cptr, n_union_t *v) {
    assertf(v, "encode param exception");
    assertf(v->rtype, "v->rtype exception");

    // data 中已经存储了足够的数据, 但是依旧使用 copy 操作, 将数据 copy 到 v->value_casting 中

}

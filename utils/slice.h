#ifndef NATURE_SRC_LIB_SLICE_H_
#define NATURE_SRC_LIB_SLICE_H_

#define SLICE_TACK(_type, _slice, _index) ((_type*) _slice->take[_index])

#define SLICE_FOR(_slice, _type, _node) for (_type *_node = (_type*) _slice->take[0]; _node <= (_type*) _slice->take[_slice->count]; _node++)

/**
 * slice 是一个动态数组，存储的内容为数据指针,其内容存储依旧是在内存中的连续空间。
 * 当空间不足时，会将整个数组进行迁移
 */
typedef struct {
    int count; // 实际占用的元素数量
    int capacity; // 申请的内存容量
    void **take; // type take[count] = type *take = void* *take
} slice_t;

/**
 * 初始给 8 个大小
 * @return
 */
slice_t *slice_new();

void slice_push(slice_t *s, void *value);

void slice_append(slice_t *dst, slice_t *src);

void slice_append_free(slice_t *dst, slice_t *src);

#endif //NATURE_SRC_LIB_SLICE_H_

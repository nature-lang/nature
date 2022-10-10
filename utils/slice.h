#ifndef NATURE_SRC_LIB_SLICE_H_
#define NATURE_SRC_LIB_SLICE_H_

#define SLICE_TACK(_type, _slice, _index) ((_type*) _slice->take[_index])

#define SLICE_FOR(_slice, _type) for (_type *_node = (_type*) _slice->take[0]; _node <= (_type*) _slice->take[_slice->count]; _node++)
#define SLICE_VALUE() _node

//#define SLICE_FOR(_slice) for (int _i = 0; _i < (_slice)->count; ++_i)

//#define SLICE_VALUE(_slice) _slice->take[_i];


/**
 * slice 是一个动态数组，存储的内容为数据指针,其内容存储依旧是在内存中的连续空间。
 * 当空间不足时，会将整个数组进行迁移
 */
typedef struct {
    int count; // 实际占用的元素数量
    int capacity; // 申请的内存容量
    void **take; // code take[count] = code *take = void* *take
} slice_t;

/**
 * 初始给 8 个大小
 * @return
 */
slice_t *slice_new();

void slice_push(slice_t *s, void *value);

void slice_append(slice_t *dst, slice_t *src);

void slice_append_free(slice_t *dst, slice_t *src);

void *slice_remove(slice_t *s, int i);

#endif //NATURE_SRC_LIB_SLICE_H_

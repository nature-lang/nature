#include "array.h"
#include "runtime/processor.h"

n_void_ptr_t array_element_addr(n_array_t *data, uint64_t rtype_hash, uint64_t index) {
    PRE_RTCALL_HOOK();
    ASSERT_ADDR(data);

    DEBUGF("[runtime.array_element_addr] array=%p, rtype_hash=%lu, index=%lu", data, rtype_hash, index);

    rtype_t *array_rtype = rt_find_rtype(rtype_hash);

    if (index >= array_rtype->length) {
        char *msg = dsprintf("index out of array [%d] with length %d", index, array_rtype->length);
        DEBUGF("[runtime.array_element_addr] has err %s", msg);
        rt_coroutine_set_error(msg, true);
        return 0;
    }

    // element_size = size / length
    uint64_t element_size = array_rtype->size / array_rtype->length;
    uint64_t offset = element_size * index;

    n_void_ptr_t result = (n_void_ptr_t) data + offset;

    DEBUGF("[runtime.array_element_addr] result=%lx, element_size=%ld, offset=%ld, rtype_size=%lu, rtype_length=%d",
           result, element_size, offset, array_rtype->size, array_rtype->length);

    return result;
}

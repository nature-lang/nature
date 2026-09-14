// Allocator-backed X-mode vec storage is owned by the caller-provided allocator.
// The allocator is never stored in the vec header, so its ownership stays explicit.
import allocator.types.{allocatable}
import reflect.{vec_t}
import libc

pub const VEC_DEFAULT_CAPACITY = 8

// GC-owned vectors can cross from .n into .x. Their existing push operation
// remains available, while allocator-owned vectors use push_alloc below.
#linkid rt_vec_push
fn vec_gc_push(anyptr list, int element_hash, anyptr val)

pub fn vec<T>.alloc(allocatable a, T default_value, int len):vec<T> {
    if len < 0 {
        panic('len must be greater than 0')
    }

    var temp = vec_t{
        data: 0,
        length: len,
        capacity: len,
        element_size: @sizeof(T),
        hash: @reflect_hash(T),
    }

    if len == 0 {
        return temp as anyptr as vec<T>
    }

    temp.data = a.alloc(len * @sizeof(T))
    var result = temp as anyptr as vec<T>
    for i in 0..len {
        result[i] = default_value
    }

    return result
}

pub fn vec<T>.grow_alloc(*self, allocatable a) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>

    if rv.capacity > 0 {
        rv.capacity *= 2
    } else {
        rv.capacity = VEC_DEFAULT_CAPACITY
    }

    anyptr old_data = rv.data
    rv.data = a.alloc(rv.capacity * @sizeof(T))

    if rv.length > 0 {
        libc.memmove(rv.data, old_data, rv.length as u64 * @sizeof(T))
    }
    if old_data != 0 {
        a.dealloc(old_data)
    }
}

pub fn vec<T>.push(*self, T v) {
    ptr<T> ref = &v
    int element_hash = @reflect_hash(T)
    return vec_gc_push(self as anyptr, element_hash, ref as anyptr)
}

pub fn vec<T>.push_alloc(*self, allocatable a, T v) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>
    if rv.length == rv.capacity {
        self.grow_alloc(a)
    }

    // check index and assign
    var index = rv.length
    rv.length += 1
    var offset = (@sizeof(T) * index) as anyptr
    anyptr p = rv.data + offset
    libc.memmove(p, &v as anyptr, @sizeof(T))
}

pub fn vec<T>.deinit(*self, allocatable a) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>
    if rv.data != 0 {
        a.dealloc(rv.data)
    }
    rv.data = 0
    rv.length = 0
    rv.capacity = 0
}

pub fn vec<T>.len(*self):int {
    var rv = self as anyptr as ptr<vec_t>
    return rv.length
}

pub fn vec<T>.cap(*self):int {
    var rv = self as anyptr as ptr<vec_t>
    return rv.capacity
}

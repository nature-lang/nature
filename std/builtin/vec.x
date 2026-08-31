// x mode part of vec: the allocator backed constructor and the methods that only touch the
// vec_t header or the allocator behind it. an allocator backed vec never takes the gc branch,
// the allocator == 0 guard keeps that path for gc owned vecs built from .n.
import allocator.types.{allocatable}
import allocator as ac
import reflect.{vec_t}
import libc

pub const VEC_DEFAULT_CAPACITY = 8

// the gc push path is only reachable for a gc owned vec, which x code never builds.
// binding the one symbol here keeps std/runtime out of x mode entirely.
#linkid rt_vec_push
fn vec_gc_push(anyptr list, int element_hash, anyptr val)

pub fn vec<T>.alloc(allocatable a, T default_value, int len):vec<T> {
    if len < 0 {
        panic('len must be greater than 0')
    }

    int cap = len

    var ap = ac.new<allocatable>(a)

    var temp = vec_t{
        data: 0,
        length: len,
        capacity: cap,
        element_size: @sizeof(T),
        hash: @reflect_hash(T),
        allocator: ap as anyptr,
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

pub fn vec<T>.grow(*self) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>
    assert(rv.allocator > 0)

    if rv.capacity > 0 {
        rv.capacity *= 2
    } else {
        rv.capacity = VEC_DEFAULT_CAPACITY
    }

    anyptr old_data = rv.data
    allocatable a = *(rv.allocator as ptr<allocatable>)
    rv.data = a.alloc(rv.capacity * @sizeof(T))

    if rv.length > 0 {
        libc.memmove(rv.data, old_data, rv.length as u64 * @sizeof(T))
    }
    a.dealloc(old_data)
}

pub fn vec<T>.push(*self, T v) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>
    if rv.allocator == 0 {
        ptr<T> ref = &v
        int element_hash = @reflect_hash(T)
        return vec_gc_push(self as anyptr, element_hash, ref as anyptr)
    }

    if rv.length == rv.capacity {
        self.grow()
    }

    // check index and assign
    var index = rv.length
    rv.length += 1
    var offset = (@sizeof(T) * index) as anyptr
    anyptr p = rv.data + offset
    libc.memmove(p, &v as anyptr, @sizeof(T))
}

pub fn vec<T>.deinit(*self) {
    ptr<vec_t> rv = self as anyptr as ptr<vec_t>
    if rv.allocator == 0 {
        return
    }

    allocatable a = *(rv.allocator as ptr<allocatable>)
    a.dealloc(rv.allocator)
    a.dealloc(rv.data)
}

pub fn vec<T>.len(*self):int {
    var rv = self as anyptr as ptr<vec_t>
    return rv.length
}

pub fn vec<T>.cap(*self):int {
    var rv = self as anyptr as ptr<vec_t>
    return rv.capacity
}

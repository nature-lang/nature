// x mode part of string: the allocator backed constructor and the methods that only touch the
// string header or the allocator behind it. a literal or a gc owned string from .n has
// allocator == 0, so deinit leaves it alone.
import allocator.types.{allocatable}
import reflect.{vec_t}
import libc

#linkid rt_string_length
pub fn string.len(*self):int

#linkid rt_string_ref
pub fn string.ref(*self):anyptr

pub fn string.char(*self):u8 {
    return (*self)[0]
}

// copy len bytes from data into a string owned by a, NUL-terminated like every string
pub fn string.alloc(allocatable a, anyptr data, int len):string {
    if len < 0 {
        panic('len must be greater than 0')
    }

    // the header has room for a pointer only, so the allocator is boxed, in memory from the allocator
    // itself: deinit frees the box through it, and an arena releases it together with the bytes
    ptr<allocatable> ap = a.alloc(@sizeof(allocatable)) as ptr<allocatable>
    *ap = a

    var temp = vec_t{
        data: a.alloc(len + 1),
        length: len,
        capacity: len + 1,
        element_size: 1,
        hash: @reflect_hash(string),
        allocator: ap as anyptr,
    }

    if len > 0 {
        libc.memmove(temp.data, data, len as u64)
    }

    // an arena does not zero its memory
    ptr<u8> end = (temp.data + len as anyptr) as ptr<u8>
    *end = 0

    return temp as anyptr as string
}

pub fn string.deinit(*self) {
    ptr<vec_t> rs = self as anyptr as ptr<vec_t>
    if rs.allocator == 0 {
        return
    }

    allocatable a = *(rs.allocator as ptr<allocatable>)
    a.dealloc(rs.allocator)
    a.dealloc(rs.data)
}

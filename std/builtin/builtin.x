// x mode declarations: the bound C symbols do no gc alloc and touch no coroutine,
// so they stay callable from .x sources. .n sources can call them as usual.
#linkid print
pub fn print(...[any] args)

#linkid println
pub fn println(...[any] args)

#linkid rt_panic
pub fn panic(string msg)

#linkid rt_assert
pub fn assert(bool cond)

// the panic of an index or slice, a map access or an `as` that a catch right after it gets as an error
errset INDEX_OUT_OF_RANGE
errset KEY_NOT_FOUND
errset TYPE_ASSERT_FAILED

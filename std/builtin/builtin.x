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
errdef INDEX_OUT_OF_RANGE 'index out of range'
errdef KEY_NOT_FOUND 'key not found in map'
errdef TYPE_ASSERT_FAILED 'type assert failed'

// the message of an errdef, or its name when it has none
#linkid rt_errset_msg
pub fn errset_t.msg(self):string

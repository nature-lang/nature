// x mode declarations: the bound C symbols do no gc alloc and touch no coroutine,
// so they stay callable from .x sources. .n sources can call them as usual.
#linkid print
pub fn print(...[any] args)

#linkid println
pub fn println(...[any] args)

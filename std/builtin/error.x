// The error a .x catch binds. It is a pointer to an immutable descriptor the compiler emitted
// into .data at the throw site: [i32 len][i32 flags][bytes][NUL]. Nothing is allocated, nothing
// is copied, and it outlives every frame, so a handler can hold it as long as it likes.
//
// error.n is untouched: throwable, errort and errorf stay there, so .n keeps its own error type
// and custom .n implementations of throwable keep working.
// The compiler lowers T! in .x to the tagged errable<T> variants value(T) and
// error(ptr<errdesc>). It synthesizes that specialization because error.n's source-level
// errable<T> intentionally describes the different, coroutine-backed .n representation.
pub type errdesc = struct {
    i32 len
    i32 flags
}
// builds a non owning string view over the descriptor's inline bytes, no allocation
#linkid rt_x_errdesc_msg
pub fn errdesc.msg(*self): string

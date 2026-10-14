# [builtin](https://github.com/nature-lang/nature/blob/master/std/builtin/builtin.n)

The builtin contains global functions and types that don't need to be imported.

## type nullable

```
type nullable<T> = T?
```

Nullable type that can hold a value of type T or null.

## fn print

```
fn print(...[any] args)
```

Print arguments to standard output without newline.

## fn println

```
fn println(...[any] args)
```

Print arguments to standard output with newline.

## fn panic

```
fn panic(string msg)
```

Panic with the given message and terminate the program.

## fn assert

```
fn assert(bool cond)
```

Assert that the condition is true, panic if false.

## errdef INDEX_OUT_OF_RANGE, KEY_NOT_FOUND, TYPE_ASSERT_FAILED

```
errdef INDEX_OUT_OF_RANGE
errdef KEY_NOT_FOUND
errdef TYPE_ASSERT_FAILED
```

An index or slice out of range, a missing map key and a failed `as` panic. A catch right after such an expression gets the panic as one of these errsets instead, in `.x` and `.n`:

```nature
var a = list[5] catch e {
    // e == INDEX_OUT_OF_RANGE
    -1
}
```

Only the expression the catch follows is covered: in `list[list[5]] catch e { ... }` the inner index still panics. A panic inside a called function, `panic()` and `assert()` always end the program.

# [chan](https://github.com/nature-lang/nature/blob/master/std/builtin/chan.n)

## fn chan<T>.new

```
fn chan<T>.new(...[int] args):chan<T>
```

Create a new channel with optional buffer size.

## type chan

A send, receive or close on a closed channel fails with the builtin `errdef CHAN_CLOSED`.

### chan.send

```
fn chan<T>.send(self, T msg):void!
```

Send a message to the channel, blocks if channel is full.

### chan.try_send

```
fn chan<T>.try_send(self, T msg):bool!
```

Try to send a message to the channel without blocking.

### chan.on_send

```
fn chan<T>.on_send(self, T msg):void
```

Event handler for channel send operations.

### chan.recv

```
fn chan<T>.recv(self):T!
```

Receive a message from the channel, blocks if channel is empty.

### chan.on_recv

```
fn chan<T>.on_recv(self):T
```

Event handler for channel receive operations.

### chan.try_recv

```
fn chan<T>.try_recv(self):(T, bool)!
```

Try to receive a message from the channel without blocking.

### chan.close

```
fn chan<T>.close(self):void!
```

Close the channel.

### chan.is_closed

```
fn chan<T>.is_closed(self):bool
```

Check if the channel is closed.

### chan.is_successful

```
fn chan<T>.is_successful(self):bool
```

Check if the last operation was successful.

# [coroutine](https://github.com/nature-lang/nature/blob/master/std/builtin/coroutine.n)

## type future_t

```
type future_t<T> = struct{
    i64 size
    ptr<T> result
    errset_t error
    anyptr co
}
```

Future type for asynchronous operations.

### future_t.await

```
fn future_t.await():T!
```

Wait for the future to complete and return the result.

### future_t.await (void)

```
fn future_t<T:void>.await():void!
```

Wait for the future to complete (void return type).

## fn async

```
fn async<T>(fn():void! function, int flag):ref<future_t<T>>
```

Execute a function asynchronously and return a future.

## fn co_return

```
fn co_return<T>(ptr<T> result)
```

Return a result from a coroutine.

# [error](https://github.com/nature-lang/nature/blob/master/std/builtin/error.n)

## type errable

```
type errable<T,E> = union{
    T value
    E error
}
```

A function that returns `errable<T,E>` is errable, in `.x` and `.n`: `return` gives a `T`, `throw` gives an `E`, and the error comes back in the return value. `T!` is shorthand for `errable<T,errset_t>`, and a `T!` function bound to C returns it too. A `!` after `errable<T,E>` is rejected.

An error propagates only into a function that fails with the same type. A call that fails with another type has to be caught and converted, which the compiler checks:

```nature
// in .x
errdef BAD

fn parse(string s):errable<int,string> {
    if s == '' {
        throw 'empty input'
    }
    return 1
}

fn load(string s):int! {
    return parse(s) catch msg {
        throw BAD
    }
}
```

A catch after a call of a function that returns `errable<T,E>` handles only that call's error, and `e` has its error type. The calls in its arguments are not covered: their errors propagate as usual and the compiler checks their error types, so in `check(parse(1)) catch e { ... }` only `check` is caught. Nothing else reaches a catch. `go` runs a function that fails with an errset; await fails with the errset the coroutine failed with.

## errdef

```nature
// in the io module
errdef TIMEOUT
errdef CLOSED

pub fn read(int mode):int! {
    if mode == 1 {
        throw TIMEOUT
    }
    return 42
}

// elsewhere
import "io.x"

fn main() {
    var n = io.read(1) catch e {
        if e == io.TIMEOUT {
            println('timeout')
        }
        -1
    }
}
```

An `errdef` declaration registers a named error, an errset. Its identity is the declaration itself: the compiler gives it a program-wide id, so `io.TIMEOUT` and `fmt.TIMEOUT` are different errors even though they share a name. An errset carries no message and no data, so it is compared with `==` and `!=`, or matched with `match e { TIMEOUT -> ... _ -> ... }`. The set of errsets is open, so a `match` needs a `_` arm.

Declarations are module-level only and always public, so `pub` is not needed; a `pub` before `errdef` is accepted and ignored. An errset value has the builtin type `errset_t` and is stored as a `u32` id at run time. Only an errset name produces an `errset_t` value: neither an integer literal nor a `u32` value converts to it, so `throw 5` and comparing `u32 a = 12` with an errset are type errors. `print`, `println` and `%v` in `fmt` print an `errset_t` as its name, such as `io.EOF`. An error nothing catches ends the program with its name and the trace of the functions it passed through:

```
coroutine 'main' uncaught error: io.NOT_FOUND at main.n:5:42
stack backtrace:
0:	main.load
		at main.n:5:42
1:	main.main
		at main.n:9:9
```

# [map](https://github.com/nature-lang/nature/blob/master/std/builtin/map.n)

## fn map<T,U>.new

```
fn map<T,U>.new():map<T,U>
```

Create a new map with key type T and value type U.

## type map

### map.len

```
fn map<T,U>.len(self):int
```

Get the number of key-value pairs in the map.

### map.del

```
fn map<T,U>.del(self, T key)
```

Delete a key-value pair from the map.

### map.contains

```
fn map<T,U>.contains(self, T key):bool
```

Check if the map contains the given key.

# [set](https://github.com/nature-lang/nature/blob/master/std/builtin/set.n)

## fn set<T>.new

```
fn set<T>.new():set<T>
```

Create a new set with element type T.

## type set

### set.add

```
fn set<T>.add(self, T key)
```

Add an element to the set.

### set.contains

```
fn set<T>.contains(self, T key):bool
```

Check if the set contains the given element.

### set.del

```
fn set<T>.del(self, T key)
```

Remove an element from the set.

# [string](https://github.com/nature-lang/nature/blob/master/std/builtin/string.n)

## type string

### string.len

```
fn string.len(self):int
```

Get the length of the string.

### string.ref

```
fn string.ref(self):anyptr
```

Get a pointer to the string data.

### string.char

```
fn string.char(self):u8
```

Get the first character of the string.

# [vec](https://github.com/nature-lang/nature/blob/master/std/builtin/vec.n)

## fn vec<T>.new

```
fn vec<T>.new(T value, int len):vec<T>
```

Create a new vector with initial value and length.

## fn vec<T>.cap_of

```
fn vec<T>.cap_of(int cap):vec<T>
```

Create a new vector with specified capacity.

## type vec

### vec.push

```
fn vec<T>.push(self, T v)
```

Add an element to the end of the vector.

### vec.append

```
fn vec<T>.append(self, vec<T> l2)
```

Append another vector to this vector.

### vec.slice

```
fn vec<T>.slice(self, int start, int end):vec<T>
```

Create a slice of the vector from start to end.

### vec.concat

```
fn vec<T>.concat(self, vec<T> l2):vec<T>
```

Concatenate two vectors and return a new vector.

### vec.copy

```
fn vec<T>.copy(self, vec<T> src):int
```

Copy elements from source vector to this vector.

### vec.len

```
fn vec<T>.len(self):int
```

Get the number of elements in the vector.

### vec<T>.cap_of

```
fn vec<T>.cap(self):int
```

Get the capacity of the vector.

### vec.ref

```
fn vec<T>.ref(self):anyptr
```

Get a pointer to the vector data.

### vec.sort

```
fn vec<T>.sort(self, fn(int, int):bool less)
```

Sort the vector using the provided comparison function.

### vec.search

```
fn vec<T>.search(self, fn(int):bool predicate):int
```

Binary search in the vector using a predicate function.

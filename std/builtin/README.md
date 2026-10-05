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

# [chan](https://github.com/nature-lang/nature/blob/master/std/builtin/chan.n)

## fn chan<T>.new

```
fn chan<T>.new(...[int] args):chan<T>
```

Create a new channel with optional buffer size.

## type chan

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
    errort error = runtime_error_t.FAILED
    bool has_error
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
fn future_t<T>.await_void(&self):void!
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

## type errort

```nature
pub type errort = interface {}

pub type errable<value_t, error_t> = union {
    value_t value
    error_t error
}
```

`errort` is a compiler-recognized marker interface. Types implementing it can be returned by `T!`, which means `errable<T,errort>`. Both `.n` and `.x` use the same tagged Result return model.

A default error stores an 8-byte type identity and a 24-byte inline payload. Enums, scalar aliases, small structs and pointers are supported; types must explicitly implement `errort`. Packing, throwing, propagating and catching do not allocate an error wrapper or call `msg()`. Constructing the payload itself follows the normal memory rules of its type.

### Enum errors

```nature
pub type error_t:errort = enum { TIMEOUT, CLOSED }
fn load():int! { throw error_t.TIMEOUT }
fn main() {
    var value = load() catch e {
        if e == error_t.TIMEOUT { println('timeout') }
        if e is error_t { var kind = e as error_t }
        -1
    }
}
```

### Concrete error types

`errable<T,E>` accepts any concrete E, including strings and structs larger than 24 bytes. Calls support catch, throw and automatic propagation; the catch variable has type E. A zero error value remains distinct from success.

```nature
fn parse():errable<int,string> { throw 'invalid input' }
fn main() {
    var value = parse() catch e { println(e); -1 }
}
```

Concrete errors propagate only into compatible E types. Propagation into T! additionally requires the marker implementation and inline capacity. A catch containing different error types promotes to errort only if every type satisfies these requirements. Catchable runtime checks, such as bounds checks, produce runtime_error_t.

### Runtime errors and native interfaces

`runtime_error_t:errort` defines runtime categories. `system_error_t:errort` stores an i32 code for errno or native library status. Standard library modules expose `error_t` enums or small structs containing the required context.

Native `.n` #linkid T! declarations retain the C return-T ABI. At the boundary the compiler drains the native error slot and converts it into the new error representation. Nature-to-Nature calls use Result returns. Native `.x` interfaces must explicitly declare errable<T,E> and return its matching tagged C structure; native T! declarations requiring a coroutine error slot are rejected.

Type identities are executable-local, not stable serialized or cross-library error numbers. Uncaught errors produce numeric diagnostics and a nonzero exit status. Applications can map errors to readable text explicitly in Nature catch blocks.

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

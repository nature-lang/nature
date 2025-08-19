# import [co](https://github.com/nature-lang/nature/tree/master/std/co/main.n)

Coroutine library for concurrent programming and cooperative multitasking.

## var SAME

```
var SAME = 1 << 2
```

Coroutine scheduling flag.

## fn sleep

```
fn sleep(int ms)
```

Suspend current coroutine for specified milliseconds.

## fn yield

```
fn yield()
```

Yield control to other coroutines.

## fn arg

```
fn arg():anyptr
```

Get argument passed to current coroutine.

# import [co.mutex](https://github.com/nature-lang/nature/tree/master/std/co/mutex.n)

Mutex implementation for coroutine synchronization.

## type mutex_t

```
type mutex_t = struct {
    i64 state
    i64 sema
    i64 waiter_count
    var waiters = types.linkco_list_t{}
}
```

Mutex for protecting shared resources in coroutine environment.

### mutex_t.lock

```
fn mutex_t.lock()
```

Acquire the mutex lock, blocking if necessary.

### mutex_t.try_lock

```
fn mutex_t.try_lock():bool
```

Try to acquire the mutex lock without blocking.

### mutex_t.unlock

```
fn mutex_t.unlock()
```

Release the mutex lock.
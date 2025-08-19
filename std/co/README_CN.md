# import [co](https://github.com/nature-lang/nature/tree/master/std/co/main.n)

协程库，用于并发编程和协作式多任务处理。

## var SAME

```
var SAME = 1 << 2
```

协程调度标志。

## fn sleep

```
fn sleep(int ms)
```

挂起当前协程指定的毫秒数。

## fn yield

```
fn yield()
```

让出控制权给其他协程。

## fn arg

```
fn arg():anyptr
```

获取传递给当前协程的参数。

# import [co.mutex](https://github.com/nature-lang/nature/tree/master/std/co/mutex.n)

用于协程同步的互斥锁实现。

## type mutex_t

```
type mutex_t = struct {
    i64 state
    i64 sema
    i64 waiter_count
    var waiters = types.linkco_list_t{}
}
```

用于在协程环境中保护共享资源的互斥锁。

### mutex_t.lock

```
fn mutex_t.lock()
```

获取互斥锁，必要时阻塞。

### mutex_t.try_lock

```
fn mutex_t.try_lock():bool
```

尝试获取互斥锁而不阻塞。

### mutex_t.unlock

```
fn mutex_t.unlock()
```

释放互斥锁。
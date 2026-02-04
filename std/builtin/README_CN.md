# [builtin](https://github.com/nature-lang/nature/blob/master/std/builtin/builtin.n)

builtin 中包含的都是全局函数和类型，不需要 import 引入

## type nullable

```
type nullable<T> = T?
```

可空类型，可以保存类型 T 的值或 null。

## fn print

```
fn print(...[any] args)
```

将参数打印到标准输出，不换行。

## fn println

```
fn println(...[any] args)
```

将参数打印到标准输出，并换行。

## fn panic

```
fn panic(string msg)
```

使用给定消息触发 panic 并终止程序。

## fn assert

```
fn assert(bool cond)
```

断言条件为真，如果为假则 panic。

# [chan](https://github.com/nature-lang/nature/blob/master/std/builtin/chan.n)

## fn chan_new

```
fn chan_new<T>(...[int] args):chan<T>
```

创建新的通道，可选缓冲区大小。

## type chan

### chan.send

```
fn chan<T>.send(T msg):void!
```

向通道发送消息，如果通道已满则阻塞。

### chan.try_send

```
fn chan<T>.try_send(T msg):bool!
```

尝试向通道发送消息，不阻塞。

### chan.on_send

```
fn chan<T>.on_send(T msg):void
```

通道发送操作的事件处理器。

### chan.recv

```
fn chan<T>.recv():T!
```

从通道接收消息，如果通道为空则阻塞。

### chan.on_recv

```
fn chan<T>.on_recv():T
```

通道接收操作的事件处理器。

### chan.try_recv

```
fn chan<T>.try_recv():(T, bool)!
```

尝试从通道接收消息，不阻塞。

### chan.close

```
fn chan<T>.close():void!
```

关闭通道。

### chan.is_closed

```
fn chan<T>.is_closed():bool
```

检查通道是否已关闭。

### chan.is_successful

```
fn chan<T>.is_successful():bool
```

检查上次操作是否成功。

# [coroutine](https://github.com/nature-lang/nature/blob/master/std/builtin/coroutine.n)

## type future_t

```
type future_t<T> = struct{
    i64 size
    ptr<T> result
    throwable? error
    anyptr co
}
```

异步操作的 Future 类型。

### future_t.await

```
fn future_t<T>.await():T!
```

等待 future 完成并返回结果。

### future_t.await (void)

```
fn future_t<T:void>.await():void!
```

等待 future 完成（void 返回类型）。

## fn async

```
fn async<T>(fn():void! function, int flag):ref<future_t<T>>
```

异步执行函数并返回 future。

## fn co_return

```
fn co_return<T>(ptr<T> result)
```

从协程返回结果。

# [error](https://github.com/nature-lang/nature/blob/master/std/builtin/error.n)

## type throwable

```
type throwable = interface{
    fn msg():string
}
```

可抛出对象的接口。

## type errort

```
type errort:throwable = struct{
    string message
    bool is_panic
}
```

实现 throwable 接口的错误类型。

### errort.msg

```
fn errort.msg():string
```

获取错误消息。

## type errable

```
type errable<T> = errort|T
```

用于错误处理的联合类型。

## fn errorf

```
fn errorf(string format, ...[any] args):ref<errort>
```

创建格式化错误。

# [map](https://github.com/nature-lang/nature/blob/master/std/builtin/map.n)

## fn map_new

```
fn map_new<T,U>():map<T,U>
```

创建键类型为 T、值类型为 U 的新映射。

## type map

### map.len

```
fn map<T,U>.len():int
```

获取映射中键值对的数量。

### map.del

```
fn map<T,U>.del(T key)
```

从映射中删除键值对。

### map.contains

```
fn map<T,U>.contains(T key):bool
```

检查映射是否包含给定键。

# [set](https://github.com/nature-lang/nature/blob/master/std/builtin/set.n)

## fn set_new

```
fn set_new<T>():set<T>
```

创建元素类型为 T 的新集合。

## type set

### set.add

```
fn set<T>.add(T key)
```

向集合添加元素。

### set.contains

```
fn set<T>.contains(T key):bool
```

检查集合是否包含给定元素。

### set.del

```
fn set<T>.del(T key)
```

从集合中移除元素。

# [string](https://github.com/nature-lang/nature/blob/master/std/builtin/string.n)

## type string

### string.len

```
fn string.len():int
```

获取字符串长度。

### string.ref

```
fn string.ref():anyptr
```

获取字符串数据的指针。

### string.char

```
fn string.char():u8
```

获取字符串的第一个字符。

# [vec](https://github.com/nature-lang/nature/blob/master/std/builtin/vec.n)

## fn vec_new

```
fn vec_new<T>(T value, int len):vec<T>
```

创建具有初始值和长度的新向量。

## fn vec_cap

```
fn vec_cap<T>(int cap):vec<T>
```

创建具有指定容量的新向量。

## type vec

### vec.push

```
fn vec<T>.push(T v)
```

向向量末尾添加元素。

### vec.append

```
fn vec<T>.append(vec<T> l2)
```

将另一个向量追加到此向量。

### vec.slice

```
fn vec<T>.slice(int start, int end):vec<T>
```

创建从 start 到 end 的向量切片。

### vec.concat

```
fn vec<T>.concat(vec<T> l2):vec<T>
```

连接两个向量并返回新向量。

### vec.copy

```
fn vec<T>.copy(vec<T> src):int
```

从源向量复制元素到此向量。

### vec.len

```
fn vec<T>.len():int
```

获取向量中元素的数量。

### vec.cap

```
fn vec<T>.cap():int
```

获取向量的容量。

### vec.ref

```
fn vec<T>.ref():anyptr
```

获取向量数据的指针。

### vec.sort

```
fn vec<T>.sort(fn(int, int):bool less)
```

使用提供的比较函数对向量排序。

### vec.search

```
fn vec<T>.search(fn(int):bool predicate):int
```

使用谓词函数在向量中进行二分搜索。
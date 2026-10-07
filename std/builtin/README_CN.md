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

## fn chan<T>.new

```
fn chan<T>.new(...[int] args):chan<T>
```

创建新的通道，可选缓冲区大小。

## type chan

### chan.send

```
fn chan<T>.send(self, T msg):void!
```

向通道发送消息，如果通道已满则阻塞。

### chan.try_send

```
fn chan<T>.try_send(self, T msg):bool!
```

尝试向通道发送消息，不阻塞。

### chan.on_send

```
fn chan<T>.on_send(self, T msg):void
```

通道发送操作的事件处理器。

### chan.recv

```
fn chan<T>.recv(self):T!
```

从通道接收消息，如果通道为空则阻塞。

### chan.on_recv

```
fn chan<T>.on_recv(self):T
```

通道接收操作的事件处理器。

### chan.try_recv

```
fn chan<T>.try_recv(self):(T, bool)!
```

尝试从通道接收消息，不阻塞。

### chan.close

```
fn chan<T>.close(self):void!
```

关闭通道。

### chan.is_closed

```
fn chan<T>.is_closed(self):bool
```

检查通道是否已关闭。

### chan.is_successful

```
fn chan<T>.is_successful(self):bool
```

检查上次操作是否成功。

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

异步操作的 Future 类型。

### future_t.await

```
fn future_t.await():T!
```

等待 future 完成并返回结果。

### future_t.await (void)

```
fn future_t<T>.await_void(&self):void!
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

## type errort

```nature
pub type errort = interface {}

pub type errable<value_t, error_t> = union {
    value_t value
    error_t error
}
```

`errort` 是编译器识别的标记接口。实现它的类型可用于默认错误返回 `T!`，即 `errable<T,errort>`。`.n` 和 `.x` 使用同一个带标签的返回值模型。

默认错误包含现有的 8 字节 `rtype_hash` 字段和 24 字节内联数据。enum、标量别名、小结构体和指针可以作为错误；类型必须显式声明实现 `errort`。不同类型作为内联错误使用时，若类型 hash 碰撞，编译器会报错。转换、抛出、传播和捕获错误不会为错误包装分配堆内存，也不调用 `msg()`。错误数据自身的构造仍遵循普通类型的内存规则。

### enum 错误

```nature
pub type error_t:errort = enum {
    TIMEOUT,
    CLOSED,
}

fn load():int! {
    throw error_t.TIMEOUT
}

fn main() {
    var value = load() catch e {
        if e == error_t.TIMEOUT { println('timeout') }
        if e is error_t { var kind = e as error_t }
        -1
    }
}
```

### 具体错误类型

`errable<T,E>` 允许任意具体 `E`，包括字符串和超过 24 字节的结构体。调用时仍可用 `catch`、`throw` 和自动传播；catch 变量的类型是 `E`，错误值为零也不会与成功混淆。

```nature
fn parse():errable<int,string> {
    throw 'invalid input'
}

fn main() {
    var value = parse() catch e {
        println(e)
        -1
    }
}
```

具体错误只能向兼容的 `E` 传播。若要传播到 `T!`，它必须实现 `errort` 且满足内联大小限制。一个 try/catch 中出现不同错误类型时，只能在全部满足此条件时提升为 `errort`。可捕获的运行时检查（如数组越界）产生 `runtime_error_t`。

### 运行时错误和原生接口

`runtime_error_t:errort` 是内置错误分类 enum；`system_error_t:errort` 包含 `i32 code`，保存 errno 或原生库状态码。标准库也通过各自公开的 `error_t` enum 或小结构体保存分类和必要的上下文。

`.n` 和 `.x` 的原生 `#linkid` 接口也使用同一返回 ABI：`T!` 返回 `errable<T,errort>`，显式 `errable<T,E>` 返回对应的带标签 union。C 函数必须返回匹配的 tag 和 value/error 布局。错误通过返回值传递；异步回调在各自的操作上下文中记录状态，协程恢复后由原生函数返回结果。coroutine 不保存通用错误槽。

类型 hash 是编译器元数据，不应作为序列化协议或跨编译器版本的 ABI 约定。未捕获的错误输出数值诊断并以非零状态退出；需要可读文本时，由 Nature 调用方在 catch 中自行映射。

# [map](https://github.com/nature-lang/nature/blob/master/std/builtin/map.n)

## fn map<T,U>.new

```
fn map<T,U>.new():map<T,U>
```

创建键类型为 T、值类型为 U 的新映射。

## type map

### map.len

```
fn map<T,U>.len(self):int
```

获取映射中键值对的数量。

### map.del

```
fn map<T,U>.del(self, T key)
```

从映射中删除键值对。

### map.contains

```
fn map<T,U>.contains(self, T key):bool
```

检查映射是否包含给定键。

# [set](https://github.com/nature-lang/nature/blob/master/std/builtin/set.n)

## fn set<T>.new

```
fn set<T>.new():set<T>
```

创建元素类型为 T 的新集合。

## type set

### set.add

```
fn set<T>.add(self, T key)
```

向集合添加元素。

### set.contains

```
fn set<T>.contains(self, T key):bool
```

检查集合是否包含给定元素。

### set.del

```
fn set<T>.del(self, T key)
```

从集合中移除元素。

# [string](https://github.com/nature-lang/nature/blob/master/std/builtin/string.n)

## type string

### string.len

```
fn string.len(self):int
```

获取字符串长度。

### string.ref

```
fn string.ref(self):anyptr
```

获取字符串数据的指针。

### string.char

```
fn string.char(self):u8
```

获取字符串的第一个字符。

# [vec](https://github.com/nature-lang/nature/blob/master/std/builtin/vec.n)

## fn vec<T>.new

```
fn vec<T>.new(T value, int len):vec<T>
```

创建具有初始值和长度的新向量。

## fn vec<T>.cap_of

```
fn vec<T>.cap_of(int cap):vec<T>
```

创建具有指定容量的新向量。

## type vec

### vec.push

```
fn vec<T>.push(self, T v)
```

向向量末尾添加元素。

### vec.append

```
fn vec<T>.append(self, vec<T> l2)
```

将另一个向量追加到此向量。

### vec.slice

```
fn vec<T>.slice(self, int start, int end):vec<T>
```

创建从 start 到 end 的向量切片。

### vec.concat

```
fn vec<T>.concat(self, vec<T> l2):vec<T>
```

连接两个向量并返回新向量。

### vec.copy

```
fn vec<T>.copy(self, vec<T> src):int
```

从源向量复制元素到此向量。

### vec.len

```
fn vec<T>.len(self):int
```

获取向量中元素的数量。

### vec<T>.cap_of

```
fn vec<T>.cap(self):int
```

获取向量的容量。

### vec.ref

```
fn vec<T>.ref(self):anyptr
```

获取向量数据的指针。

### vec.sort

```
fn vec<T>.sort(self, fn(int, int):bool less)
```

使用提供的比较函数对向量排序。

### vec.search

```
fn vec<T>.search(self, fn(int):bool predicate):int
```

使用谓词函数在向量中进行二分搜索。

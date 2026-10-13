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

## errset INDEX_OUT_OF_RANGE、KEY_NOT_FOUND、TYPE_ASSERT_FAILED

```
errset INDEX_OUT_OF_RANGE
errset KEY_NOT_FOUND
errset TYPE_ASSERT_FAILED
```

索引或切片越界、map 里没有这个 key、`as` 失败都会 panic。catch 紧跟在这类表达式后面时，会把 panic 当作其中一个 errset 接住，`.x` 和 `.n` 都一样：

```nature
var a = list[5] catch e {
    // e == INDEX_OUT_OF_RANGE
    -1
}
```

只有 catch 紧跟的那个表达式算数：`list[list[5]] catch e { ... }` 里内层的越界照样 panic。被调函数内部的 panic、`panic()` 和 `assert()` 总是结束程序。

# [chan](https://github.com/nature-lang/nature/blob/master/std/builtin/chan.n)

## fn chan<T>.new

```
fn chan<T>.new(...[int] args):chan<T>
```

创建新的通道，可选缓冲区大小。

## type chan

在已关闭的通道上发送、接收或再次关闭，会以内置的 `errset CHAN_CLOSED` 出错。

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
    errset_t error
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

## type errable

```
type errable<T,E> = union{
    T value
    E error
}
```

返回 `errable<T,E>` 的函数是可出错函数，`.x` 和 `.n` 都支持：`return` 给出 `T`，`throw` 给出 `E`，错误通过返回值带回。`T!` 是 `errable<T,errset_t>` 的简写，绑定到 C 的 `T!` 函数也返回它。`errable<T,E>` 后面再写 `!` 会被拒绝。

错误只能传播给错误类型相同的函数。错误类型不同的调用必须 catch 后转换，编译器会检查：

```nature
// .x 中
errset BAD

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

跟在返回 `errable<T,E>` 的函数调用后面的 catch 只处理这个调用的错误，`e` 的类型就是它的错误类型。参数中的调用不在范围内：它们的错误照常向外传播，错误类型由编译器检查，所以 `check(parse(1)) catch e { ... }` 只处理 `check`。其他情况都不会进入 catch。`go` 运行以 errset 出错的函数，await 以协程的 errset 出错。

## errset

```nature
// 在 io 模块中
errset TIMEOUT
errset CLOSED

pub fn read(int mode):int! {
    if mode == 1 {
        throw TIMEOUT
    }
    return 42
}

// 其他模块
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

`errset` 声明注册一个具名错误。它的身份就是这条声明本身：编译器为其分配全程序唯一的 id，因此 `io.TIMEOUT` 与 `fmt.TIMEOUT` 即使同名也是两个不同的错误。errset 不携带消息和数据，可以用 `==`、`!=` 比较，也可以用 `match e { TIMEOUT -> ... _ -> ... }` 分支。errset 的集合是开放的，所以 `match` 必须有 `_` 分支。

声明只能出现在模块顶层，且始终对外公开，不需要写 `pub`；写了 `pub` 也会被忽略。errset 的值类型是内置的 `errset_t`，运行时存成 `u32` 的 id。只有 errset 名字能产生 `errset_t` 的值：整数字面量和 `u32` 的值都不能转换为它，所以 `throw 5`、把 `u32 a = 12` 和 errset 比较都是类型错误。`print`、`println` 和 `fmt` 的 `%v` 把 `errset_t` 打印成它的名字，比如 `io.EOF`。没有被 catch 的错误会结束程序，并打印错误名和它经过的函数：

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

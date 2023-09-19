<p align="center"><a href="https://nature-lang.org" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>  


# nature 编程语言

nature 是下一代系统级编程语言和编译器，语法上追求简洁优雅，关注使用者的编写与阅读体验。

在编程语言层面，nature 具有以下特点:

- 类型系统，null 安全，泛型，联合类型
- 自研编译器/汇编器/链接器，不依赖 llvm。能够编译至 amd64/riscv64/wasm 架构
- 无入侵的方式与 C 语言等交互进行高效率与高性能开发
- 渐进式 GC，支持自动与手动 GC
- 内置 vec/map/set/tup 数据结构
- 包管理与模块管理
- 函数标签/闭包/错误提示/运行时堆栈追踪/协程
- 集成SSA/线性扫描寄存器分配/反射机制/汇编器与连接器

随着标准库以逐步完善，nature 可以应用于游戏引擎与游戏制作、科学与 AI 计算、操作系统与物联网、WEB 开发。其中游戏引擎将作为 nature 0.7.0 ~ 1.0+ 的核心任务。

nature 适合于开源创作者/独立创作者以及学习和研究使用，我们不仅希望你能够在语言中得到便利，同样也希望你使用 nature 创作快乐且有趣的事情。

通过官方网站，您可以获得更多信息以及它的文档。

官网: [https://nature-lang.org](https://nature-lang.org/)

文档: [https://nature-lang.org/docs/getting-started/hello-world](https://nature-lang.org/docs/getting-started/hello-world)

> ❗️ 当前版本 0.4.0-beta，未集成 riscv64 wasm 架构编译/手动 GC/函数标签/协程，其余功能已经集成完毕。**nature 即将发布社区可用版本(0.7.0)，邀请大家进行先行测试建议与贡献**

## ⚙️ 安装

从 [releases](https://github.com/nature-lang/nature/releases) 中下载并解压 natrue 安装包。推荐将解压后的 nature 文件夹移动到 `/usr/local/` 下，并将 `/usr/local/nature/bin` 目录加入到系统环境变量。

创建一个 main.n 文件，写入以下内容

```rust  
import fmt

fn fib(int n):int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fmt.printf('fib result is %d', fib(30))
```  

编译并执行

```bash  
> nature build main.n && ./main  
fib result is 832040
```  

使用 docker 集成环境快速编译并执行

```shell  
docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'  
```  

## 🌱 发布计划

nature 版本号遵循[语义化版本](https://semver.org/)，️其中 0.1 ~ 1.0 包含上下两个部分

上半部分总是携带 beta 标识，表示生产不可用。

下半部分则具有稳定且向下兼容的语法 api，此时 nature 可以用于个人的独立/开源项目，但不提供 LTS 版本。

1.0 版本发布时，nature 将正式用于开源/商业项目使用，且具有 LTS 版本。

| 版本号      | 内容                      | 预计发布时间 |  
| ----------- | ------------------------- | ------------ |  
| v0.1.0-beta | 基础语法版本发布          | 2023-05      |  
| v0.2.0-beta | 类型系统/基础语法完善     | 2023-07      |  
| v0.3.0-beta | 包管理/基础语法完善       | 2023-09      |  
| v0.4.0-beta | 小型测试用例/基础标准库   | 2023-11      |  
| v0.5.0-beta | lsp 开发/核心语法完善     | 2024-02      |  
| v0.6.0-beta | 中型测试用例/ bug 修复    | 2024-04      |  
| v0.7.0      | 大型测试用例/稳定语法 api | 2024-07      |  
| v0.8.0+     | 正式版本发布相关准备工作  | 2024-09      |  
| v1.0.0      | 正式版本发布              | 2025-        |  

当前版本: 0.4.0-beta，当前还在规划的核心功能，将会在后续的版本中逐步集成。

- switch/try 等关键语法集成及优化
- wasm 架构编译
- 协程功能支持
- darwin 系统编译
- 函数标签功能支持
- 渐进式 GC 完善
- riscv 架构编译
- windows 系统编译


## 🧭 设计理念

编程语言中存在一等公民的说法，比如 javascript 中的一等公民是函数，虽然 nature 中的函数也能够作为值传递等高阶用法，但是 nature 的一等公民并不是函数。那对于 nature 来说最重要的是什么呢

编译速度？运行速度？安全性？简洁性？都不是。即使我们追求简洁之道，但是绝对不会为此而牺牲开发者的使用便利。

所以对于 nature 来说最重要的一定是开发者，为开发者带来便利，让开发者看起来顺眼，写起来顺手。当然这不代表 nature 不具备编译速度/运行速度/安全性/简洁性等等特性，我们致力于协调便利性与这些常见特性的平衡。但是一旦遇到不可协调的冲突时，则优先考虑的是开发者的便利性，而不是编译速度/复杂度/简洁性等等。

例如在单引号和双引号字符串的选择中，虽然绝大多数强类型语言选择了双引号作为标准字符串，但是单引号能够减少一次 shift 的输入，减轻开发者的小拇指负担。同时让阅读更加简洁。关于取消 if 和 for 中的括号同样如此。

例如在 nature 中很少去发明新语法糖，而是尽可能在其他语言中选择已经存在的众所周知的语法糖，从而减轻开发者的学习与心理负担。包括关键字的缩写也应该选择众所周知的缩写。如 i8 代替 int8_t，fn 代替 func/function，fn 在键盘中是常见的按键，并且 rust 已经对 fn/i8 关键字进行了广泛的传播，所以能够极大的避免理解歧义与学习负担。


## 🍺 贡献指南

有多种方式参与 nature 的贡献，包括但不限于提交 BUG、分享想法、社区讨论、编码参与、文档改进、规范制定、资料贡献、捐赠等。

nature 基于 ANSI C11 和 musl libc 进行开发。源码上追求简单可读，不使用复杂的第三方库，文件名/目录名/关键字都采用小写下划线分词，唯一的例外是宏定义使用大写下划线分词。

源码目录结构/编译/相关资料参考 [https://nature-lang.org/docs/prologue/contribution-guide](https://nature-lang.org/docs/prologue/contribution-guide)

natrue 所有的标准库贡献通过后将会合并至主仓库。在进行功能开发前请先通过 issue 进行提前沟通与 api 设计。

## 🐳 社区交流

想法和问题推荐使用 github issue 进行讨论让更多人能够关注并参与。

微信群: 添加微信号 `nature-lang` 备注 “申请加群”

github 讨论社区: [https://github.com/nature-lang/nature/discussions](https://github.com/nature-lang/nature/discussions)


## 🍼 编码示例

## 🍼 编码示例


错误处理

```rust
type test = struct {
    [i8] list
    var div = fn(int a, int b):int {
        if b == 0 {
            throw 'divisor cannot zero'
        }
        return a / b
    }
}

var t = test {
    list = [1, 2, 3, 4, 5]
}

var (l, err) = try foo.list[8]
if err.has {
    println("chain access list err=", err.msg)
}

var (_, err) = try foo.div(10, 0)
if err.has {
    println("div error", err.msg)
}
```


泛型

```rust
// generic fn
type numbert = gen i8|i16|i32|i64|u8|u16|u32|u64|f32|f64

fn sum(numbert a, numbert b):numbert {
    return a + b
}
fn cmp(numbert a, numbert b):bool {
    return a > b
}

// type param
type box<t> = struct {
    t width
    t length
    var area = fn(self s):t {
        return s.width * s.length
    }
}

fn run() {
    var b = box<i8> {
        width = 5,
        length = 10
    }
    println('self area=', b.area())
}
```


联合类型

```rust
type nullable<t> = t|null

nullable<i8> foo = 24
if foo is null {
    // logic...
    return
}

// x println(foo + 12), foo is a union type, cannot use binary

let foo as i8
println(foo + 12)
```

函数标签

```java
@local @retry=5 
@test 24, 10 -> 4
@test -5, 10 -> -5
fn rem(int dividend, int divisor):int {
    if divisor == 0 {
        throw 'divisor cannot be zero'
    }
    return dividend % divisor
}

@global @post increase_views
fn read_blog():int {
    // logic ...
}

@comment Based on label prompt + test for automatic code generation testing
@prompt sum up a and b
@test 12, 13 -> 25
@test -5, 10 -> 5
fn sum(int a, int b):int
```

http server

```js
import http
import http.router
import http.resp

var app = http.server()

router.get('/', fn(ctx):resp {
    return resp.string('hello world')
})

app.use(router).listen('127.0.0.1', 8000)
```

更多编码示例 👉 [cases](https://github.com/nature-lang/nature/tree/master/tests/blackbox/cases)
## 📌 FAQ


1.natrue 采用类型前置还是后置？

nature 统一采用类型前置，包括函数的返回值类型。一个原始设计示例

`fn sum(int a, int b):int c` 可以看到函数的返回值同样采用类型前置。将 ident 全部省略可以得到函数的类型声明 `fn(int,int):int f` ，通常在函数的定义上返回值的 ident 也需要省略，可以得到正式的函数声明 `fn sum(int a, int b):int {}`

2.nature/logo 的含义？

logo 是一艘星际飞船，意为《三体》中的自然选择号，nature 也来源于此。

3.为什么没有性能测试与比较？

nature 目前还在 beta 版本进行核心功能的开发，没有对编译器后端进行任何优化。所以做性能测试是不公平且没有意义的。

4.nature 开发了多久？

当前主仓库已经开发了近 3 年，实际投入的时间则接近 6 年左右。我想说的是 nature 项目并不会被随意的放弃，并且会富有生命力的持续开发并维护下去。

## 🪶 执照

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

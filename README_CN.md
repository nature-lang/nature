
# nature 编程语言
  
通用系统型编程语言与编译器，期望用**简洁优雅**的方式构建高性能、安全可靠、跨平台软件。

## 特性

- 轻量、简洁、一致性的语法设计，轻松掌握并快速上手使用
- 强类型、静态分析与编译、内存安全、异常处理，轻松编写安全可靠的软件
- 内置并发原语 go/future/channel/select
- 直接编译为目标平台的机器码，不依赖 LLVM，并支持交叉编译
- 部署简单，高效编译，基于 musl libc 进行静态链接，具备良好的跨平台特性
- 完善的类型系统，支持泛型、联合类型、interface、nullable(?)、errable(!)
- 高性能 GC 实现，具有非常短暂的 STW (Stop The World)
- 高性能内存分配器实现，参考 tcmalloc
- 高性能共享栈协程实现，每秒能够进行数百万次的协程切换
- 基于 libuv 实现的高性能 IO
- 纯 C 实现的高性能 runtime 和编译器
- 内置数据结构 vec/map/set/tup 和常用标准库实现
- 函数调用遵守 system ABI，内置 libc，无性能损耗调用 c 标准库函数
- 集中式包管理系统 npkg
- 编辑器 lsp 支持
  
## 概况

nature 编程语言已经达到早期可用版本，语法 API 基本稳定，在 1.0 版本之前不会有大幅的变化，后续版本会添加一些必要的语法，如 enum，三元运算符，struct label 等。

待完成的关键特性有可控内存分配器，LLM 编码适配，DSL 测试框架，GUI 适配，WASM3.0 适配。

当前版本编译目标架构包含 linux_amd64、linux_arm64、linux_riscv64、darwin_amd64、darwin_arm64。

nature 包含一组测试用例和标准库用来测试基本功能和语法的可用性，包含一组中小型项目测试整体可用性，还未经过大型的项目测试。
 
官网 https://nature-lang.cn


## 安装  
  
从 [releases](https://github.com/nature-lang/nature/releases) 中下载并解压 nature 安装包(注意权限是否正确)。将解压后的 nature 文件夹移动到 `/usr/local/` 下，并将 `/usr/local/nature/bin` 目录加入到系统环境变量。 

> 如果需要安装到其他目录中需要手动设置 NATURE_ROOT 环境变量到对应目录

运行 `nature -v` 命令查看版本，运行 `nature -h` 命令查看帮助

```sh
> nature -v
nature vx.x.x - release build 1970-01-01
```

创建一个 main.n 文件  
  
```js  
import fmt  
  
fn main() {  
    fmt.printf('hello nature')
}  
```

编译并执行 
  
```sh  
> nature build main.n && ./main  

hello nature
```
  
## 文档  

快速开始 https://nature-lang.org/docs/get-started

语法文档 https://nature-lang.org/docs/syntax

标准库文档 https://nature-lang.org/stds/co

在线试用 https://nature-lang.org/playground


## 示例

mysql/postgresql/redis 驱动 https://github.com/weiwenhao/dbdriver

sqlite 驱动 https://github.com/weiwenhao/nature-sqlite

API 框架 https://github.com/weiwenhao/emoji-api

轻量级容器打包工具与运行时 https://github.com/weiwenhao/parker

Llama2 推理模型实现 https://github.com/weiwenhao/llama.n

基于 raylib 实现的俄罗斯方块 https://github.com/weiwenhao/tetris

更多语法示例 https://github.com/nature-lang/nature/tree/master/tests/features/cases

## 设计理念

nature 编程语言是一款轻量简单，易于学习的编程语言，在设计理念和 runtime 架构上参考了 golang。

nature 在语法设计上注重内存安全，有着完善的类型系统支持以及方便的错误处理方式，以文件作为 module 单位，采用基于 package.toml 的集中式包管理方式。

nature 原生支持并发原语 go+select+channel，在高并发 IO 应用上有着非常优秀的并发表现。正式版本发布时性能将会得到进一步提升。

nature 有着完全自研的编译器、汇编器、链接器，这让 nature 更加灵活可控，源码简单且没有复杂的第三方依赖，可以轻松地参与贡献，并根据语言和技术发展进行高度定制与优化。

得益于简单的语法设计、自动化内存管理、编译时静态分析等特性，带来了极低的编码负担，使得 nature 编程语言非常适合 AI 编码及编程新手使用。

nature 作为通用编程语言，基于现有的语言特性和标准库实现，可以用于 WEB 开发、命令行程序、数据库、网络中间件、容器系统、IOT 设备、编程教学、操作系统、游戏引擎与游戏开发等各种领域。

## 基准测试

https://github.com/nature-lang/benchmark

Linux Ubuntu 虚拟机（内核版本 6.17.8，aarch64 架构, Mac M4 芯片, 9 核, 8G）

**IO: HTTP Server**

`ab -n 100000 -c 1000 http://127.0.0.1:8888/`

| Language | Version | QPS (req/sec) | Mean Response Time |
|----------|---------|---------------|---------------------|
| Nature | v0.7.2  | ~104,000 | 9 ms |
| Go | 1.25.5  | ~90,000 | 11 ms |
| Node.js | v25.2.0 | ~66,000 | 14 ms |

**Call: Fibonacci(45) time consumed**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nature | v0.7.2  | ~2.4 s    |
| Go | 1.25.5  | ~2.4 s    |
| Rust | 1.92.0  | ~1.7 s    |
| Node.js | v25.2.0 | ~6.0 s    |

**CPU: Calculate 1 billion times π**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nature | v0.7.2  | ~512 ms   |
| Go | 1.25.5  | ~512 ms   |
| Rust | 1.92.0  | ~552 ms   |
| Node.js | v25.2.0 | ~850 ms   |

**FFI: Calling 100 million c fn sqrt time consumed**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nature | v0.7.2  | ~73 ms |
| Go | 1.25.5  | ~2178 ms |

**Coroutine: 1 Million Coroutine Time/Memory consumed**

| Language | Version | Creation Time | Dispatch Time | Peak Memory |
|----------|---------|---------------|---------------|-------------|
| Nature | v0.7.2  | ~559 ms | ~589 ms       | ~969 MB |
| Go | 1.25.5  | ~1035 ms | ~1047 ms      | ~2580 MB |


## 贡献指南

部署指南 https://github.com/nature-lang/nature/blob/master/DEVELOPMENT.md

贡献指南 https://nature-lang.org/docs/contribute

待办事项  https://github.com/nature-lang/nature/issues/131


## License  
  
本项目的编译器前端、后端、runtime 等项目源码受 Apache License (Version 2.0) 保护。当前项目中包含的 nature 源码文件(.n) 如标准库等使用 MIT 许可证。
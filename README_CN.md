
# nature 编程语言

通用开源编程语言与编译器，期望为开发者提供**简洁优雅**的开发体验，能够**简单高效**构建出安全可靠的跨平台软件。

## 特性

- ✓ 简洁、自然、一致性的语法设计，即使编程初学者也能够轻松掌握，快速上手
- ✓ 不依赖 llvm 和虚拟机，直接编译为目标平台机器码，并支持交叉编译
- ✓ 部署简单，基于 musl libc 进行纯静态链接，无额外依赖，具备良好的跨平台特性
- ✓ 完善的类型系统，支持泛型、联合类型、接口、空值安全等
- ✓ 和 go 一样的高性能 GC 实现，具有非常短暂的 STW (Stop The World）
- ✓ 和 go 一样的高性能内存分配器实现，即 google/tcmalloc
- ✓ 高性能共享栈协程实现，每秒能够进行数百万次的协程切换
- ✓ 内置 libuv 与协程协作处理 IO 事件循环
- ✓ 基于纯 C 实现的高性能 runtime 和编译器
- ✓ 模块化与包管理系统 npkg
- ✓ 内置常用数据结构及标准库实现
- ✓ 使用 try+catch 进行错误处理，match 进行模式匹配、channel 进行协程通信、select 进行并发处理
- ✓ 遵循系统 ABI，内置 libc，可以直接调用 C 语言标准库函数加速代码开发
- ✓ 编辑器 lsp 支持
- ○ 高性能内存管理方式协助自动 GC
- ○ 测试 DSL，希望能够高效稳定利用 AI 编码
- ○ macho 跨平台连接器(目前缺少 macho 链接器导致 darwin 平台无法进行交叉编译)
- ○ 协作式调度系统完善
- ○ 跨平台编译支持 wasm、risc64 平台
- ○ 编译为可读的 go 编程语言

## 项目概况

nature 编程语言已经达到早期可用版本，语法 API 基本稳定，在 1.0 版本之前不会有大幅的变化。

当前版本 nature 源码支持编译的目标平台有
- linux/amd64
- linux/arm64
- darwin/amd64
- darwin/arm64

nature 包含一组测试用例及标准库用来测试基本功能和语法的可用性，包含一组小型项目测试整体可用性，还未经过中大型的项目测试。

后续的核心工作是提升 nature 编程语言的可用性，包括标准库完善、性能优化、收集用户反馈以及 bug 修复。


官网: [https://nature-lang.org](https://nature-lang.org)


## 设计理念

go 是我日常工作使用的编程语言，其语法简单，非常方便的进行交叉编译以及部署，拥有非常优秀且高性能的 runtime 实现，拥有先进的并发设计风格。但也有一些不方便的地方

- 语法过于简洁导致表达能力不足
- 类型系统不够完善
- 错误处理机制繁琐
- 自动 GC 和抢占式调度的设计虽然非常优秀，但是也让 go 的应用范围受限。
- 包管理方式
- interface{}
- ...

nature 在设计理念上是对 go 编程语言的延续与改进，并追寻一定的差异性。在改善上述问题的同时，nature 拥有和 go 类似的 runtime、GMP 模型、allocator、collector、coroutine、channel、std 等。并且 nature 同样不依赖 llvm，以及高效的编译速度，方便的交叉编译与部署等。

基于 nature 编程语言的现有特性，其适用于游戏引擎和游戏开发、科学计算和 AI、操作系统和物联网、命令行、以及 Web 开发等领域。

## 安装

从 [releases](https://github.com/nature-lang/nature/releases) 中下载并解压 natrue 安装包(注意权限是否正确)。将解压后的 nature 文件夹移动到 `/usr/local/` 下，并将 `/usr/local/nature/bin` 目录加入到系统环境变量。

> 如果需要安装到其他目录中需要手动设置 NATURE_ROOT 环境变量到对应目录

运行 `nature -v` 命令查看版本，运行 `nature -h` 命令查看帮助

```sh
> nature -v
nature v0.5.0 - release build 2025-05-01
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


---

## 文档

快速开始 [https://nature-lang.org/docs/get-started](https://nature-lang.org/docs/get-started)

语法文档 [https://nature-lang.org/docs/syntax](https://nature-lang.org/docs/syntax)

标准库文档 [https://nature-lang.org/stds](https://nature-lang.org/stds/co)

在线试用 [https://nature-lang.org/playground](https://nature-lang.org/playground)

LSP  https://github.com/nature-lang/nls 参考 README.md

## 项目示例

[parker](https://github.com/weiwenhao/parker) 轻量打包工具

[llama.n](https://github.com/weiwenhao/llama.n) Llama2 nature 语言实现

[tetris](https://github.com/weiwenhao/tetris) 基于 raylib 实现的俄罗斯方块，仅支持 macos

[playground](https://github.com/weiwenhao/playground) playground server api 实现

更多语法示例 https://github.com/nature-lang/nature/tree/master/tests/features/cases

## 贡献指南

https://nature-lang.org/docs/contribute

## License

本项目的编译器前端、后端、runtime 等项目源码受 Apache License (Version 2.0) 限制。当前项目中包含的 nature 源码文件(.n) 如标准库等使用 MIT 许可证。
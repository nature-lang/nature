
#  nature 编程语言

通用开源编程语言与编译器，期望为开发者提供**简洁优雅**的开发体验，能够**简单高效**构建出安全可靠的跨平台软件。


## 特性

- ✓ 简洁、自然、一致性的语法设计
- ✓ 独立的编译系统，不依赖 llvm，支持跨平台编译
- ✓ 完善的类型系统，支持泛型、空值安全、错误处理
- ✓ 自动 GC，具有非常短暂的 STW
- ✓ 模块化与包管理系统 npkg
- ✓ 内置共享栈协程，每秒进行数百万的协程切换
- ✓ 内置 libuv 与协程合作处理 IO 事件循环
- ✓ 内置常用数据结构及标准库实现，并且方便的和 C 语言交互
- ✓ 编辑器 lsp 支持
- ○ 渐进式 GC，能够进行手动内存管理
- ○ 测试 DSL，高效稳定利用 AI 编码
- ○ macho 跨平台连接器，目前缺少 macho 链接器 darwin 无法跨平台编译。
- ○ 协作式调度系统完善
- ○ 跨平台编译支持 wasm、risc64 平台
- ○ 编译为可读的 golang 编程语言

## 项目概况

nature 编程语言已经达到可用版本，语法 API 基本稳定，在 1.0 版本之前不会有大幅的变化，会有小范围的语法改动。

当前版本 nature 源码支持编译的目标平台有
- linux/amd64
- linux/arm64
- darwin/amd64
- darwin/arm64

nature 包含一组测试用例及标准库用来测试语法的可用性，但未经过中大型的项目测试，所以还有较多的 bug。

后续版本的主要工作有
- 标准库完善
- 收集用户反馈
- 项目验证及 bug 修复
- 完善语言特性


官网: [https://nature-lang.org](https://nature-lang.org)

## 设计理念

golang 是我工作使用的编程语言，其语法简单，拥有非常优秀且高性能的跨平台编译器和 runtime 实现，拥有先进的协程设计风格和高性能网络 I/O 以及完善的标准库支持。但也有一些不方便的地方

- 语法过于简单导致表达能力不足
- 类型系统不够完善，缺乏 nullable，枚举参数，泛型(现在已经有了)等
- 错误处理机制繁琐
- 自动 GC 和抢占式调度的设计虽然非常优秀，但是也让 golang 的应用范围受限。

nature 在设计理念上是对 golang 编程语言的延续与优化，并追寻一定的差异性，就如特性一栏所述一般。

基于 nature 编程语言的现有特性，其适用于游戏引擎和游戏开发、科学计算和 AI、操作系统和物联网，以及 Web 开发等领域。

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

1. [parker](https://github.com/weiwenhao/parker) 轻量打包工具
2. [llama.n](https://github.com/weiwenhao/llama.n) Llama2 nature 语言实现
3. [tetris](https://github.com/weiwenhao/tetris) 基于 raylib 实现的俄罗斯方块，仅支持 macos
4. [playground](https://github.com/weiwenhao/playground) playground server api 实现

更多语法示例 https://github.com/nature-lang/nature/tree/master/tests/features/cases

## 贡献指南

https://nature-lang.org/docs/contribute

## License

本项目的编译器前端、后端、runtime 等项目源码受 Apache License (Version 2.0) 限制。当前项目中包含的 nature 源码文件(.n) 如标准库等使用 MIT 许可证。
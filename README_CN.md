<p align="center"><a href="https://nature-lang.org" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>  


# nature 编程语言

nature 是现代系统级编程语言和编译器，语法上追求简洁优雅，关注使用者的编写与阅读体验。

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

编码示例 👉 [cases](https://github.com/nature-lang/nature/tree/master/tests/blackbox/cases)

## 🪶 执照

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

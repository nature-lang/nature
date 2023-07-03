

<p align="center"><a href="https://nature-lang.org" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>


# nature 编程语言

nature 是一种编程语言，语法上追求简洁优雅，关注使用者的编写与阅读体验。

在编程语言层面，nature 具有以下特点:

- 类型系统，null 安全，泛型，联合类型
- 静态交叉编译，能够不借助任何第三方组件从 nature 源码编译成目标机器可执行文件
- 渐进式 GC，支持自动回收与手动回收
- 内置 list/map/set/tuple 数据结构
- 包管理与模块管理
- 函数标签/闭包/错误处理/协程
- 集成SSA/线性扫描寄存器分配/反射机制/汇编器与连接器

随着标准库以逐步完善，nature 可以应用于游戏引擎与游戏制作、科学与 AI 计算、操作系统与物联网、WEB 开发。其中游戏引擎将作为 nature 1.0+ 的主要任务。

nature 适合于开源创作者/独立创作者以及学习和研究使用，我们不仅希望你能够在语言中得到便利，同样也希望你使用 nature 创作快乐且有趣的事情。

通过官方网站，您可以获得更多信息以及它的文档。

官网: [https://nature-lang.org](https://nature-lang.org/)

文档: [https://nature-lang.org/docs/getting-started/hello-world](https://nature-lang.org/docs/getting-started/hello-world)


## ⚙️ 安装

从 [releases](https://github.com/nature-lang/nature/releases) 中下载并解压 natrue 安装包。推荐将解压后的 nature 文件夹移动到 `/usr/local/` 下，并将 `/usr/local/nature/bin` 目录加入到系统环境变量。

创建一个 main.n 文件，写入以下内容

```nature
print("hello nature")
```

编译并执行

```shell
> nature build main.n && ./main
hello nature
```

使用 docker 快速编译并执行

```shell
docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'
```



## 🍼 编码示例


错误处理
![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630115906.png)

泛型
![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630132324.png)

联合类型

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630132845.png)

协程
![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120423.png)

函数标签
![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120447.png)


http server
![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120523.png)



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
| v0.4.0-beta | 基础标准库/基础语法完善   | 2023-11      |
| v0.5.0-beta | lsp 开发/错误追踪优化     | 2024-02      |
| v0.6.0-beta | 小型测试用例/ bug 修复    | 2024-04      |
| v0.7.0      | 中型测试用例/稳定语法 api | 2024-07      |
| v0.8.0+     | 正式版本发布相关准备工作  | 2024-09      |
| v1.0.0      | 正式版本发布              | 2025-        |


## 🧭 设计理念

编程语言中存在一等公民的说法，比如 javascript 中的一等公民是函数，虽然 nature 中的函数也能够作为值传递等高阶用法，但是 nature 的一等公民并不是函数。那对于 nature 来说最重要的是什么呢

编译速度？运行速度？安全性？简洁性？都不是。即使我们追求简洁之道，但是绝对不会为此而牺牲开发者的使用便利。

所以对于 nature 来说最重要的一定是开发者，为开发者带来便利，让开发者看起来顺眼，写起来顺手。当然这不代表 nature 不具备编译速度/运行速度/安全性/简洁性等等特性，我们致力于协调便利性与这些常见特性的平衡。但是一旦遇到不可协调的冲突时，则优先考虑的是开发者的便利性，而不是编译速度/复杂度/简洁性等等。

例如在单引号和双引号字符串的选择中，虽然绝大多数强类型语言选择了双引号作为标准字符串，但是单引号能够减少一次 shift 的输入，减轻开发者的小拇指负担。同时让阅读更加简洁。关于取消 if 和 for 中的括号同样如此。

例如在 nature 中很少去发明新语法糖，而是尽可能在其他语言中选择已经存在的众所周知的语法糖，从而减轻开发者的学习与心理负担。包括关键字的缩写也应该选择众所周知的缩写。如 i8 代替 int8_t，fn 代替 func/function 是因为 rust 已经传播发展了这种关键字的含义，所以能够极大的避免理解歧义与学习负担。


## 🍺 贡献指南

有多种方式参与 nature 的贡献，包括但不限于提交 BUG、分享想法、社区讨论、编码参与、文档改进、规范制定、资料贡献、捐赠等。

nature 基于 ANSI C11 和 musl libc 进行开发。源码上追求简单可读，不使用复杂的第三方库，文件名/目录名/关键字都采用小写下划线分词，唯一的例外是宏定义使用大写下划线分词。

源码目录结构/编译/相关资料参考 [https://nature-lang.org/docs/prologue/contribution-guide](https://nature-lang.org/docs/prologue/contribution-guide)


## 🐳 社区交流

想法和问题推荐使用 github issue 进行讨论让更多人能够关注并参与。

discard: [https://discord.gg/xYYkVaKZ](https://discord.gg/xYYkVaKZ)

微信群: 添加微信号 `nature-lang` 备注 “申请加群”

github 讨论社区: [https://github.com/nature-lang/nature/discussions](https://github.com/nature-lang/nature/discussions)


## 🫧 FAQ

1.为什么变量和函数参数的类型是前置的，而函数的返回值类型是后置的？

关于类型的位置的说明 [https://github.com/nature-lang/nature/issues/7](https://github.com/nature-lang/nature/issues/7)

2.logo 的含义？

logo 是一艘星际飞船，意为《三体》中的自然选择号。

3.为什么语言关键字中包含 emoji？

是的，因为 mojo.🔥 已经这么做了。emoji 关键字会作为 nature 的一种特性延续下去，在后续的版本中新增的语法糖也会尽量采用 emoji 作为关键字。希望能够 emoji 在编码的时候能够带来轻松的感觉。


## 🪶 执照

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

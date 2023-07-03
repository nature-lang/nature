

<p align="center"><a href="https://nature-lang.org" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>


# The Nature Programming Language

nature is a programming language that aims for simplicity and elegance in its syntax, focusing on the writing and reading experience of its users.

At the language level, nature has the following features:

- Type system with null safety, generics, and union types
- Static cross-compilation that allows generating executable files for target machines without relying on any third-party components
- Incremental garbage collection with support for automatic and manual memory reclamation
- Built-in data structures like lists, maps, sets, and tuples
- Package and module management
- Function labels, closures, error handling, and coroutines
- Integration with SSA, linear scan register allocation, reflection mechanisms, assemblers, and linkers

As the standard library gradually improves, nature can be applied to game engines and game development, scientific and AI computing, operating systems and IoT, and web development. Game engines will be the main focus of nature 1.0+.

nature is suitable for open-source creators, independent developers, as well as learning and research purposes. We hope that nature not only provides convenience in the language itself but also allows you to create enjoyable and interesting things.

You can find more information and documentation on the official website.

Website: [https://nature-lang.org](https://nature-lang.org/)

Documentation: [https://nature-lang.org/docs/getting-started/hello-world](https://nature-lang.org/docs/getting-started/hello-world)


## ⚙️ Installation

To get started with Nature, download and extract the nature installation package from the [releases](https://github.com/nature-lang/nature/releases). We recommend moving the extracted nature folder to `/usr/local/` and adding the `/usr/local/nature/bin` directory to the system's environment variables.

Create a main.n file with the following contents:

```nature
print("hello nature")
```

Compile and execute:

```shell
# docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'
> nature build main.n && ./main
hello nature
```

Use Docker for quick compilation and execution:

```shell
docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'
```



## 🍼 Code Examples


Error Handling

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630115906.png)

Generics

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630132324.png)

Union Types

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630132845.png)

Coroutines

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120423.png)

Function Labels

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120447.png)


HTTP Server

![](https://raw.githubusercontent.com/weiwenhao/pictures/main/blogs20230630120523.png)


## 🌱 Release Plan

nature follows [Semantic Versioning](https://semver.org/), where versions 0.1 to 1.0 consist of two parts:

The upper part always carries the beta tag, indicating that it is not production-ready.


The lower part contains stable and backward-compatible syntax and APIs. During this stage, nature can be used for personal independent/open-source projects but does not provide LTS versions.

When version 1.0 is released, nature will be officially used for open source/commercial projects and will have an LTS version.

| Version | Content                                   | Estimated Release Date |
|---------|-------------------------------------------|------------------------|
| v0.1.0-beta | Initial syntax release                    | 2023-05                |
| v0.2.0-beta | Type system and syntax improvements       | 2023-07                |
| v0.3.0-beta | Package management and syntax improvements| 2023-09                |
| v0.4.0-beta | Basic standard library and syntax improvements | 2023-11           |
| v0.5.0-beta | LSP development and error tracking optimization | 2024-02          |
| v0.6.0-beta | Small test cases and bug fixes            | 2024-04                |
| v0.7.0      | Medium test cases and stable syntax API    | 2024-07                |
| v0.8.0+     | Preparation for the official release      | 2024-09                |
| v1.0.0      | Official release                          | 2025-                   |



## 🧭 Design Philosophy

There is a saying in programming languages about first-class citizens, such as functions being the first-class citizens in JavaScript. While functions in nature can be passed as values and used in higher-order functions, functions are not the primary focus in nature. So what is the most important thing in nature?

Is it compilation speed? Runtime speed? Safety? Simplicity? None of these. Even though we pursue the path of simplicity, we will never sacrifice the convenience of developers for it.

Therefore, the most important thing for nature is developers. It aims to provide convenience to developers, to make the language look pleasant and feel comfortable to write. This doesn't mean that nature lacks features like fast compilation, runtime speed, safety, and simplicity. We strive to strike a balance between convenience and these common features. However, if there is an irreconcilable conflict, the priority will be the convenience of developers rather than compilation speed, complexity, or simplicity.

For example, in the choice between single quotes and double quotes for strings, although most strongly-typed languages choose double quotes as the standard for strings, single quotes can reduce one shift key press, reducing the burden on the developer's pinky finger. It also makes the code more concise. The same applies to the removal of parentheses in `if` and `for` statements.

In nature, we rarely invent new syntactic sugar but instead prefer to choose well-known syntactic sugar from other languages. This reduces the learning and mental burden on developers. Even the abbreviations of keywords should be well-known. For example, using `i8` instead of `int8_t` and `fn` instead of `func`/`function` is because Rust has already popularized these abbreviations, which greatly reduces the potential for misunderstanding and learning burden.


## 🍺 Contribution Guidelines

There are multiple ways to contribute to nature, including but not limited to submitting bugs, sharing ideas, participating in community discussions, coding, improving documentation, establishing standards, contributing resources, and donating.

Nature is developed based on ANSI C11 and musl libc. The source code aims to be simple and readable, without using complex third-party libraries. File and directory names, as well as keywords, use lowercase with underscores as word separators, with the only exception being macro definitions, which use uppercase with underscores.

For information on source code directory structure, compilation, and related resources, please refer to [https://nature-lang.org/docs/prologue/contribution-guide](https://nature-lang.org/docs/prologue/contribution-guide)


## 🐳 Community Communication

For ideas and questions, it is recommended to use GitHub issues to facilitate wider attention and participation.

Discord: [https://discord.gg/xYYkVaKZ](https://discord.gg/xYYkVaKZ)

微信: Add WeChat ID `nature-lang` and mention "申请加群"

GitHub Discussion: [https://github.com/nature-lang/nature/discussions](https://github.com/nature-lang/nature/discussions)


## 🫧 FAQ

1.Why are variable and function parameter types placed before the identifier, while the return type of a function is placed after the identifier?

Explanation about the placement of types: [https://github.com/nature-lang/nature/issues/7](https://github.com/nature-lang/nature/issues/7)

2.What is the meaning of the logo?

The logo represents a starship, symbolizing the "Natural Selection" in "The Three-Body Problem" novel.

3.Why do language keywords include emojis?

Yes, because mojo.🔥 already did it. Emoji keywords will be a feature carried over from mojo. In future versions, newly introduced syntactic sugar will also use emojis as keywords as much as possible. The aim is to bring a light-hearted feeling to coding.


## 🪶 License

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

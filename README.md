<p align="center"><a href="https://nature-lang.org" target="_blank"><img src="https://raw.githubusercontent.com/weiwenhao/pictures/main/blogslogo_300.png" width="400" alt="nature Logo"></a></p>

# Nature Programming Language

Nature is the modern systems programming language and compiler, striving for elegant and concise syntax while prioritizing the writing and reading experience for developers.

Key features of nature at the language level include:

- Type system, null safety, generics, union types
- In-house compiler/assembler/linker, not reliant on llvm. Supports compilation for amd64/riscv64/wasm architectures
- Non-intrusive interaction with C for efficient and high-performance development
- Progressive GC, supports both automatic and manual GC
- Built-in vec/map/set/tup data structures
- Package and module management
- Function tags/closures/error prompts/runtime stack traces/coroutines
- Integrated SSA/linear scan register allocation/reflection/assembler & linker

With the continual refinement of its standard library, nature is applicable for game engines and game development, scientific and AI computing, operating systems and IoT, and WEB development. The game engine will be the core focus for nature from versions 0.7.0 to 1.0+.

Nature is suitable for open-source authors, independent developers, as well as for learning and research. Not only do we hope you find the language convenient, but we also wish that you enjoy creating wonderful things with nature.

For more information and documentation, visit our official website:

Official website: [https://nature-lang.org](https://nature-lang.org/)

Documentation: [https://nature-lang.org/docs/getting-started/hello-world](https://nature-lang.org/docs/getting-started/hello-world)

> ❗️ Current version: 0.4.0-beta. Yet to integrate riscv64 wasm architecture compilation/manual GC/function tags/coroutines. All other features are integrated. **Nature is set to release its community-friendly version (0.7.0) soon. We invite you to test it out and contribute.**

## ⚙️ Installation

Download and unzip the nature package from [releases](https://github.com/nature-lang/nature/releases). We recommend moving the unzipped `nature` folder to `/usr/local/` and adding `/usr/local/nature/bin` to the system's environment variable.

Create a file named `main.n` with the following content:

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

Compile and execute:

```bash  
> nature build main.n && ./main  
fib result is 832040
```  

Quickly compile and execute using the docker integrated environment:

```shell  
docker run --rm -it -v $PWD:/app --name nature naturelang/nature:latest sh -c 'nature build main.n && ./main'  
```  

## 🍺 Contribution Guide

There are many ways to contribute to nature: reporting BUGs, sharing ideas, participating in community discussions, coding, improving documentation, establishing standards, contributing resources, donations, and more.

Nature is developed based on ANSI C11 and musl libc. The codebase aims for simplicity and readability, avoiding complex third-party libraries. Filenames, directory names, and keywords all use lowercase with underscores. The only exception is macro definitions which use uppercase with underscores.

For source code directory structure, compilation, and related resources, refer to [https://nature-lang.org/docs/prologue/contribution-guide](https://nature-lang.org/docs/prologue/contribution-guide).

All contributions to the standard library will eventually be merged into the main repository. Before embarking on feature development, please initiate communication via an issue for preliminary discussions and API design.

## 🐳 Community Interaction

For ideas and issues, we recommend discussing on Github issues so that more people can pay attention and participate.

Github Discussion Community: [https://github.com/nature-lang/nature/discussions](https://github.com/nature-lang/nature/discussions)


## 🍼 Coding Example

coding examples 👉 [cases](https://github.com/nature-lang/nature/tree/master/tests/cases)

## 🪶 License

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

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

## 🌱 Release Schedule

Nature's versioning adheres to [Semantic Versioning](https://semver.org/). Versions 0.1 ~ 1.0 have two parts:

The first half always has a beta indication, denoting that it's not ready for production.

The second half is stable with backward-compatible syntax API. By this time, nature is suitable for personal indie/open-source projects, but LTS versions aren't provided.

With the release of version 1.0, nature will be officially used for open-source/commercial projects and will have an LTS version.

| Version     | Content                                      | Expected Release Date |  
| ----------- | -------------------------------------------- | --------------------- |  
| v0.1.0-beta | Basic syntax release                         | 2023-05               |  
| v0.2.0-beta | Type system/Basic syntax refinement          | 2023-07               |  
| v0.3.0-beta | Package management/Basic syntax refinement   | 2023-09               |  
| v0.4.0-beta | Small test cases/Basic standard library      | 2023-11               |  
| v0.5.0-beta | LSP development/Core syntax refinement       | 2024-02               |  
| v0.6.0-beta | Medium test cases/bug fixes                  | 2024-04               |  
| v0.7.0      | Large test cases/Stable syntax API           | 2024-07               |  
| v0.8.0+     | Preparations for the official release        | 2024-09               |  
| v1.0.0      | Official release                             | 2025-                 |  

Current version: 0.4.0-beta. Key functionalities still being planned will be gradually integrated in upcoming versions.

- Integration and optimization of essential syntaxes like switch/try
- wasm architecture compilation
- Coroutine support
- Compilation for the darwin system
- Function tags support
- Progressive GC refinement
- riscv architecture compilation
- Compilation for the windows system

## 🧭 Design Philosophy

In programming languages, there's a notion of first-class citizens. For example, in JavaScript, functions are the first-class citizens. Although in nature, functions can also be passed as values and have higher-order usages, they aren't its first-class citizens. So, what's most important for nature?

Compilation speed? Execution speed? Safety? Simplicity? None of these. Even though we aim for simplicity, we won't compromise on developer convenience.

For nature, the developer is paramount. We prioritize offering convenience, ensuring the code is aesthetically pleasing and intuitive. That's not to say nature doesn't provide fast compilation/execution, safety, simplicity, etc. We aim to balance these attributes with user-friendliness. However, when conflicts arise, the developer's convenience takes precedence.

For instance, despite most strong-typed languages opting for double quotes for standard strings, we chose single quotes to save a shift input, reducing strain on the pinky and offering a more concise read. The omission of brackets in 'if' and 'for' also stems from this principle.

Nature rarely introduces new syntactic sugar. Instead, we often choose already-existing, widely-recognized syntactic sugar from other languages, easing the learning curve and mental load for developers. Keyword abbreviations also follow popular abbreviations, e.g., i8 instead of int8_t, fn instead of func/function. fn is a common keystroke, and Rust has already popularized fn/i8, hence it drastically reduces potential misunderstandings and learning burdens.

## 🍺 Contribution Guide

There are many ways to contribute to nature: reporting BUGs, sharing ideas, participating in community discussions, coding, improving documentation, establishing standards, contributing resources, donations, and more.

Nature is developed based on ANSI C11 and musl libc. The codebase aims for simplicity and readability, avoiding complex third-party libraries. Filenames, directory names, and keywords all use lowercase with underscores. The only exception is macro definitions which use uppercase with underscores.

For source code directory structure, compilation, and related resources, refer to [https://nature-lang.org/docs/prologue/contribution-guide](https://nature-lang.org/docs/prologue/contribution-guide).

All contributions to the standard library will eventually be merged into the main repository. Before embarking on feature development, please initiate communication via an issue for preliminary discussions and API design.

## 🐳 Community Interaction

For ideas and issues, we recommend discussing on Github issues so that more people can pay attention and participate.

Github Discussion Community: [https://github.com/nature-lang/nature/discussions](https://github.com/nature-lang/nature/discussions)


## 🍼 Coding Example

Error Handling:

```rust
type test = struct {
    [i8] list
    var div = fn(int a, int b):int {
        if b == 0 {
            throw 'divisor cannot be zero'
        }
        return a / b
    }
}

var t = test {
    list = [1, 2, 3, 4, 5]
}

var (item, err) = try foo.list[8]
if err.has {
    println("chain access list error=", err.msg)
}

var (_, err) = try foo.div(10, 0)
if err.has {
    println("division error", err.msg)
}
```

Generics:

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

Union Types:

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

Function Tags:

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
fn sum(int a, int b):int {}
```

HTTP Server:

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

For more coding examples 👉 [cases](https://github.com/nature-lang/nature/tree/master/tests/blackbox/cases)

## 📌 FAQ

1.Does nature use type prefix or suffix?

Nature consistently uses type prefixing, including the return type of functions. A primitive design example:

`fn sum(int a, int b):int c` shows that the function return type also uses type prefixing. Omitting all idents can lead to the function type declaration `fn(int,int):int`. Typically, the return value's ident also needs to be omitted, resulting in the formal function declaration `fn sum(int a, int b):int {}`.

2.What is the meaning of nature/logo?

The logo represents a spaceship, symbolizing the "Natural Selection" ship from "Three-Body". The name "nature" is derived from this.

3.Why isn't there any performance testing and comparison?

Nature is currently in its beta phase focusing on core functionality development. There hasn't been any optimization done on the compiler backend. Hence, performance testing would be unfair and meaningless.

4.How long has nature been in development?

The main repository has been under development for almost 3 years. The actual time invested is close to 6 years. What I want to emphasize is that the Nature project won't be abandoned arbitrarily and will be continuously developed and maintained with vitality.

## 🪶 License

This project is open-sourced software licensed under the [MIT license](https://opensource.org/licenses/MIT). as a programming language, source files (.n files) and compiled binary files generated during use of Nature are not subject to Open-source license restrictions.

Copyright (c) 2020-2023 WEIWENHAO, all rights reserved.

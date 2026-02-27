# Nature Programming Language  
  
A general-purpose system programming language and compiler, designed to build high-performance, safe and reliable, and cross-platform software in a concise and elegant way.

## Features

- Lightweight, concise, and consistent syntax design, easy to master and get started quickly
- Strong type system, static analysis and compilation, memory safety, exception handling, making it easy to write secure and reliable software
- Built-in concurrency primitives: go/future/channel/select
- Comprehensive type system supporting generics, enum, tagged union, interface, nullable(?), errable(!)
- Compiles directly to machine code for the target platform, does not rely on LLVM, and supports cross-compilation.
- Simple deployment, efficient compilation, static linking based on musl libc with good cross-platform characteristics
- High-performance C FFI implementation with zero-overhead calls to C standard library functions
- High-performance GC implementation with very short STW (Stop The World)
- High-performance memory allocator implementation, referencing tcmalloc
- High-performance shared-stack coroutine implementation, capable of millions of coroutine switches per second
- High-performance IO based on libuv implementation
- High-performance runtime and compiler based on pure C implementation
- Built-in data structures vec/string/map/set/tup and common standard library implementations
- Test as a first-class citizen, write test directly in source files
- Centralized package management system npkg
- Editor LSP support
  
## Overview

The nature programming language has reached an early usable version, and its core syntax features are now in place. Key features still to be completed include unsafe runtime mode, LLM coding adaptation, C target adaptation, and WASM3.0 target adaptation.

The current version supports compilation for the following target architectures: linux_amd64, linux_arm64, linux_riscv64, darwin_amd64, darwin_arm64. 

Nature includes a set of test cases and standard libraries to test the usability of basic functionality and syntax, includes a set of small to medium-sized projects to test overall usability, but has not yet been tested in large-scale projects.
  
Official website: https://nature-lang.org  



## Install  
  
Download and extract the nature installer from [releases](https://github.com/nature-lang/nature/releases) (note the correct permissions). Move the extracted nature folder to `/usr/local/` and add the `/usr/local/nature/bin` directory to the system environment variable.  
  
> If you need to install into another directory you need to manually set the NATURE_ROOT environment variable to the corresponding directory  
  
Run the `nature -v` command to check the version, and run the `nature -h` command to check the help  
  
```sh
> nature -v
nature vx.x.x - release build 1970-01-01
```   

create a main.n file  
  
```js
import fmt   
    
fn main() {   
    fmt.printf('hello nature')
}   
```   

compile and execute  
  
```sh
> nature build main.n && ./main   
hello nature 
```   
  
## Documentation  
  
Quick start https://nature-lang.org/docs/get-started

Syntax documentation https://nature-lang.org/docs/syntax

Standard library documentation https://nature-lang.org/stds/co

Try in browser https://nature-lang.org/playground  
  
## Examples

MySQL/PostgreSQL/Redis drivers https://github.com/weiwenhao/dbdriver

SQLite driver https://github.com/weiwenhao/nature-sqlite

API Framework https://github.com/weiwenhao/emoji-api

Lightweight container packaging tool and runtime https://github.com/weiwenhao/parker

Llama2 inference model implementation https://github.com/weiwenhao/llama.n

Tetris implementation based on raylib https://github.com/weiwenhao/tetris

Nature WebView Package https://github.com/wzzc-dev/nature-webview

More syntax examples https://github.com/nature-lang/nature/tree/master/tests/features/cases


## Design Philosophy

The Nature programming language is a lightweight, simple, and easy-to-learn programming language that references golang in terms of design philosophy and runtime architecture.

Nature focuses on memory safety in its syntax design, has comprehensive type system support and convenient error handling methods, uses files as module units, and adopts a centralized package management approach based on package.toml.

Nature natively supports concurrency primitive go+select+channel, which has excellent concurrency performance in highly concurrent IO applications. Performance will be further improved when the official version is released.

Nature has a completely self-developed compiler, assembler, and linker (which will later transition to zig ld), making Nature more flexible and controllable. The source code is simple without complex third-party dependencies, making it easy to participate in contributions and perform highly customized optimizations according to language and technology development.

Benefiting from simple syntax design, automated memory management, compile-time static analysis and other features, it brings extremely low coding burden, making the Nature programming language very suitable for AI coding and programming beginners.

As a general-purpose programming language, based on existing language features and standard library implementations, Nature can be used in various fields such as web development, command-line programs, databases, network middleware, container systems, IoT devices, programming education, operating systems, game engines and game development.

## Benchmark

https://github.com/nature-lang/benchmark

Linux Ubuntu VM（Kernel version 6.17.8，aarch64, Mac M4, 9 cores, 8G memory）

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
  
## Contribution Guidelines

Development Guide https://github.com/nature-lang/nature/blob/master/DEVELOPMENT.md

Contribution Guide https://nature-lang.org/docs/contribute

TODO  https://github.com/nature-lang/nature/issues/131
  
## License  
  
The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nature source files (.n) such as standard libraries included in the current project use the MIT license.

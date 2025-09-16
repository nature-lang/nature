# Nature Programming Language  
  
A general-purpose open-source programming language and compiler designed to provide developers with an **elegant and concise** development experience, enabling them to build secure and reliable cross-platform software **simply and efficiently**.  

## Features

- ✓ Lightweight, concise, and consistent syntax design, easy to master and get started quickly
- ✓ Strong type system, static analysis and compilation, memory safety, exception handling, making it easy to write secure and reliable software
- ✓ Built-in concurrency primitives: go/future/channel/select
- ✓ Compiles directly to target platform machine code with cross-compilation and linking support, no third-party libraries like llvm required
- ✓ Simple deployment, efficient compilation, static linking based on musl libc with good cross-platform characteristics
- ✓ Comprehensive type system supporting generics, union types, interfaces, nullable(?), errable(!)
- ✓ High-performance GC implementation with very short STW (Stop The World)
- ✓ High-performance memory allocator implementation, referencing tcmalloc
- ✓ High-performance shared-stack coroutine implementation, capable of millions of coroutine switches per second
- ✓ High-performance IO based on libuv implementation
- ✓ High-performance runtime and compiler based on pure C implementation
- ✓ Built-in data structures vec/map/set/tup and common standard library implementations
- ✓ Function calls follow system ABI, built-in libc, convenient for calling C standard library functions
- ✓ Centralized package management system npkg
- ✓ Editor LSP support
- ○ More controllable GC implementation, such as gc switch, fast free obj, etc.
- ○ Cross-linker based on zig build source code refactoring
- ○ Improvement of collaborative scheduling system
- ○ Compilation target support for golang, wasm
  
## Project Overview

The nature programming language has reached an early usable version, with a basically stable syntax API that will not change significantly before version 1.0. Future versions will add some necessary and commonly used syntax features such as enum, ternary operators, struct labels, etc.

The current version supports compilation for the following target architectures: linux_amd64, linux_arm64, linux_riscv64, darwin_amd64, darwin_arm64. Future versions will leverage zig ld to compile for Windows platforms.

nature includes a set of test cases and standard libraries to test the usability of basic functionality and syntax, includes a set of small to medium-sized projects to test overall usability, but has not yet been tested in large-scale projects.
  
Official website: https://nature-lang.org  


## Design Philosophy

The nature programming language is a lightweight, simple, and easy-to-learn programming language that references golang in terms of design philosophy and runtime architecture.

nature focuses on memory safety in its syntax design, has comprehensive type system support and convenient error handling methods, uses files as module units, and adopts a centralized package management approach based on package.toml.

nature natively supports concurrency primitives go+select+channel. Thanks to the libuv network library and libaco shared-stack coroutines, it has excellent concurrent performance in IO applications. When the official version is released, nature's performance will be further improved.

nature has a completely self-developed compiler, assembler, and linker (which will later transition to zig ld), making nature more flexible and controllable. The source code is simple without complex third-party dependencies, making it easy to participate in contributions and perform highly customized optimizations according to language and technology development.

Benefiting from simple syntax design, automated memory management, compile-time static analysis and other features, it brings extremely low coding burden, making the nature programming language very suitable for AI coding and programming beginners.

As a general-purpose programming language, based on existing language features and standard library implementations, nature can be used in various fields such as web development, command-line programs, databases, network middleware, container systems, IoT devices, programming education, operating systems, game engines and game development.


## Install  
  
Download and extract the nature installer from [releases](https://github.com/nature-lang/nature/releases) (note the correct permissions). Move the extracted nature folder to `/usr/local/` and add the `/usr/local/nature/bin` directory to the system environment variable.  
  
> If you need to install into another directory you need to manually set the NATURE_ROOT environment variable to the corresponding directory  
  
Run the `nature -v` command to check the version, and run the `nature -h` command to check the help  
  
```sh
> nature -v
nature v0.6.0 - release build 2025-10-01
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
  
## Project Examples

MySQL/PostgreSQL/Redis drivers https://github.com/weiwenhao/dbdriver

SQLite driver https://github.com/weiwenhao/nature-sqlite

API Framework https://github.com/weiwenhao/emoji-api

Lightweight container packaging tool and runtime https://github.com/weiwenhao/parker

Llama2 inference model implementation https://github.com/weiwenhao/llama.n

Tetris implementation based on raylib https://github.com/weiwenhao/tetris

More syntax examples https://github.com/nature-lang/nature/tree/master/tests/features/cases  
  
## Contribution Guidelines
  
Guide https://nature-lang.org/docs/contribute

TODO https://github.com/nature-lang/nature/issues/131  
  
## License  
  
The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nature source files (.n) such as standard libraries included in the current project use the MIT license.

# Nax Programming Language  
  
A general-purpose system programming language and compiler, designed to build high-performance, safe and reliable, and cross-platform software in a concise and elegant way.

## Features

- Lightweight, concise, and consistent syntax design, easy to master and get started quickly
- Strong type system, static analysis and compilation, memory safety, exception handling, making it easy to write secure and reliable software
- Built-in concurrency primitives: go/future/channel/select
- Comprehensive type system supporting generics, enum, tagged union, interface, nullable(?), errable(!)
- Compiles directly to machine code for the target platform, does not rely on LLVM, and supports cross-compilation.
- Simple deployment and efficient compilation with bundled target sysroots and statically linked third-party runtimes
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

> nax was formerly known as nature and has been renamed to nax.

The nax programming language has reached an early usable version, and its core syntax features are now in place. Key features still to be completed include unsafe runtime mode, LLM coding adaptation, C target adaptation, and WASM3.0 target adaptation.

The current version supports compilation for the following targets: linux_amd64, linux_arm64, linux_riscv64, darwin_amd64, darwin_arm64, and windows_amd64.

Nax includes a set of test cases and standard libraries to test the usability of basic functionality and syntax, includes a set of small to medium-sized projects to test overall usability, but has not yet been tested in large-scale projects.
  
Official website: https://naxlang.org  



## Install  
  
Download and extract the nax installer from [releases](https://github.com/naxlang/nax/releases) (note the correct permissions). Move the extracted nax folder to `/usr/local/` and add the `/usr/local/nax/bin` directory to the system environment variable.  
  
> If you need to install into another directory you need to manually set the NAX_ROOT environment variable to the corresponding directory  
  
Run the `nax -v` command to check the version, and run the `nax -h` command to check the help  
  
```sh
> nax -v
nax vx.x.x - release build 1970-01-01
```   

create a main.n file  
  
```js
import fmt   
    
fn main() {   
    fmt.printf('hello nax')
}   
```   

compile and execute  
  
```sh
> nax build main.n && ./main   
hello nax 
```   

Cross-compile a Windows x64 console executable from a supported host:

```sh
nax build --target windows_amd64 main.n
```

This produces `main.exe` using Nax's built-in COFF/PE linker and bundled
Windows sysroot. Clang, LLD, MSVC, the Windows SDK, MinGW, and Zig are not
required on the final-user build path.

## Formatting

Nax ships with one canonical formatter style. The formatter is convention-over-configuration: there are no style profiles or user-tunable formatting rules.

The same formatter core is exposed through both the CLI and the language server:

- `nax fmt` is the user-facing toolchain command
- `nls fmt` is the underlying formatter command used by the language server and wrapper
- editor formatting uses the same canonical output through LSP `textDocument/formatting`

### CLI usage

Print formatted output to stdout:

```sh
nax fmt < path/to/file.n
nls fmt < path/to/file.n

nax fmt path/to/file.n
nls fmt path/to/file.n
```

Directories are supported and are walked recursively for `.n` files:

```sh
nax fmt src/
nls fmt src/
```

Write formatted output back to the file in place:

```sh
nax fmt -w path/to/file.n
nls fmt --write path/to/file.n
```

Check whether files are already formatted. This exits with status `1` if any file would change:

```sh
nax fmt --check path/to/file.n
nls fmt --check path/to/file.n
```

Show a unified diff instead of rewriting files. This also exits with status `1` if any file would change:

```sh
nax fmt --diff path/to/file.n
nls fmt --diff path/to/file.n
```

List files whose formatting differs from the canonical style:

```sh
nax fmt -l src/
nls fmt -l src/
```

Report all lexer errors instead of only the first one:

```sh
nax fmt -e path/to/file.n
nls fmt -e path/to/file.n
```

Notes:

- default mode prints formatted output to stdout and does not modify files
- if no path is provided, the formatter reads from stdin
- directory inputs are walked recursively and only `.n` files are formatted
- `--write` cannot be combined with `--check` or `--diff`
- formatting invalid syntax is not supported; formatter errors are reported instead of producing best-effort output
- formatter output is intended to be stable and idempotent
  
## Documentation  
  
Quick start https://naxlang.org/docs/get-started

Syntax documentation https://naxlang.org/docs/syntax

Standard library documentation https://naxlang.org/stds/co

Try in browser https://naxlang.org/playground  
  
## Examples

MySQL/PostgreSQL/Redis drivers https://github.com/weiwenhao/dbdriver

SQLite driver https://github.com/weiwenhao/nax-sqlite

API Framework https://github.com/weiwenhao/emoji-api

Lightweight container packaging tool and runtime https://github.com/weiwenhao/parker

Llama2 inference model implementation https://github.com/weiwenhao/llama.n

Tetris implementation based on raylib https://github.com/weiwenhao/tetris

Nax WebView Package https://github.com/wzzc-dev/nax-webview

More syntax examples https://github.com/naxlang/nax/tree/master/tests/features/cases


## Design Philosophy

The Nax programming language is a lightweight, simple, and easy-to-learn programming language that references golang in terms of design philosophy and runtime architecture.

Nax focuses on memory safety in its syntax design, has comprehensive type system support and convenient error handling methods, uses files as module units, and adopts a centralized package management approach based on package.toml.

Nax natively supports concurrency primitive go+select+channel, which has excellent concurrency performance in highly concurrent IO applications. Performance will be further improved when the official version is released.

Nax has a self-developed compiler, assembler, and built-in ELF, Mach-O, and
COFF/PE linkers, making Nax more flexible and controllable without requiring
LLVM or a host linker on the ordinary build path. The source code is simple
without complex third-party dependencies, making it easy to participate in
contributions and perform highly customized optimizations according to language
and technology development.

Benefiting from simple syntax design, automated memory management, compile-time static analysis and other features, it brings extremely low coding burden, making the Nax programming language very suitable for AI coding and programming beginners.

As a general-purpose programming language, based on existing language features and standard library implementations, Nax can be used in various fields such as web development, command-line programs, databases, network middleware, container systems, IoT devices, programming education, operating systems, game engines and game development.

## Benchmark

https://github.com/nature-lang/benchmark

Linux Ubuntu VM（Kernel version 6.17.8，aarch64, Mac M4, 9 cores, 8G memory）

**IO: HTTP Server**

`ab -n 100000 -c 1000 http://127.0.0.1:8888/`

| Language | Version | QPS (req/sec) | Mean Response Time |
|----------|---------|---------------|---------------------|
| Nax | v0.7.2  | ~104,000 | 9 ms |
| Go | 1.25.5  | ~90,000 | 11 ms |
| Node.js | v25.2.0 | ~66,000 | 14 ms |

**Call: Fibonacci(45) time consumed**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nax | v0.7.2  | ~2.4 s    |
| Go | 1.25.5  | ~2.4 s    |
| Rust | 1.92.0  | ~1.7 s    |
| Node.js | v25.2.0 | ~6.0 s    |

**CPU: Calculate 1 billion times π**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nax | v0.7.2  | ~512 ms   |
| Go | 1.25.5  | ~512 ms   |
| Rust | 1.92.0  | ~552 ms   |
| Node.js | v25.2.0 | ~850 ms   |

**FFI: Calling 100 million c fn sqrt time consumed**

| Language | Version | Mean Time |
|----------|---------|-----------|
| Nax | v0.7.2  | ~73 ms |
| Go | 1.25.5  | ~2178 ms |

**Coroutine: 1 Million Coroutine Time/Memory consumed**

| Language | Version | Creation Time | Dispatch Time | Peak Memory |
|----------|---------|---------------|---------------|-------------|
| Nax | v0.7.2  | ~559 ms | ~589 ms       | ~969 MB |
| Go | 1.25.5  | ~1035 ms | ~1047 ms      | ~2580 MB |
  
## Contribution Guidelines

Development Guide https://github.com/naxlang/nax/blob/master/DEVELOPMENT.md

Contribution Guide https://naxlang.org/docs/contribute

TODO  https://github.com/naxlang/nax/issues/131
  
## License  
  
The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nax source files (.n) such as standard libraries included in the current project use the MIT license.

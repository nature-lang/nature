# Nature Programming Language  
  
A general-purpose open-source programming language and compiler designed to provide developers with an **elegant and concise** development experience, enabling them to build secure and reliable cross-platform software **simply and efficiently**.  
  
## Features  
  
- ✓ Simple, natural, consistent syntax design, even programming beginners can easily grasp, quickly get started!
- ✓ No dependency on llvm and VM, compiles directly to target platform machine code and supports cross-compilation.
- ✓ Simple deployment, purely static linking based on musl libc, no additional dependencies, good cross-platform characteristics
- ✓ Comprehensive type system with support for generics, union types, interfaces, null-value safety, etc.
- ✓ High-performance GC implementation with a very short STW (Stop The World)
- ✓ High-performance shared-stack Coroutine implementation, capable of millions of Coroutine switches per second
- ✓ Built-in libuv to handle IO event loops in collaboration with the concatenators  
- ✓ High-performance runtime and compiler based on a pure C implementation
- ✓ Modularity and package management system npkg
- ✓ Built-in implementations of common data structures and standard libraries.
- ✓ Use try+catch for error handling, match for pattern matching, channel for concurrent communication, select for concurrent processing.
- ✓ Follow the system ABI, built-in libc, you can directly call the C language standard library functions to accelerate code development
- ✓ editor lsp support
- ○ Asymptotic GC for manual memory management.  
- ○ Test DSL, efficient and stable utilization of AI code.  
- ○ macho cross-platform linker (lack of macho linker currently prevents cross-compilation on darwin platform)
- ○ Collaborative scheduling system  
- ○ Cross-platform compilation support for wasm and risc64 platforms.  
- ○ Compile to readable golang programming language.
  
## Project Overview

The nature programming language has reached an early usable version, and the syntax API is basically stable and will not change significantly before version 1.0.

The current version of nature source code supports compilation on the following target platforms
- linux/amd64
- linux/arm64
- darwin/amd64
- darwin/arm64

nature includes a set of test cases and standard libraries to test the usability of basic functionality and syntax, and a small set of projects to test overall usability, but has not yet been tested in medium to large scale projects.

The core work to follow will be to improve the usability of the nature programming language, including standard library refinement, user feedback collection, and bug fixes.
  
Official website: [https://nature-lang.org](https://nature-lang.org)  

## Design Philosophy

Golang is the programming language I use at work. Its syntax is simple, and it boasts an excellent, high-performance cross-platform compiler and runtime implementation. It features an advanced coroutine design style, high-performance network I/O, and comprehensive standard library support. However, there are also some inconveniences:

- The overly simple syntax leads to insufficient expressive power
- The type system is not perfect, lacking features such as nullable types, enum parameters, and generics (which are now available)
- The error handling mechanism is cumbersome
- Although the design of automatic GC and preemptive scheduling is excellent, it also limits the application scope of golang

The design philosophy of nature is a continuation and optimization of the golang programming language, while also pursuing certain differences, as described in the features section. Based on the current features of the nature programming language, it is suitable for fields such as game engines and game development, scientific computing and AI, operating systems and IoT, as well as web development.

## Install  
  
Download and extract the natrue installer from [releases](https://github.com/nature-lang/nature/releases) (note the correct permissions). Move the extracted nature folder to `/usr/local/` and add the `/usr/local/nature/bin` directory to the system environment variable.  
  
> If you need to install into another directory you need to manually set the NATURE_ROOT environment variable to the corresponding directory  
  
Run the `nature -v` command to check the version, and run the `nature -h` command to check the help  
  
```sh 
> nature -v

nature v0.5.0 - release build 2025-05-01 
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
> nature build main.n && . /main   
hello nature 
```   
  
---   
  
## Documentation  
  
Quick start [https://nature-lang.org/docs/get-started](https://nature-lang.org/docs/get-started)  
  
Syntax documentation [https:// nature-lang.org/docs/syntax](https://nature-lang.org/docs/syntax)  
  
Standard library documentation [https://nature-lang.org/stds](https://nature-lang.org/stds)  
  
Try in browser [https://nature-lang.org/playground](https://nature-lang.org/playground)  
  
LSP https://github.com/nature-lang/nls Reference README.md  
  
## Project examples  
  
[parker](https://github.com/weiwenhao/parker) Lightweight packaging tool  

[llama.n](https://github.com/weiwenhao/llama.n) Llama2 nature language implementation  

[tetris](https://github.com/weiwenhao/tetris) Tetris implementation based on raylib, macos only  

[playground](https://github.com/weiwenhao/playground) playground server api implementation  
  
More syntax examples https://github.com/nature-lang/nature/tree/master/tests/features/cases  
  
## Contribution guidelines  
  
https://nature-lang.org/docs/contribute  
  
## License  
  
The source code for the compiler frontend, backend, and runtime of this project is restricted by the Apache License (Version 2.0). Nature source files (.n) such as standard libraries included in the current project use the MIT license.